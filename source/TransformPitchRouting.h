#pragma once

#include "TransformTimePitch.h"

namespace transform::pitch
{
// Parallel corrected reference and transformed lead. Both have the same fixed
// delay. The reference only needs an FFT when a partial PITCH mix also uses TUNE.
// A third, separately gated engine shifts the feedback signal by the manual
// interval only; it never reapplies the original input's tuning error.
class Lead
{
public:
    void prepare(double sr, int channels)
    {
        wet.prepare(sr, channels);
        reference.prepare(sr, channels);
        echo.prepare(sr, channels);
        const double rate = std::isfinite(sr) ? std::clamp(sr, 8000.0, 384000.0) : 44100.0;
        step = static_cast<float>(1.0 / (0.020 * rate));
        reset();
    }
    int latency() const noexcept { return wet.latency(); }
    void reset() noexcept
    {
        wet.reset(); reference.reset(); echo.reset();
        manual = false; initial = true; needWet = needReference = echoActive = false;
        shift = cents = formant = correction = 0.0f;
        speed = 40.0f;
        enhancements = {};
        mix = targetMix = 1.0f;
        feedback = targetFeedback = echoMix = 0.0f;
        previousL = previousR = 0.0f;
        warmup = 0;
        selectedAlgorithm = 0;
    }
    void setControls(bool enabled, float semitones, float fine, float resonance,
                     const Enhancements& extra, float blendPercent, float feedbackPercent, int algorithm = 0) noexcept
    {
        manual = enabled;
        selectedAlgorithm = std::clamp(algorithm, 0, lastAlgorithm);
        wet.setAlgorithm(selectedAlgorithm);
        shift = semitones; cents = fine; formant = resonance; enhancements = extra;
        targetMix = manual ? bounded(blendPercent, 100.0f) * 0.01f : 1.0f;
        targetFeedback = manual ? bounded(feedbackPercent, 95.0f) * 0.01f : 0.0f;
        if (initial) { mix = targetMix; initial = false; }
        updateBranches();
        updateEcho();
    }
    void setCorrection(float semitones, float speedMs) noexcept
    {
        correction = semitones; speed = speedMs;
        wet.setCorrection(needWet ? correction : 0.0f, speed);
        reference.setCorrection(needReference ? correction : 0.0f, speed);
    }
    void process(float& l, float& r, float& dryL, float& dryR, AnalysisCache* cache = nullptr) noexcept
    {
        float baseL = l, baseR = r, unusedL = 0.0f, unusedR = 0.0f;
        // Keep the neutral delay histories warm even at the endpoints.
        reference.process(baseL, baseR, unusedL, unusedR, cache);
        wet.process(l, r, dryL, dryR, cache);
        if (echoActive)
        {
            const float requestedGain = needWet ? targetFeedback : 0.0f;
            feedback = approach(feedback, requestedGain, step);
            echoMix = approach(echoMix, requestedGain > 0.0f ? 1.0f : 0.0f, step);
            float el = safe(previousL) * feedback, er = safe(previousR) * feedback;
            unusedL = unusedR = 0.0f;
            echo.process(el, er, unusedL, unusedR);
            l += safe(el) * echoMix;
            r += safe(er) * echoMix;
            previousL = l; previousR = r;
            if (echoMix == 0.0f && feedback == 0.0f && requestedGain == 0.0f)
            {
                echoActive = false;
                previousL = previousR = 0.0f;
            }
        }
        if (warmup > 0) --warmup;
        else if (mix != targetMix)
        {
            mix = approach(mix, targetMix, step);
            if (mix == targetMix) { updateBranches(); updateEcho(); }
        }
        if (mix == 0.0f) { l = baseL; r = baseR; }
        else if (mix < 1.0f) { l = baseL + (l - baseL) * mix; r = baseR + (r - baseR) * mix; }
    }
private:
    Selectable wet, echo;
    Engine reference;
    Enhancements enhancements {};
    bool manual = false, initial = true, needWet = false, needReference = false, echoActive = false;
    float shift = 0.0f, cents = 0.0f, formant = 0.0f, correction = 0.0f, speed = 40.0f;
    float mix = 1.0f, targetMix = 1.0f, step = 0.001f;
    float feedback = 0.0f, targetFeedback = 0.0f, echoMix = 0.0f, previousL = 0.0f, previousR = 0.0f;
    int warmup = 0, selectedAlgorithm = 0;
    static float bounded(float x, float hi) noexcept { return std::clamp(std::isfinite(x) ? x : 0.0f, 0.0f, hi); }
    static float safe(float x) noexcept { return std::clamp(std::isfinite(x) ? x : 0.0f, -2.0f, 2.0f); }
    static float approach(float x, float target, float amount) noexcept
    { const float delta = target - x; return std::abs(delta) <= amount ? target : x + std::copysign(amount, delta); }
    void updateBranches() noexcept
    {
        const bool nextWet = mix > 0.0f || targetMix > 0.0f;
        const bool nextReference = mix < 1.0f || targetMix < 1.0f;
        // Do not fade towards a newly enabled spectral path before its OLA
        // has filled. The other branch stays audible during this interval.
        if (mix != targetMix && ((!needWet && nextWet) || (!needReference && nextReference)))
            warmup = latency();
        needWet = nextWet; needReference = nextReference;
        wet.setControls(manual && needWet, shift, cents, formant);
        wet.setEnhancements(manual && needWet, enhancements);
        wet.setCorrection(needWet ? correction : 0.0f, speed);
        reference.setCorrection(needReference ? correction : 0.0f, speed);
    }
    void updateEcho() noexcept
    {
        if (needWet && targetFeedback > 0.0f)
        {
            if (!echoActive)
            {
                echo.reset();
                previousL = previousR = 0.0f;
                feedback = echoMix = 0.0f;
                echoActive = true;
            }
            echo.setAlgorithm(selectedAlgorithm);
            echo.setControls(manual, shift, cents, formant);
            echo.setEnhancements(manual, enhancements);
        }
    }
};
}
