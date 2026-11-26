#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace transform::freeze
{
inline constexpr std::array<const char*, 3> algorithmNames {{ "Granular", "Micro-loop", "Shaker" }};
inline constexpr std::array<const char*, 3> modeNames {{ "Live", "Hold", "MIDI" }};
inline constexpr std::array<const char*, 3> voiceModeNames {{ "Poly", "Mono", "Legato" }};
inline constexpr std::array<const char*, 2> directionNames {{ "Forward", "Reverse" }};
struct Controls
{
    float pitch = 0.0f, size = 100.0f, position = 0.0f, blend = 100.0f;
    int mode = 0, direction = 0;
    float density = 2.0f, smooth = 100.0f, drift = 0.0f, capture = 2000.0f;
    int algorithm = 0;
    float formant = 0.0f, attack = 15.0f, release = 15.0f;
    int voiceMode = 0;
    float glide = 0.0f;
};
enum class Status { bypassed, live, armed, captured, releasing };
#include "TransformFreezeCreative.h"

// One shared stereo capture, with independently pitched grain streams.
// All storage is prepared outside processing; capture is a ring-buffer view.
class Engine
{
public:
    static constexpr std::size_t maxVoices = 8, maxGrains = 8;
    void prepare(double rate, int channels)
    {
        sr = std::isfinite(rate) ? std::clamp(rate, 8000.0, 384000.0) : 44100.0;
        stereo = channels > 1;
        for (auto& channel : history)
            channel.assign(static_cast<std::size_t>(std::ceil(8.0 * sr)) + 4, 0.0f);
        fadeStep = 1.0 / (0.015 * sr);
        blendCoefficient = 1.0 - std::exp(-1.0 / (0.010 * sr));
        activityCoefficient = 1.0 - std::exp(-1.0 / (0.025 * sr));
        creative.prepare(sr, stereo);
        reset();
    }
    void reset() noexcept
    {
        creative.reset();
        enabled = frozen = false; write = valid = oldest = 0; wet = 0.0; blend = 1.0;
        target = {}; voices = {}; hold = {}; liveVoice = {}; serial = 0; captureMode = 0; random = 0x613f129bu;
        activity = 0.0; activeSamples = 0; signalAge = 0; soundHistory = 0; usableAudio = false; liveWet = 0.0; liveRatio = 1.0;
    }
    void setControls(bool active, Controls c) noexcept
    {
        c.pitch = bound(c.pitch, -24, 24, 0); c.size = bound(c.size, 20, 500, 100);
        c.position = bound(c.position, 0, 100, 0); c.blend = bound(c.blend, 0, 100, 100);
        c.density = bound(c.density, 2, 8, 2); c.smooth = bound(c.smooth, 0, 100, 100);
        c.drift = bound(c.drift, 0, 100, 0); c.capture = bound(c.capture, 100, 8000, 2000);
        c.mode = std::clamp(c.mode, 0, 2); c.direction = std::clamp(c.direction, 0, 1);
        c.algorithm = std::clamp(c.algorithm, 0, 2);
        c.voiceMode = std::clamp(c.voiceMode, 0, 2); c.glide = bound(c.glide, 0, 500, 0);
        c.formant = bound(c.formant, -24, 24, 0); c.attack = bound(c.attack, 1, 500, 15); c.release = bound(c.release, 1, 1000, 15);
        if (c.algorithm != target.algorithm) reset();
        if (c.algorithm != 0) { creative.setControls(active, c); target = c; enabled = active; return; }
        if (active && !enabled && !frozen) { write = valid = 0; blend = c.blend * 0.01; activity = 0; activeSamples = 0; signalAge = 0; soundHistory = 0; usableAudio = false; liveVoice = {}; liveWet = 0; }
        if (c.mode != target.mode) { liveVoice = {}; liveWet = 0; }
        if (!active || c.mode != target.mode)
            for (auto& v : voices) v.held = false;
        // Ratios change only at control/MIDI updates, not per sample.
        if (c.pitch != target.pitch)
        {
            hold.ratio = ratioFor(c.pitch, 60);
            liveRatio = hold.ratio;
            for (auto& v : voices) if (v.used) v.ratio = ratioFor(c.pitch, v.note);
        }
        target = c; enabled = active;
        if (!enabled && !frozen) { write = valid = 0; voices = {}; liveVoice = {}; liveWet = 0; activity = 0; activeSamples = 0; signalAge = 0; soundHistory = 0; usableAudio = false; }
    }
    void disable() noexcept { setControls(false, target); }
    bool acceptsMidi() const noexcept { return target.algorithm != 0 ? creative.acceptsMidi() : enabled && target.mode == 2; }
    void midiEvent(const std::uint8_t* data, int bytes) noexcept
    {
        if (target.algorithm != 0) { creative.midiEvent(data, bytes); return; }
        if (!acceptsMidi() || data == nullptr || bytes != 3) return;
        const int type = data[0] & 0xf0, channel = data[0] & 15, note = data[1] & 127;
        if (type == 0x90 && data[2] != 0)
        {
            Voice* chosen = nullptr;
            for (auto& v : voices) if (v.used && v.note == note && v.channel == channel) { chosen = &v; break; }
            if (!chosen) for (auto& v : voices) if (!v.used) { chosen = &v; break; }
            if (!chosen) for (auto& v : voices) if (!v.held && (!chosen || v.age < chosen->age)) chosen = &v;
            if (!chosen) chosen = &*std::min_element(voices.begin(), voices.end(), [](const Voice& a, const Voice& b) { return a.age < b.age; });
            if (!chosen->used || chosen->note != note || chosen->channel != channel) *chosen = {};
            chosen->used = chosen->held = true; chosen->channel = channel; chosen->note = note;
            chosen->age = ++serial; chosen->ratio = ratioFor(target.pitch, note);
            chosen->velocity = std::sqrt(static_cast<double>(data[2] & 127) / 127.0);
        }
        else if (type == 0x80 || (type == 0x90 && data[2] == 0))
        {
            for (auto& v : voices) if (v.used && v.channel == channel && v.note == note) v.held = false;
        }
        else if (type == 0xb0 && (note == 120 || note == 123))
            for (auto& v : voices) if (v.used && v.channel == channel) v.held = false;
    }
    Status status() const noexcept
    {
        if (target.algorithm != 0) return creative.status();
        if (!enabled) return Status::bypassed;
        if (frozen) return target.mode == captureMode ? Status::captured : Status::releasing;
        return wantsCapture() ? Status::armed : Status::live;
    }
    void process(float& left, float& right) noexcept
    {
        if (target.algorithm != 0) { creative.process(left, right, history); return; }
        if ((!enabled && !frozen) || history[0].empty()) return;
        blend += blendCoefficient * (target.blend * 0.01 - blend);
        if (!frozen)
        {
            if (!enabled) return;
            for (auto& v : voices) if (v.used && !v.held) v = {};
            record(left, right);
            if (wantsCapture() && readyToCapture()) startCapture();
            else
            {
                if (target.mode == 0 && valid >= minimumHistory())
                {
                    liveWet = std::min(1.0, liveWet + fadeStep);
                    liveVoice.ratio = liveRatio;
                    if (blend > 0.00001)
                    {
                        double l = 0, r = 0;
                        render(liveVoice, l, r);
                        const float amount = static_cast<float>(liveWet * blend);
                        left += amount * (static_cast<float>(l) - left);
                        right = stereo ? right + amount * (static_cast<float>(r) - right) : left;
                    }
                    else { liveVoice = {}; liveWet = 0; }
                }
                else if (target.mode == 2)
                {
                    // MIDI is a gated granular instrument, not a dry-through effect.
                    const float dry = static_cast<float>(1.0 - blend);
                    left *= dry; right = stereo ? right * dry : left;
                }
                return;
            }
        }
        const bool want = enabled && target.mode == captureMode;
        wet = std::clamp(wet + (want ? fadeStep : -fadeStep), 0.0, 1.0);
        if (!want && wet <= 0.0)
        {
            frozen = false; hold = {}; liveVoice = {}; liveWet = 0;
            for (auto& v : voices) { v.grains = {}; v.until = 0; v.gain = 0; if (!v.held) v = {}; }
            if (enabled)
            {
                record(left, right);
                if (target.mode == 2) { const float dry = static_cast<float>(1.0 - blend); left *= dry; right = stereo ? right * dry : left; }
            }
            else { write = valid = 0; voices = {}; activity = 0; activeSamples = 0; signalAge = 0; soundHistory = 0; usableAudio = false; }
            return;
        }
        if (target.blend == 0.0f && blend < 0.00001)
        {
            blend = 0.0;
            for (auto& v : voices) if (!v.held) v = {};
            return;
        }
        double l = 0, r = 0, weights = 0;
        if (captureMode == 1)
        {
            render(hold, l, r); weights = 1.0;
        }
        else
        {
            for (auto& v : voices)
            {
                if (!v.used) continue;
                // MIDI retains the shared capture between notes; each voice fades independently.
                if (want) v.gain = std::clamp(v.gain + (v.held ? fadeStep : -fadeStep), 0.0, 1.0);
                if (v.gain <= 0.0 && !v.held) { v = {}; continue; }
                if (v.gain <= 0.0) continue;
                double vl = 0, vr = 0; render(v, vl, vr);
                const double g = v.gain * v.velocity;
                l += vl * g; r += vr * g; weights += v.gain;
            }
        }
        // Normalise simultaneous voices, without cancelling the individual note fades.
        const double normal = weights > 1.0 ? 1.0 / std::sqrt(weights) : 1.0;
        if (captureMode == 2 && enabled && target.mode == 2)
        {
            const float dry = static_cast<float>(1.0 - blend);
            const float amount = static_cast<float>(wet * blend);
            left = left * dry + amount * static_cast<float>(l * normal);
            right = stereo ? right * dry + amount * static_cast<float>(r * normal) : left;
        }
        else
        {
            const float amount = static_cast<float>(wet * blend);
            left += amount * (static_cast<float>(l * normal) - left);
            right = stereo ? right + amount * (static_cast<float>(r * normal) - right) : left;
        }
    }
private:
    struct Grain { double cursor = 0, step = 1; std::size_t age = 0, length = 0; };
    struct Voice
    {
        std::array<Grain, maxGrains> grains {};
        std::size_t until = 0;
        std::uint64_t age = 0;
        int note = 60, channel = -1;
        double ratio = 1, gain = 0, velocity = 1;
        bool held = false, used = false;
    };
    CreativeEngine creative;
    std::array<std::vector<float>, 2> history;
    std::array<Voice, maxVoices> voices {};
    Voice hold {}, liveVoice {};
    std::uint64_t serial = 0;
    std::uint32_t random = 0x613f129bu;
    double sr = 44100, wet = 0, blend = 1, fadeStep = .001, blendCoefficient = .001;
    double activity = 0, activityCoefficient = .001, liveWet = 0, liveRatio = 1;
    std::size_t activeSamples = 0, signalAge = 0, soundHistory = 0;
    bool usableAudio = false;
    std::size_t write = 0, valid = 0, oldest = 0;
    int captureMode = 0;
    bool stereo = true, enabled = false, frozen = false;
    Controls target {};
    static float bound(float v, float lo, float hi, float fallback) noexcept
    { return std::clamp(std::isfinite(v) ? v : fallback, lo, hi); }
    static double ratioFor(float pitch, int note) noexcept
    { return std::exp2(std::clamp(static_cast<double>(pitch) + note - 60.0, -24.0, 24.0) / 12.0); }
    bool wantsCapture() const noexcept
    {
        return enabled && (target.mode == 1 || (target.mode == 2 && std::any_of(voices.begin(), voices.end(), [](const Voice& v) { return v.held; })));
    }
    std::size_t selectedLength() const noexcept
    { return std::min(valid, static_cast<std::size_t>(std::ceil(target.capture * .001 * sr))); }
    std::size_t minimumHistory() const noexcept
    {
        return static_cast<std::size_t>(std::ceil(std::min(static_cast<double>(target.capture), 30.0) * .001 * sr)) + 3;
    }
    bool readyToCapture() const noexcept
    {
        // Wait for a useful recent signal, not merely elapsed samples of silence.
        // A short history also accommodates the upstream spectral latency.
        return usableAudio && signalAge < soundHistory && soundHistory - signalAge >= minimumHistory();
    }
    void startCapture() noexcept
    {
        // Anchor the snapshot at the last audible sample, not the silence after it.
        // The discarded trailing silence is not available to the grain player.
        valid = std::min(valid, soundHistory);
        if (signalAge > 0 && signalAge < valid)
        {
            write = (write + history[0].size() - signalAge) % history[0].size();
            valid -= signalAge;
        }
        soundHistory = valid;
        signalAge = 0;
        oldest = write >= valid ? write - valid : write + history[0].size() - valid;
        frozen = true; captureMode = target.mode; hold = {}; hold.used = true; liveVoice = {}; liveWet = 0; wet = 0;
        hold.ratio = ratioFor(target.pitch, 60); hold.gain = 1;
        for (auto& v : voices) { v.grains = {}; v.until = 0; v.gain = 0; }
    }
    double randomBipolar() noexcept
    {
        random ^= random << 13; random ^= random >> 17; random ^= random << 5;
        return static_cast<double>(random) / 2147483648.0 - 1.0;
    }
    Grain makeGrain(Voice& v) noexcept
    {
        const double speed = v.ratio;
        const auto available = selectedLength();
        const bool live = !frozen;
        // Reserve source-position room for alignment, even when SIZE exceeds the capture.
        const double reserve = std::min(.018 * sr, static_cast<double>(available) * .2);
        const double length = std::max(2.0, std::floor(std::min(target.size * .001 * sr, (static_cast<double>(available - 2) - reserve) / (speed + (live ? 1.0 : 0.0)))));
        const double base = std::max(static_cast<double>(valid - available), live ? length : 0.0);
        const double span = (length - 1.0) * speed;
        const double space = std::max(0.0, static_cast<double>(valid - 1) - base - span);
        const double pos = std::clamp(1.0 - target.position * .01 + (target.drift > 0 ? randomBipolar() * target.drift * .005 : 0.0), 0.0, 1.0);
        const double start = base + space * pos;
        Grain g { target.direction == 0 ? start : start + span, target.direction == 0 ? speed : -speed, 0, static_cast<std::size_t>(length) };
        // Align the new stream to the youngest overlapping grain. Bounded search
        // keeps dense polyphonic playback practical; drift can intentionally decorrelate it.
        const Grain* reference = nullptr;
        for (const auto& old : v.grains)
            if (old.length && old.age < old.length && std::abs(old.step - g.step) < .00001 && (!reference || old.age < reference->age)) reference = &old;
        if (!reference || target.drift >= 75.0f) return g;
        const double lo = base + (g.step < 0 ? span : 0), hi = base + space + (g.step < 0 ? span : 0);
        const double radius = .012 * sr * (1.0 - target.drift * .01), begin = std::max(lo, g.cursor - radius), end = std::min(hi, g.cursor + radius);
        const double probe = std::min(length * .15, .002 * sr) / 7.0;
        std::array<double, 8> ref {}, rightRef {};
        double leftEnergy = 0, rightEnergy = 0;
        for (std::size_t i = 0; i < ref.size(); ++i)
        {
            const double posRef = reference->cursor + probe * static_cast<double>(i) * reference->step;
            ref[i] = readAt(0, posRef); leftEnergy += ref[i] * ref[i];
            if (stereo) { rightRef[i] = readAt(1, posRef); rightEnergy += rightRef[i] * rightRef[i]; }
        }
        const std::size_t channel = rightEnergy > leftEnergy ? 1u : 0u;
        if (channel == 1u) ref = rightRef;
        const auto cost = [&](double cursor) noexcept
        {
            double error = 0, energy = 0;
            for (std::size_t i = 0; i < ref.size(); ++i)
            {
                const double a = ref[i], b = readAt(channel, cursor + probe * static_cast<double>(i) * g.step);
                error += (a-b)*(a-b); energy += a*a+b*b;
            }
            return error / (energy + 1e-12) + .0001 * std::abs(cursor - g.cursor) / radius;
        };
        const double stride = std::max(2.0, std::floor(radius / 32.0));
        double best = g.cursor, score = cost(best);
        for (double p = begin; p <= end; p += stride) if (const double c = cost(p); c < score) { best = p; score = c; }
        const double coarse = best;
        for (double p = std::max(begin, coarse-stride); p <= std::min(end, coarse+stride); p += std::max(.5, stride / 16.0)) if (const double c = cost(p); c < score) { best = p; score = c; }
        g.cursor = best; return g;
    }
    void render(Voice& v, double& l, double& r) noexcept
    {
        if (v.until == 0)
        {
            auto slot = std::find_if(v.grains.begin(), v.grains.end(), [](const Grain& g) { return g.length == 0; });
            if (slot != v.grains.end())
            {
                *slot = makeGrain(v);
                const auto density = static_cast<std::size_t>(std::lround(target.density));
                v.until = std::max<std::size_t>(1, (slot->length + density - 1) / density);
                // Start the first grain one sample into its window; the wet fade handles the attack.
                bool alone = true; for (const auto& g : v.grains) if (&g != &*slot && g.length) alone = false;
                if (alone) { slot->age = 1; slot->cursor += static_cast<double>(slot->age) * slot->step; }
            }
            else v.until = 1;
        }
        double weights = 0;
        const double smooth = target.smooth * .01;
        for (auto& g : v.grains)
        {
            if (!g.length) continue;
            const double t = static_cast<double>(g.age) / static_cast<double>(g.length);
            const double triangle = 1.0 - std::abs(2.0*t-1.0);
            // Crossfade between a short edge taper and a smooth full-grain window.
            const double edge = std::min(1.0, 10.0 * triangle);
            const double soft = triangle * triangle * (3.0 - 2.0 * triangle);
            const double w = edge + smooth * (soft - edge);
            // Stereo grains share cursor, interpolation fraction and ring indices.
            const double cursor = std::clamp(g.cursor, 0.0, static_cast<double>(valid - 1));
            const auto whole = static_cast<std::size_t>(cursor);
            const double fraction = cursor - static_cast<double>(whole);
            const auto n = history[0].size();
            auto a = oldest + whole, b = oldest + std::min(whole + 1, valid - 1);
            if (a >= n) a -= n;
            if (b >= n) b -= n;
            const auto interpolate = [&](std::size_t ch) noexcept
            { return history[ch][a] + fraction * (static_cast<double>(history[ch][b]) - history[ch][a]); };
            const double vl = interpolate(0);
            l += w * vl; r += w * (stereo ? interpolate(1) : vl); weights += w;
            g.cursor += g.step; if (++g.age >= g.length) g = {};
        }
        if (weights > 1e-9) { l /= weights; r /= weights; }
        if (v.until) --v.until;
    }
    void record(float l, float r) noexcept
    {
        const double a = std::isfinite(l) ? l : 0.0;
        const double b = std::isfinite(r) ? r : 0.0;
        activity += activityCoefficient * (std::max(a*a, b*b) - activity);
        if (activity > 1e-6) activeSamples = std::min(activeSamples + 1, static_cast<std::size_t>(sr));
        else activeSamples = 0;
        if (std::max(std::abs(a), std::abs(b)) > .002 && activity > 1e-6)
        {
            if (signalAge >= history[0].size()) soundHistory = 0;
            signalAge = 0;
            soundHistory = std::min(soundHistory + 1, history[0].size());
        }
        else
        {
            signalAge = std::min(signalAge + 1, history[0].size());
            if (soundHistory) soundHistory = std::min(soundHistory + 1, history[0].size());
        }
        // Shorter MIDI acquisition keeps note attacks responsive. Live also builds
        // usable history for subsequent Hold/MIDI without needing a new onset.
        const auto required = static_cast<std::size_t>(sr * (target.mode == 2 ? .035 : .075));
        if (activeSamples >= required && signalAge < static_cast<std::size_t>(sr * .025)) usableAudio = true;
        if (signalAge >= history[0].size()) usableAudio = false;
        // Live grains use coordinates relative to the oldest retained sample.
        if (valid == history[0].size())
            for (auto& g : liveVoice.grains) if (g.length) g.cursor -= 1.0;
        history[0][write] = static_cast<float>(a);
        if (stereo) history[1][write] = std::isfinite(r) ? r : 0.0f;
        if (++write == history[0].size()) write = 0;
        valid = std::min(valid + 1, history[0].size());
        oldest = write >= valid ? write - valid : write + history[0].size() - valid;
    }
    double readAt(std::size_t channel, double position) const noexcept
    {
        const double cursor = std::clamp(position, 0.0, static_cast<double>(valid - 1));
        const auto whole = static_cast<std::size_t>(cursor);
        const double fraction = cursor - static_cast<double>(whole);
        const auto n = history[0].size();
        auto a = oldest + whole, b = oldest + std::min(whole + 1, valid - 1);
        if (a >= n) a -= n;
        if (b >= n) b -= n;
        return history[channel][a] + fraction * (static_cast<double>(history[channel][b]) - history[channel][a]);
    }
};
}
