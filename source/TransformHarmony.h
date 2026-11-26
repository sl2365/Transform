#pragma once

#include "TransformPitch.h"
#include "TransformTune.h"
#include <vector>
#include <array>
#include <cstddef>

namespace transform::harmony
{
inline constexpr std::size_t voiceCount = 4;
inline constexpr std::size_t controlsPerVoice = 6;
struct VoiceInfo
{
    const char* intervalId;
    const char* panId;
    const char* levelId;
    float defaultInterval;
    const char* formantId;
    const char* humanId;
    const char* delayId;
};
inline constexpr std::array<VoiceInfo, voiceCount> info {{
    { "harmony1Interval", "harmony1Pan", "harmony1Level", 7.0f, "harmony1Formant", "harmony1Human", "harmony1Delay" },
    { "harmony2Interval", "harmony2Pan", "harmony2Level", 12.0f, "harmony2Formant", "harmony2Human", "harmony2Delay" },
    { "harmony3Interval", "harmony3Pan", "harmony3Level", -5.0f, "harmony3Formant", "harmony3Human", "harmony3Delay" },
    { "harmony4Interval", "harmony4Pan", "harmony4Level", -12.0f, "harmony4Formant", "harmony4Human", "harmony4Delay" }
}};
inline constexpr std::array<const char*, voiceCount> powerIds {{ "harmony1Enabled", "harmony2Enabled", "harmony3Enabled", "harmony4Enabled" }};
inline constexpr std::array<const char*, 3> modeNames {{ "Fixed", "Scale", "MIDI" }};
struct Controls { float interval = 0.0f, pan = 0.0f, level = 0.0f, formant = 0.0f, human = 0.0f, delay = 0.0f; };
// Map a desired chromatic interval to the nearest allowed note, or assign
// ascending held MIDI notes to voice slots. No detected note means no harmony
// in these modes. Fixed mode does not require pitch detection.
inline bool resolveInterval(int mode, std::size_t voice, float frequency, float reference,
                            int key, int scale, unsigned mask, const tune::HeldNotes& held,
                            float leadShift, float correction, float& interval) noexcept
{
    if (mode == 0) return true;
    if (!std::isfinite(frequency) || frequency < tune::Detector::minimumHz || frequency > tune::Detector::maximumHz) return false;
    const double lead = 69.0 + 12.0 * std::log2(static_cast<double>(frequency) / std::clamp(static_cast<double>(reference), 400.0, 480.0)) + leadShift + correction;
    int target = -1;
    if (mode == 2)
    {
        std::size_t slot = 0;
        for (int n = 0; n < 128; ++n)
            if (held.contains(n) && slot++ == voice) { target = n; break; }
    }
    else
    {
        const auto allowed = scale == tune::customScale ? mask : (scale == tune::midiScale ? 0xfffu : tune::scaleMasks[static_cast<std::size_t>(std::clamp(scale, 0, 8))]);
        const int root = scale == tune::customScale ? 0 : std::clamp(key, 0, 11);
        const double wanted = lead + interval;
        double distance = 1.0e9;
        for (int n = std::max(0, static_cast<int>(std::floor(wanted)) - 12); n <= std::min(127, static_cast<int>(std::ceil(wanted)) + 12); ++n)
            if ((allowed & (1u << ((n - root + 12) % 12))) != 0 && std::abs(n - wanted) < distance)
            { target = n; distance = std::abs(n - wanted); }
    }
    if (target < 0) return false;
    const double shift = target - lead;
    if (std::abs(shift) > 24.0) return false;
    interval = static_cast<float>(shift);
    return true;
}
using VoiceControls = std::array<Controls, voiceCount>;

// Four parallel engines process the pre-PITCH input with the same latency as
// the lead. Each voice follows the lead's pitch, correction and formants.
// Engines are prepared once; only audible or fading voices run.
class Engine
{
public:
    void prepare(double sr, int channels)
    {
        stereo = channels > 1;
        const double rate = std::isfinite(sr) ? std::clamp(sr, 8000.0, 384000.0) : 44100.0;
        slewStep = static_cast<float>(1.0 / (0.020 * rate));
        sampleRate = rate;
        for (auto& v : voices)
        {
            v.pitch.prepare(sr, channels);
            for (auto& h : v.history) h.assign(static_cast<std::size_t>(std::ceil(rate * 0.12)) + 3, 0.0f);
        }
        reset();
    }
    void reset() noexcept
    {
        correction = 0.0f;
        speed = 40.0f;
        for (auto& v : voices)
        {
            v.pitch.reset();
            v.active = false;
            v.gain = v.targetGain = v.pan = v.targetPan = 0.0f;
            v.warmup = 0;
            v.write = v.valid = 0;
            v.delay = v.targetDelay = 0.0f;
        }
    }
    void setControls(bool enabled, const VoiceControls& values,
                     float leadShift, float leadFormant) noexcept
    {
        for (std::size_t i = 0; i < voiceCount; ++i)
        {
            auto& v = voices[i];
            const auto& c = values[i];
            v.targetGain = enabled ? bounded(c.level, 0.0f, 100.0f) * 0.01f : 0.0f;
            if (v.targetGain <= 0.0f) continue;
            v.targetPan = bounded(c.pan, -100.0f, 100.0f) * 0.01f;
            constexpr std::array<float, voiceCount> humanPitch {{ -0.14f, 0.11f, -0.07f, 0.18f }};
            constexpr std::array<float, voiceCount> humanTime {{ 4.0f, 9.0f, 13.0f, 6.0f }};
            const float human = bounded(c.human, 0.0f, 100.0f) * 0.01f;
            v.targetDelay = (bounded(c.delay, 0.0f, 100.0f) + human * humanTime[i]) * static_cast<float>(sampleRate * 0.001);
            if (!v.active)
            {
                v.pitch.reset();
                v.warmup = v.pitch.latency();
                v.gain = 0.0f;
                v.pan = v.targetPan;
                v.write = v.valid = 0;
                v.delay = v.targetDelay;
                v.active = true;
            }
            v.pitch.setHarmonyControls(leadShift + bounded(c.interval, -24.0f, 24.0f) + human * humanPitch[i],
                                       leadFormant + bounded(c.formant, -24.0f, 24.0f));
            v.pitch.setCorrection(correction, speed);
        }
    }
    void setCorrection(float shift, float speedMs) noexcept
    {
        correction = shift;
        speed = speedMs;
        for (auto& v : voices)
            if (v.active && v.targetGain > 0.0f) v.pitch.setCorrection(shift, speedMs);
    }
    void disable() noexcept
    {
        for (auto& v : voices) v.targetGain = 0.0f;
    }
    void process(float inputL, float inputR, float& leadL, float& leadR, pitch::AnalysisCache* cache = nullptr) noexcept
    {
        for (auto& v : voices)
        {
            if (!v.active) continue;
            if (v.targetGain == 0.0f && v.gain == 0.0f)
            {
                v.active = false;
                continue;
            }
            float l = inputL, r = inputR, unusedL = 0.0f, unusedR = 0.0f;
            v.pitch.process(l, r, unusedL, unusedR, cache);
            if (v.warmup > 0) { --v.warmup; continue; }
            // Delay only the added voice, never the lead. Ring validity avoids
            // clearing large buffers on the audio thread when voices restart.
            v.delay = approach(v.delay, v.targetDelay, 0.5f);
            const auto size = v.history[0].size();
            if (size > 0)
            {
                v.history[0][v.write] = l;
                v.history[1][v.write] = r;
                v.valid = std::min(v.valid + 1, size);
                const auto whole = static_cast<std::size_t>(v.delay);
                const float fraction = v.delay - static_cast<float>(whole);
                const auto read = [&](std::size_t ch, std::size_t age) noexcept
                { return age < v.valid ? v.history[ch][v.write >= age ? v.write - age : v.write + size - age] : 0.0f; };
                if (v.delay > 0.0f)
                {
                    const float a = read(0, whole), b = read(1, whole);
                    l = a + fraction * (read(0, whole + 1) - a);
                    r = b + fraction * (read(1, whole + 1) - b);
                }
                if (++v.write == size) v.write = 0;
            }
            v.gain = approach(v.gain, v.targetGain, slewStep);
            v.pan = approach(v.pan, v.targetPan, 2.0f * slewStep);
            if (v.gain == 0.0f) continue;
            if (stereo)
            {
                // Balance law: unity on both channels at centre; no boost at extremes.
                leadL += l * v.gain * (1.0f - std::max(0.0f, v.pan));
                leadR += r * v.gain * (1.0f + std::min(0.0f, v.pan));
            }
            else
            {
                leadL += l * v.gain;
                leadR = leadL;
            }
        }
    }
private:
    struct Voice
    {
        pitch::Engine pitch;
        float gain = 0.0f, targetGain = 0.0f, pan = 0.0f, targetPan = 0.0f;
        int warmup = 0;
        std::array<std::vector<float>, 2> history;
        std::size_t write = 0, valid = 0;
        float delay = 0.0f, targetDelay = 0.0f;
        bool active = false;
    };
    std::array<Voice, voiceCount> voices;
    float correction = 0.0f, speed = 40.0f, slewStep = 0.001f;
    bool stereo = true;
    double sampleRate = 44100.0;
    static float bounded(float x, float lo, float hi) noexcept
    {
        return std::clamp(std::isfinite(x) ? x : 0.0f, lo, hi);
    }
    static float approach(float x, float target, float step) noexcept
    {
        return x + std::clamp(target - x, -step, step);
    }
};
}
