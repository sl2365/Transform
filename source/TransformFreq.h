#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>
#include "TransformTune.h"

namespace transform::freq
{
inline constexpr std::array<const char*, 4> modeNames {{ "Upper", "Lower", "Ring", "Stereo" }};
inline constexpr std::array<const char*, 9> syncNames {{ "Free", "1/4", "1/8 D", "1/8", "1/8 T", "1/16 D", "1/16", "1/16 T", "1/32" }};
inline constexpr std::array<double, 9> syncBeats {{ 0.0, 1.0, 0.75, 0.5, 1.0 / 3.0, 0.375, 0.25, 1.0 / 6.0, 0.125 }};
inline double delayMs(float freeMs, int sync, double bpm) noexcept
{
    if (sync <= 0) return std::clamp(static_cast<double>(std::isfinite(freeMs) ? freeMs : 20.0f), 1.0, 100.0);
    bpm = std::clamp(std::isfinite(bpm) ? bpm : 120.0, 10.0, 1000.0);
    return 60000.0 * syncBeats[static_cast<std::size_t>(std::clamp(sync, 1, 8))] / bpm;
}
struct Controls
{
    float shift = 0.0f, offset = 0.0f, feedback = 0.0f, delay = 20.0f, blend = 100.0f;
    int mode = 0;
    float modRate = 1.0f, modDepth = 0.0f, phase = 0.0f, cutoff = 12000.0f;
    int sync = 0;
    double bpm = 120.0;
    bool relative = false;
    float relativePercent = 0.0f;
};

// Six second-order allpass sections per quadrature branch. The coefficients
// were fitted to a 90-degree phase difference over 25 Hz at 192 kHz through
// one quarter of the sample rate (with symmetric response above that point).
// The paths have unity magnitude individually; no FFT or extra fixed delay.
class Engine
{
public:
    void prepare(double rate, int channels)
    {
        sr = std::isfinite(rate) ? std::clamp(rate, 8000.0, 384000.0) : 44100.0;
        stereo = channels > 1;
        smoothing = 1.0 - std::exp(-1.0 / (0.008 * sr));
        fadeStep = 1.0 / (0.020 * sr);
        for (auto& ch : state)
            ch.history.assign(static_cast<std::size_t>(std::ceil(sr * 6.0)) + 3, 0.0f);
        pitchDetector.prepare(sr);
        reset();
    }
    void reset() noexcept
    {
        for (auto& ch : state) ch.clear();
        running = false;
        pitchDetector.reset();
        tracking = false;
        wet = 0.0;
        target = {};
        current = {};
        modeWeights = { 1.0, 0.0, 0.0, 0.0 };
        wanted = false;
        lfoPhase = 0.0; filterCoefficient = targetFilterCoefficient = 1.0; cachedCutoff = -1.0f;
    }
    void setControls(bool enabled, Controls values) noexcept
    {
        target.shift = bound(values.shift, -2000.0f, 2000.0f, 0.0f);
        target.relative = values.relative;
        target.relativePercent = bound(values.relativePercent, -200.0f, 200.0f, 0.0f);
        if (target.relative) target.shift = 0.0f;
        target.offset = stereo ? bound(values.offset, -1000.0f, 1000.0f, 0.0f) : 0.0f;
        target.feedback = bound(values.feedback, 0.0f, 95.0f, 0.0f);
        target.delay = static_cast<float>(delayMs(values.delay, values.sync, values.bpm));
        target.modRate = bound(values.modRate, 0.05f, 20.0f, 1.0f);
        target.modDepth = bound(values.modDepth, 0.0f, 1000.0f, 0.0f);
        target.phase = stereo ? bound(values.phase, -180.0f, 180.0f, 0.0f) : 0.0f;
        target.cutoff = bound(values.cutoff, 100.0f, 20000.0f, 12000.0f);
        if (target.cutoff != cachedCutoff)
        {
            cachedCutoff = target.cutoff;
            targetFilterCoefficient = 1.0 - std::exp(-6.28318530717958647692 * std::min(static_cast<double>(cachedCutoff), sr * 0.45) / sr);
        }
        target.blend = bound(values.blend, 0.0f, 100.0f, 100.0f);
        target.mode = std::clamp(values.mode, 0, 3);
        wanted = enabled && target.blend > 0.0f && (target.shift != 0.0f || (target.relative && target.relativePercent != 0.0f) || target.offset != 0.0f || target.feedback != 0.0f || target.modDepth != 0.0f || target.phase != 0.0f);
        if (wanted && !running && !state[0].history.empty())
        {
            current = target;
            filterCoefficient = targetFilterCoefficient;
            lfoPhase = 0.0;
            modeWeights = { 0.0, 0.0, 0.0, 0.0 };
            modeWeights[static_cast<std::size_t>(target.mode)] = 1.0;
            running = true;
        }
    }
    void disable() noexcept { wanted = false; }
    bool isRunning() const noexcept { return running; }
    void process(float& left, float& right) noexcept
    {
        if (!running) return;
        wet = std::clamp(wet + (wanted ? fadeStep : -fadeStep), 0.0, 1.0);
        if (!wanted && wet <= 0.0)
        {
            for (auto& ch : state) ch.clear();
            running = false;
            pitchDetector.reset();
            tracking = false;
            return;
        }
        // Hold the last effect settings while fading out, avoiding a sweep to
        // zero during bypass. All continuous changes while active are smoothed.
        if (wanted)
        {
            const bool track = target.relative && target.relativePercent != 0.0f;
            if (track)
            {
                if (!tracking) pitchDetector.reset();
                pitchDetector.process(&left, stereo ? &right : nullptr, 1);
            }
            tracking = track;
            const float effectiveShift = track ? std::clamp(pitchDetector.getFrequency() * target.relativePercent * 0.01f,
                -3200.0f, 3200.0f) : target.shift;
            smooth(current.shift, effectiveShift);
            smooth(current.offset, target.offset);
            smooth(current.feedback, target.feedback);
            smooth(current.delay, target.delay);
            smooth(current.blend, target.blend);
            smooth(current.modRate, target.modRate);
            smooth(current.modDepth, target.modDepth);
            smooth(current.phase, target.phase);
            filterCoefficient += smoothing * (targetFilterCoefficient - filterCoefficient);
            for (std::size_t i = 0; i < modeWeights.size(); ++i)
            {
                const double goal = i == static_cast<std::size_t>(target.mode) ? 1.0 : 0.0;
                modeWeights[i] += smoothing * (goal - modeWeights[i]);
                if (std::abs(goal - modeWeights[i]) < 1.0e-9) modeWeights[i] = goal;
            }
        }
        const double amount = wet * current.blend * 0.01;
        const double feedback = current.feedback * 0.01;
        const double delaySamples = std::clamp(current.delay * sr * 0.001, 1.0, sr * 6.0);
        double modulation = 0.0;
        if (current.modDepth > 0.0f)
        {
            modulation = current.modDepth * std::sin(lfoPhase);
            lfoPhase += 6.28318530717958647692 * current.modRate / sr;
            if (lfoPhase >= 6.28318530717958647692) lfoPhase -= 6.28318530717958647692;
        }
        const double shift = current.shift + modulation;
        left = processChannel(state[0], left, shift - 0.5 * current.offset, feedback, delaySamples, amount, 0.0, false);
        if (stereo)
            right = processChannel(state[1], right, shift + 0.5 * current.offset, feedback, delaySamples, amount, current.phase, true);
        else
            right = left;
    }
private:
    tune::Detector pitchDetector;
    bool tracking = false;
    inline static constexpr std::array<double, 6> coeffA {{ 0.09326978588384, 0.5564068609144891, 0.8601891194640949, 0.9624056862739603, 0.9903865627771149, 0.9978560630917253 }};
    inline static constexpr std::array<double, 6> coeffB {{ 0.3157368790022351, 0.7424126751118832, 0.9268159971807207, 0.9808941000943762, 0.9952696179382855, 0.9993871319757995 }};
    struct Allpass
    {
        std::array<double, 2> memory {};
        double process(double x, double a, std::size_t parity) noexcept
        {
            const double y = memory[parity] - a * x;
            memory[parity] = x + a * y;
            return y;
        }
    };
    struct Channel
    {
        std::array<Allpass, 6> a {}, b {};
        std::vector<float> history;
        std::size_t write = 0, parity = 0, valid = 0;
        double filtered = 0.0, lastPhase = 0.0, phaseC = 1.0, phaseS = 0.0;
        double lastB = 0.0, oscC = 1.0, oscS = 0.0, rotC = 1.0, rotS = 0.0, lastHz = 0.0;
        unsigned counter = 0;
        void clear() noexcept
        {
            a = {}; b = {};
            // Invalidate old samples instead of clearing a multi-second buffer on the audio thread.
            write = parity = valid = 0; counter = 0;
            filtered = lastPhase = phaseS = 0.0; phaseC = 1.0;
            lastB = oscS = rotS = lastHz = 0.0;
            oscC = rotC = 1.0;
        }
    };
    std::array<Channel, 2> state;
    Controls target {}, current {};
    std::array<double, 4> modeWeights {{ 1.0, 0.0, 0.0, 0.0 }};
    double sr = 44100.0, smoothing = 0.003, fadeStep = 0.001, wet = 0.0;
    double lfoPhase = 0.0, filterCoefficient = 1.0, targetFilterCoefficient = 1.0;
    float cachedCutoff = -1.0f;
    bool stereo = true, running = false, wanted = false;
    static float bound(float x, float lo, float hi, float fallback) noexcept
    { return std::clamp(std::isfinite(x) ? x : fallback, lo, hi); }
    void smooth(float& value, float goal) const noexcept
    {
        const float delta = goal - value;
        const float next = value + static_cast<float>(smoothing * delta);
        value = std::abs(delta) < 0.0001f || next == value ? goal : next;
    }
    float processChannel(Channel& ch, float input, double hz, double fb, double delay, double amount, double phase, bool rightChannel) noexcept
    {
        const auto size = ch.history.size();
        const auto whole = static_cast<std::size_t>(delay);
        const double fraction = delay - static_cast<double>(whole);
        const auto read = ch.write >= whole ? ch.write - whole : ch.write + size - whole;
        const auto older = read == 0 ? size - 1 : read - 1;
        const double recent = whole <= ch.valid ? ch.history[read] : 0.0;
        const double previous = whole + 1 <= ch.valid ? ch.history[older] : 0.0;
        const double delayed = recent + fraction * (previous - recent);
        ch.filtered += filterCoefficient * (delayed - ch.filtered);
        if (std::abs(ch.filtered) < 1.0e-20) ch.filtered = 0.0;
        // Bound the recirculation, not the direct signal. Feedback cannot
        // accumulate an unbounded state even at extreme automated settings.
        const double x = static_cast<double>(input) + fb * std::clamp(ch.filtered, -8.0, 8.0);
        double i = x, q = x;
        for (std::size_t j = 0; j < coeffA.size(); ++j)
        {
            i = ch.a[j].process(i, coeffA[j], ch.parity);
            q = ch.b[j].process(q, coeffB[j], ch.parity);
        }
        const double delayedQ = ch.lastB;
        ch.lastB = q; ch.parity ^= 1u;
        if (hz != ch.lastHz)
        {
            const double angle = 6.28318530717958647692 * hz / sr;
            ch.rotC = std::cos(angle); ch.rotS = std::sin(angle); ch.lastHz = hz;
        }
        const double c = ch.oscC, s = ch.oscS;
        ch.oscC = c * ch.rotC - s * ch.rotS;
        ch.oscS = s * ch.rotC + c * ch.rotS;
        if (++ch.counter == 1024u)
        {
            const double norm = std::sqrt(ch.oscC * ch.oscC + ch.oscS * ch.oscS);
            ch.oscC /= norm; ch.oscS /= norm; ch.counter = 0;
        }
        if (phase != ch.lastPhase)
        {
            const double radians = phase * (3.14159265358979323846 / 180.0);
            ch.phaseC = std::cos(radians); ch.phaseS = std::sin(radians); ch.lastPhase = phase;
        }
        const double carrierC = c * ch.phaseC - s * ch.phaseS;
        const double carrierS = s * ch.phaseC + c * ch.phaseS;
        const double shifted = modeWeights[0] * (i * carrierC - delayedQ * carrierS)
                             + modeWeights[1] * (i * carrierC + delayedQ * carrierS)
                             + modeWeights[2] * (x * carrierC)
                             + modeWeights[3] * (i * carrierC + (rightChannel ? -1.0 : 1.0) * delayedQ * carrierS);
        ch.history[ch.write] = static_cast<float>(std::clamp(shifted, -8.0, 8.0));
        if (++ch.write == size) ch.write = 0;
        if (ch.valid < size) ++ch.valid;
        return static_cast<float>(input + amount * (shifted - input));
    }
};
}
