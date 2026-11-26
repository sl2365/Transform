#pragma once
#include "TransformPitch.h"
#include "TransformTune.h"
#include <cstdint>

namespace transform::pitch
{
inline constexpr int chordsMode = 0, vocalMode = 1, granularMode = 2, bassMode = 3, beatsMode = 4, texturesMode = 5, alternatorMode = 6;
inline constexpr std::array<const char*, 7> algorithmNames {{ "Chords", "Vocal", "Granular", "Bass", "Beats", "Textures", "Alternator" }};
inline constexpr int lastAlgorithm = static_cast<int>(algorithmNames.size()) - 1;
// Time-domain modes share a preallocated, latency-aligned history. Vocal
// aligns to pitch epochs; Granular uses waveform-matched grains. Textures
// uses longer, more densely overlapped grains. Bass uses
// a low-range detector and longer period-adaptive waveform matching. Beats
// uses short grains and an aligned transient-preservation envelope.
class TimePitch
{
public:
    void prepare(double sampleRate, int channels, int delay)
    {
        sr = std::clamp(std::isfinite(sampleRate) ? sampleRate : 44100.0, 8000.0, 384000.0);
        stereo = channels > 1; latency = delay; size = 8 * delay;
        for (auto& c : states) c.history.assign(static_cast<std::size_t>(size), 0.0f);
        // A small polyphase low-pass table avoids most upward-resampling
        // aliasing without filtering the history or allocating during playback.
        for (int b = 1; b <= filterCount; ++b)
            for (int ph = 0; ph < phases; ++ph)
            {
                const double cutoff = 0.47 / std::exp2(static_cast<double>(b) / 6.0);
                double sum = 0.0;
                auto& coeff = filters[static_cast<std::size_t>(b - 1)][static_cast<std::size_t>(ph)];
                for (int j = 0; j < taps; ++j)
                {
                    const double x = j - (taps / 2 - 1) - static_cast<double>(ph) / phases;
                    const double w = 0.5 + 0.5 * std::cos(pi * x / (taps / 2));
                    const double h = std::abs(x) < 1.0e-8 ? 2.0 * cutoff : std::sin(2.0 * pi * cutoff * x) / (pi * x);
                    coeff[static_cast<std::size_t>(j)] = static_cast<float>(w * h); sum += w * h;
                }
                for (auto& h : coeff) h = static_cast<float>(h / sum);
            }
        detector.prepare(sr);
        attackCoeff = coefficient(0.5f, 1.0);
        releaseCoeff = coefficient(25.0f, 1.0);
        transientDecay = coefficient(5.0f, 1.0);
        reset();
    }
    void reset() noexcept
    {
        if (mode == bassMode) detector.prepare(sr);
        time = 0; mode = 0; wasShifted = false; manual = target = correction = correctionTarget = detune = 0.0f;
        energyL = energyR = 0.0f;
        clearTransient();
        glide = speed = -1.0f; glideCoefficient = correctionCoefficient = 1.0f;
        detector.reset();
        for (auto& c : states) { std::fill(c.history.begin(), c.history.end(), 0.0f); clear(c); }
    }
    void setControls(int algorithm, float shift, float fine, const Enhancements& extra) noexcept
    {
        algorithm = std::clamp(algorithm, 0, lastAlgorithm);
        if (algorithm != mode)
        {
            const bool changeRange = (mode == bassMode) != (algorithm == bassMode);
            mode = algorithm;
            if (changeRange) detector.prepare(sr, mode == bassMode);
            else detector.reset();
            energyL = energyR = 0.0f;
            clearTransient();
            for (auto& c : states) clear(c);
        }
        target = bound(shift, -24, 24) + 0.01f * bound(fine, -100, 100);
        detune = stereo ? bound(extra.detune, 0, 100) * 0.005f : 0.0f;
        transientStrength = 0.65f + 0.35f * bound(extra.transient, 0, 100) * 0.01f;
        const float ms = bound(extra.glide, 0, 500);
        if (ms != glide) { glide = ms; glideCoefficient = coefficient(ms, 4.605170186); }
    }
    void setCorrection(float st, float ms) noexcept
    {
        correctionTarget = bound(st, -24, 24);
        ms = bound(ms, 0, 200);
        if (ms != speed) { speed = ms; correctionCoefficient = coefficient(ms, 1.0); }
    }
    void process(float& l, float& r) noexcept
    {
        if (size == 0) return;
        const float inputL = std::isfinite(l) ? l : 0.0f, inputR = std::isfinite(r) ? r : 0.0f;
        states[0].history[slot(time)] = inputL;
        if (stereo) states[1].history[slot(time)] = inputR;
        if (mode != 0)
        {
            manual += glideCoefficient * (target - manual);
            correction += (correctionTarget == 0.0f ? 1.0f : correctionCoefficient) * (correctionTarget - correction);
            if (std::abs(manual - target) < 0.0001f) manual = target;
            if (std::abs(correction - correctionTarget) < 0.0001f) correction = correctionTarget;
            const bool shifted = std::abs(manual + correction) > 0.0001f || detune > 0.0f;
            const bool tracksPitch = mode == vocalMode || mode == bassMode || mode == alternatorMode;
            if (tracksPitch && shifted)
            {
                detector.process(&inputL, stereo ? &inputR : nullptr, 1);
                energyL += 0.001f * (std::abs(inputL) - energyL);
                energyR += 0.001f * (std::abs(inputR) - energyR);
            }
            else if (tracksPitch && !shifted && wasShifted) detector.reset();
            wasShifted = shifted;
            const float hz = tracksPitch ? detector.getFrequency() : 0.0f;
            if (mode == beatsMode && shifted) updateTransient();
            else if (mode == beatsMode && !shifted) clearTransient();
            l = channel(states[0], manual + correction - detune, shifted, hz);
            r = stereo ? channel(states[1], manual + correction + detune, shifted, hz) : l;
        }
        else { manual = target; correction = correctionTarget; wasShifted = false; }
        ++time;
    }
private:
    static constexpr int taps = 24, phases = 32, filterCount = 26;
    static constexpr double pi = 3.14159265358979323846;
    using Coefficients = std::array<float, taps>;
    std::array<std::array<Coefficients, phases>, filterCount> filters {};
    struct Grain { double read = 0.0, rate = 1.0; int age = 0, length = 0, filter = 0; };
    struct Channel
    {
        std::vector<float> history;
        std::array<Grain, 8> grains {};
        double countdown = 0.0;
        float mix = 0.0f;
        int strategy = 0;
        bool alternateDown = false;
        float lastShift = 1000.0f;
        double ratio = 1.0;
    };
    std::array<Channel, 2> states;
    tune::Detector detector;
    double sr = 48000.0;
    std::int64_t time = 0;
    int latency = 0, size = 0, mode = 0;
    bool stereo = false, wasShifted = false;
    float manual = 0, target = 0, correction = 0, correctionTarget = 0, detune = 0;
    float energyL = 0, energyR = 0;
    float fastEnvelope = 0, slowEnvelope = 0, transientMix = 0, transientStrength = 0.65f;
    float attackCoeff = 1, releaseCoeff = 1, transientDecay = 1;
    int transientLockout = 0, transientHold = 0;
    bool transientHit = false;
    void clearTransient() noexcept { fastEnvelope = slowEnvelope = transientMix = 0; transientLockout = transientHold = 0; transientHit = false; }
    void updateTransient() noexcept
    {
        // Both channels share the detector to preserve stereo attacks. Use
        // the latency-aligned input, not the earlier undelayed transient.
        const double at = static_cast<double>(time - latency);
        const float x = std::max(std::abs(read(states[0], at)), stereo ? std::abs(read(states[1], at)) : 0.0f);
        fastEnvelope += attackCoeff * (x - fastEnvelope);
        slowEnvelope += releaseCoeff * (x - slowEnvelope);
        if (transientLockout > 0) --transientLockout;
        transientHit = transientLockout == 0 && fastEnvelope > 0.008f && fastEnvelope > 3.0f * slowEnvelope;
        if (transientHit)
        {
            transientLockout = static_cast<int>(0.025 * sr);
            transientHold = static_cast<int>(0.003 * sr);
        }
        // Preserve a short portion of the original attack, then return to
        // fully pitched grains. Smooth onset/decay to avoid clicks.
        const float goal = transientHold > 0 ? transientStrength : 0.0f;
        if (transientHold > 0) --transientHold;
        transientMix += (goal > transientMix ? attackCoeff : transientDecay) * (goal - transientMix);
    }
    float glide = -1, speed = -1, glideCoefficient = 1, correctionCoefficient = 1;
    static float bound(float x, float lo, float hi) noexcept { return std::clamp(std::isfinite(x) ? x : 0.0f, lo, hi); }
    float coefficient(float ms, double constant) const noexcept { return ms == 0.0f ? 1.0f : static_cast<float>(-std::expm1(-constant * 1000.0 / (ms * sr))); }
    static void clear(Channel& c) noexcept { c.grains = {}; c.countdown = 0; c.mix = 0; c.strategy = 0; c.alternateDown = false; c.lastShift = 1000.0f; c.ratio = 1.0; }
    std::size_t slot(std::int64_t i) const noexcept { return static_cast<std::size_t>(static_cast<std::uint64_t>(i) & static_cast<std::uint64_t>(size - 1)); }
    float read(const Channel& c, double at) const noexcept
    {
        const auto i = static_cast<std::int64_t>(std::floor(at));
        const float f = static_cast<float>(at - static_cast<double>(i));
        const auto get = [&](std::int64_t p) { return p < 0 || p > time || p <= time - size ? 0.0f : c.history[slot(p)]; };
        const float a = get(i), b = get(i + 1); return a + f * (b - a);
    }
    float grainRead(const Channel& c, const Grain& g) const noexcept
    {
        if (g.filter == 0) return read(c, g.read);
        const auto i = static_cast<std::int64_t>(std::floor(g.read));
        const int phase = std::clamp(static_cast<int>((g.read - static_cast<double>(i)) * phases), 0, phases - 1);
        const auto& coeff = filters[static_cast<std::size_t>(g.filter - 1)][static_cast<std::size_t>(phase)];
        const auto first = i - (taps / 2 - 1);
        float value = 0.0f;
        if (first >= 0 && first > time - size && first + taps - 1 <= time)
        {
            for (int j = 0; j < taps; ++j) value += coeff[static_cast<std::size_t>(j)] * c.history[slot(first + j)];
        }
        else
        {
            for (int j = 0; j < taps; ++j)
            {
                const auto at = first + j;
                if (at >= 0 && at > time - size && at <= time) value += coeff[static_cast<std::size_t>(j)] * c.history[slot(at)];
            }
        }
        return value;
    }
    // Match the phase of the newest existing grain over a short overlap.
    // Bounded search, independent of sample-rate, with local refinement.
    double match(const Channel& c, double nominal, double rate, int radius) const noexcept
    {
        const Grain* old = nullptr;
        for (const auto& g : c.grains) if (g.length > g.age && (!old || g.age < old->age)) old = &g;
        if (!old) return nominal;
        const int stride = std::max(1, radius / 32), probe = std::max(1, std::min(old->length - old->age - 1, radius) / 12);
        double best = nominal, scoreBest = -2.0;
        const auto score = [&](double p)
        {
            double xy = 0, xx = 0, yy = 0;
            for (int i = 0; i < 12; ++i)
            {
                const double a = read(c, old->read + i * probe * old->rate), b = read(c, p + i * probe * rate);
                xy += a*b; xx += a*a; yy += b*b;
            }
            const double value = xy / std::sqrt(xx*yy + 1.0e-20) - 0.0005 * std::abs(p - nominal) / std::max(1, radius);
            if (value > scoreBest) { scoreBest = value; best = p; }
        };
        for (int i = -radius; i <= radius; i += stride) score(nominal + i);
        const double coarse = best;
        for (int i = -stride; i <= stride; ++i) score(coarse + i);
        return best;
    }
    // Find a repeatable waveform peak near the desired input epoch.
    double epoch(const Channel& c, double centre, double period) const noexcept
    {
        const int radius = std::max(1, static_cast<int>(std::ceil(period * 0.55)));
        const int stride = std::max(1, radius / 64);
        double best = centre; float peak = -1.0e30f;
        const auto inspect = [&](double p) { const float x = read(c, p); if (x > peak) { peak = x; best = p; } };
        for (int i = -radius; i <= radius; i += stride) inspect(centre + i);
        const double coarse = best;
        for (int i = -stride; i <= stride; ++i) inspect(coarse + i);
        const float a = read(c, best-1), b = read(c, best), d = read(c, best+1);
        const float denom = a - 2*b + d;
        return best + (denom < -1.0e-8f ? std::clamp(0.5f*(a-d)/denom, -0.5f, 0.5f) : 0.0f);
    }
    float channel(Channel& c, float shift, bool shifted, float hz) noexcept
    {
        const float dry = read(c, static_cast<double>(time - latency));
        if (shift != c.lastShift) { c.lastShift = shift; c.ratio = std::exp2(static_cast<double>(shift) / 12.0); }
        const double ratio = c.ratio;
        const double period = hz > 0.0f ? sr / hz : 0.0;
        const double spacing = period / ratio;
        const double vocalRadius = spacing;
        const bool vocal = mode == vocalMode && hz > 0.0f && spacing >= 8.0
            && vocalRadius * (1.0 + ratio) + period * 0.6 < latency * 0.95;
        const int strategy = !shifted || (mode == vocalMode && hz <= 0.0f) ? 0 : vocal ? 1 : 2;
        if (strategy != c.strategy)
        {
            c.countdown = 0.0; c.strategy = strategy;
            // Outgoing grains finish naturally, avoiding a discontinuous reset.
        }
        if (mode == beatsMode && transientHit) c.countdown = 0.0;
        if (strategy != 0 && c.countdown <= 0.0)
        {
            auto* grain = &c.grains[0];
            for (auto& g : c.grains) if (g.length <= g.age) { grain = &g; break; } else if (g.age > grain->age) grain = &g;
            int rad = vocal ? std::max(4, static_cast<int>(std::lround(vocalRadius)))
                : std::max(8, std::min(latency / 4, static_cast<int>(latency * 0.65 / (1.0 + ratio))));
            if (mode == bassMode)
            {
                // Longer, pitch-adaptive grains with enough history for a
                // half-period alignment search, even for deep bass notes.
                const double wanted = period > 0.0 ? period : sr * 0.022;
                rad = std::max(8, static_cast<int>(std::min(wanted, latency * 0.88 / (1.0 + ratio))));
            }
            else if (mode == beatsMode)
                rad = std::max(8, std::min(rad, static_cast<int>(sr * 0.004)));
            else if (mode == texturesMode)
                rad = std::max(8, static_cast<int>(latency * 0.82 / (1.0 + ratio)));
            // Alternate the manual interval on consecutive pitch-aligned grains.
            // The tuning correction remains common to both directions.
            double rate = ratio;
            if (mode == alternatorMode)
            {
                const double centreRatio = std::exp2(static_cast<double>(correction) / 12.0);
                rate = c.alternateDown ? centreRatio * centreRatio / ratio : ratio;
                c.alternateDown = !c.alternateDown;
                const double maxRate = std::max(rate, 1.0 / rate);
                const double wantedRadius = period > 0.0 ? period : sr * 0.012;
                rad = std::max(8, static_cast<int>(std::min(wantedRadius, latency * 0.65 / (1.0 + maxRate))));
            }
            const double centre = static_cast<double>(time - latency + rad);
            double start = centre - rad * rate;
            if (vocal) start = epoch(states[stereo && energyR > energyL * 1.1f ? 1 : 0], centre, period) - rad * rate;
            else if (mode == bassMode)
            {
                const int safeRadius = std::max(1, static_cast<int>(latency - rad * std::abs(1.0 - ratio) - taps));
                const int radius = std::max(1, std::min(safeRadius, static_cast<int>(period > 0.0 ? period * 0.55 : rad)));
                start = match(c, start, rate, radius);
            }
            else if (mode == alternatorMode && period > 0.0)
                start = epoch(states[stereo && energyR > energyL * 1.1f ? 1 : 0], centre, period) - rad * rate;
            else if (mode == beatsMode && transientHit) start = static_cast<double>(time - latency);
            else start = match(c, start, rate, std::max(1, std::min(latency / 8, rad)));
            const int filter = rate > 1.01 ? std::clamp(static_cast<int>(std::ceil(std::log2(rate) * 6.0)), 1, filterCount) : 0;
            *grain = { start, rate, 0, 2 * rad, filter };
            c.countdown += vocal ? spacing : mode == texturesMode ? rad * 0.5 : rad;
        }
        if (strategy != 0) c.countdown -= 1.0;
        float sum = 0.0f, weight = 0.0f;
        for (auto& g : c.grains) if (g.age < g.length)
        {
            // Smooth parabolic overlap window; normalize by total weight.
            const float x = static_cast<float>(g.age) / static_cast<float>(g.length);
            const float parabola = 4.0f*x*(1.0f-x);
            const float w = (mode == texturesMode || mode == alternatorMode) ? parabola * parabola : parabola;
            sum += w * grainRead(c, g); weight += w;
            g.read += g.rate; ++g.age;
        }
        const float aim = strategy != 0 && weight > 0.0001f ? 1.0f : 0.0f;
        const float step = static_cast<float>(1.0 / (0.010 * sr));
        c.mix += std::clamp(aim - c.mix, -step, step);
        const float wet = weight > 0.0001f ? sum / weight : dry;
        const float processed = dry + (wet - dry) * c.mix;
        return mode == beatsMode && shifted ? processed + (dry - processed) * transientMix : processed;
    }
};

// Only the selected processing strategy runs. Spectral's delay history stays
// warm to supply global dry and for switching back to spectral processing.
class Selectable
{
public:
    void prepare(double sr, int channels) { spectral.prepare(sr, channels); alternate.prepare(sr, channels, spectral.latency()); reset(); }
    int latency() const noexcept { return spectral.latency(); }
    void reset() noexcept { spectral.reset(); alternate.reset(); selected = mode = 0; shift = fine = correction = 0; speed = 40; extra = {}; }
    void setAlgorithm(int a) noexcept { selected = std::clamp(a, 0, lastAlgorithm); }
    void setControls(bool enabled, float st, float ct, float formant) noexcept
    {
        mode = enabled ? selected : 0;
        shift = st; fine = ct;
        spectral.setControls(enabled && mode == 0, st, ct, formant);
        spectral.setCorrection(mode == 0 ? correction : 0.0f, speed);
        alternate.setControls(mode, st, ct, extra);
        alternate.setCorrection(mode != 0 ? correction : 0.0f, speed);
    }
    void setEnhancements(bool enabled, const Enhancements& x) noexcept
    {
        extra = enabled ? x : Enhancements{};
        spectral.setEnhancements(enabled && mode == 0, extra);
        alternate.setControls(mode, shift, fine, extra);
    }
    void setCorrection(float st, float ms) noexcept
    {
        correction = st; speed = ms;
        spectral.setCorrection(mode == 0 ? st : 0.0f, ms);
        alternate.setCorrection(mode != 0 ? st : 0.0f, ms);
    }
    void process(float& l, float& r, float& dl, float& dr, AnalysisCache* cache = nullptr) noexcept
    {
        float al = l, ar = r;
        spectral.process(l, r, dl, dr, cache);
        alternate.process(al, ar);
        if (mode != 0) { l = al; r = ar; }
    }
private:
    Engine spectral;
    TimePitch alternate;
    Enhancements extra {};
    int selected = 0, mode = 0;
    float shift = 0, fine = 0, correction = 0, speed = 40;
};
}
