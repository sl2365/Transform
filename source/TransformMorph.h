#pragma once

#include "TransformTune.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace transform::morph
{
inline constexpr std::array<const char*, 3> modeNames {{ "Additive", "Reed", "Metallic" }};
struct Controls
{
    float harmonics = 50.0f, timbre = 50.0f, sub = 0.0f, response = 30.0f, blend = 0.0f;
    float density = 6.0f, smear = 0.0f, modRate = 1.0f, modDepth = 0.0f;
    int mode = 0;
};

// Monophonic, envelope-following additive resynthesis. Analyse the actual
// upstream wet signal, not the original TUNE input (which may have been shifted).
class Engine
{
public:
    void prepare(double rate, int channels) noexcept
    {
        sampleRate = std::isfinite(rate) && rate >= 16000.0 && rate <= 384000.0 ? rate : 44100.0;
        stereo = channels > 1;
        detector.prepare(rate);
        for (std::size_t n = 1; n < inverseRoots.size(); ++n)
            inverseRoots[n] = 1.0f / std::sqrt(static_cast<float>(n));
        smooth = coefficient(15.0f);
        wetStep = static_cast<float>(1.0 / (0.020 * sampleRate));
        attack = coefficient(4.0f);
        release = coefficient(30.0f);
        previousResponse = 30.0f;
        reset();
    }
    void reset() noexcept
    {
        detector.reset();
        active = false; awake = false;
        target = {}; current = target;
        wet = voiced = frequency = envL = envR = 0.0f;
        phase = smearPhase = metalPhase = lfoPhase = 0.0;
        reedWeight = metalWeight = 0.0f;
    }
    void setControls(bool enabled, const Controls& c) noexcept
    {
        const auto percent = [](float x, float fallback) { return std::clamp(std::isfinite(x) ? x : fallback, 0.0f, 100.0f); };
        target = { percent(c.harmonics, 50.0f), percent(c.timbre, 50.0f), percent(c.sub, 0.0f),
                   std::clamp(std::isfinite(c.response) ? c.response : 30.0f, 5.0f, 200.0f), percent(c.blend, 0.0f),
                   std::clamp(std::isfinite(c.density) ? c.density : 6.0f, 1.0f, 16.0f),
                   percent(c.smear, 0.0f), std::clamp(std::isfinite(c.modRate) ? c.modRate : 1.0f, 0.05f, 20.0f),
                   percent(c.modDepth, 0.0f), std::clamp(c.mode, 0, 2) };
        if (target.response != previousResponse)
        {
            release = coefficient(target.response);
            previousResponse = target.response;
        }
        active = enabled && target.blend > 0.0f;
    }
    void disable() noexcept { active = false; }
    void process(float& left, float& right) noexcept
    {
        if (!active && !awake) return;
        if (!awake)
        {
            detector.reset();
            current = target;
            wet = voiced = frequency = envL = envR = 0.0f;
            phase = smearPhase = metalPhase = lfoPhase = 0.0;
            reedWeight = metalWeight = 0.0f;
            awake = true;
        }
        const float desiredWet = active ? target.blend * 0.01f : 0.0f;
        wet += std::clamp(desiredWet - wet, -wetStep, wetStep);
        if (!active && wet <= 0.0f)
        {
            awake = false;
            wet = 0.0f;
            return;
        }
        const float l = safe(left), r = stereo ? safe(right) : l;
        detector.process(&l, stereo ? &r : nullptr, 1);
        const float detected = detector.getFrequency();
        const bool valid = detected >= tune::Detector::minimumHz && detected <= tune::Detector::maximumHz;
        // Follow a valid note smoothly; fade the resynthesis to silence on unvoiced input.
        // Pitch confidence must never control the dry/wet mix.
        voiced += smooth * ((valid ? 1.0f : 0.0f) - voiced);
        if (valid)
        {
            if (frequency <= 0.0f) frequency = detected;
            else frequency += smooth * (detected - frequency);
        }
        const auto envelope = [this](float& env, float x)
        {
            const float power = x * x;
            env += (power > env ? attack : release) * (power - env);
        };
        envelope(envL, l);
        envelope(envR, r);
        if (frequency <= 0.0f || voiced < 1.0e-5f)
        {
            left = (1.0f - wet) * l;
            right = stereo ? (1.0f - wet) * r : left;
            return;
        }
        current.harmonics += smooth * (target.harmonics - current.harmonics);
        current.timbre += smooth * (target.timbre - current.timbre);
        current.sub += smooth * (target.sub - current.sub);
        const auto approach = [this](float& x, float goal) { x += smooth * (goal - x); if (std::abs(x-goal) < 1.0e-5f) x = goal; };
        approach(current.density, target.density); approach(current.smear, target.smear);
        approach(current.modRate, target.modRate); approach(current.modDepth, target.modDepth);
        approach(reedWeight, target.mode == 1 ? 1.0f : 0.0f);
        approach(metalWeight, target.mode == 2 ? 1.0f : 0.0f);
        // Phase advances at half the fundamental; even harmonics of this
        // oscillator are exact multiples of the tracked fundamental.
        constexpr double twoPi = 6.2831853071795864769;
        phase += 0.5 * static_cast<double>(frequency) / sampleRate;
        if (phase >= 1.0) phase -= 1.0;
        const float c = static_cast<float>(std::cos(twoPi * phase));
        const float fundamental = 2.0f * c * c - 1.0f;
        const auto advance = [this](double& p, double hz) { p += hz / sampleRate; p -= std::floor(p); };
        advance(lfoPhase, current.modRate);
        advance(smearPhase, frequency * 1.006);
        advance(metalPhase, frequency * 1.41421356237);
        const float odd = std::clamp(current.timbre * 0.01f + (current.modDepth > 0.0f ? current.modDepth * 0.005f * static_cast<float>(std::sin(twoPi*lfoPhase)) : 0.0f), 0.0f, 1.0f);
        const float smearAmount = current.smear * 0.01f;
        const float smearFund = smearAmount > 0.0f ? static_cast<float>(std::cos(twoPi*smearPhase)) : 0.0f;
        const float metalFund = metalWeight > 0.0f ? static_cast<float>(std::cos(twoPi*metalPhase)) : 0.0f;
        const float harmonics = current.harmonics * 0.01f;
        const float sub = current.sub * 0.01f;
        float sum = fundamental + sub * c;
        float power = 1.0f + sub * sub;
        // Cosine recurrence avoids separate trigonometric calls for each partial.
        float previous = 1.0f, partial = fundamental;
        float smearPrevious = 1.0f, smearPartial = smearFund, metalPrevious = 1.0f, metalPartial = metalFund;
        for (int n = 2; n <= static_cast<int>(std::ceil(current.density)); ++n)
        {
            const float next = 2.0f * fundamental * partial - previous;
            previous = partial;
            partial = next;
            // Smoothly suppress partials approaching Nyquist at unusual rates.
            const float guard = std::clamp(static_cast<float>((0.48 * sampleRate - n * frequency) / (0.08 * sampleRate)), 0.0f, 1.0f);
            const float densityWeight = std::clamp(current.density - static_cast<float>(n - 1), 0.0f, 1.0f);
            const float baseWeight = harmonics * 2.0f * ((n & 1) ? odd : 1.0f - odd) / static_cast<float>(n);
            const float invRoot = (reedWeight > 0.0f || metalWeight > 0.0f) ? inverseRoots[static_cast<std::size_t>(n)] : 0.0f;
            const float reed = harmonics * ((n & 1) ? (0.5f + odd) : (1.0f - odd) * 0.12f) * invRoot;
            const float metallic = harmonics * (0.4f + 0.6f * ((n & 1) ? odd : 1.0f - odd)) * invRoot;
            const float base = densityWeight * guard * (baseWeight + reedWeight * (reed - baseWeight)) * (1.0f - metalWeight);
            const float metalGuard = std::clamp(static_cast<float>((0.48 * sampleRate - n * frequency * 1.41421356237) / (0.08 * sampleRate)), 0.0f, 1.0f);
            const float metal = densityWeight * metalGuard * metallic * metalWeight;
            const float nextSmear = 2.0f * smearFund * smearPartial - smearPrevious;
            smearPrevious = smearPartial; smearPartial = nextSmear;
            const float nextMetal = 2.0f * metalFund * metalPartial - metalPrevious;
            metalPrevious = metalPartial; metalPartial = nextMetal;
            const float smearGuard = std::clamp(static_cast<float>((0.48 * sampleRate - n * frequency * 1.006) / (0.08 * sampleRate)), 0.0f, 1.0f);
            const float main = base * (1.0f - smearAmount * 0.5f);
            const float spread = densityWeight * smearGuard * (baseWeight + reedWeight * (reed - baseWeight)) * (1.0f - metalWeight) * smearAmount * 0.5f;
            // Smear also decorrelates metallic partials by mixing their harmonic counterparts.
            const float metalMain = metal * (1.0f - smearAmount * 0.35f);
            const float metalSpread = densityWeight * smearGuard * metallic * metalWeight * smearAmount * 0.35f;
            sum += main * partial + (spread + metalSpread) * smearPartial + metalMain * metalPartial;
            power += (main + spread) * (main + spread) + (metalMain + metalSpread) * (metalMain + metalSpread);
        }
        const float carrier = sum * std::sqrt(2.0f / power);
        const float amount = wet * voiced;
        const float dryAmount = 1.0f - wet;
        const float outL = carrier * std::sqrt(std::max(0.0f, envL));
        const float outR = carrier * std::sqrt(std::max(0.0f, envR));
        left = dryAmount * l + amount * outL;
        right = stereo ? dryAmount * r + amount * outR : left;
    }
private:
    float coefficient(float ms) const noexcept
    { return static_cast<float>(1.0 - std::exp(-1.0 / (0.001 * ms * sampleRate))); }
    static float safe(float x) noexcept { return std::clamp(std::isfinite(x) ? x : 0.0f, -16.0f, 16.0f); }
    std::array<float, 17> inverseRoots {};
    tune::Detector detector;
    Controls target {}, current {};
    double sampleRate = 44100.0, phase = 0.0, smearPhase = 0.0, metalPhase = 0.0, lfoPhase = 0.0;
    float reedWeight = 0.0f, metalWeight = 0.0f;
    bool stereo = true, active = false, awake = false;
    float smooth = 0.0f, wetStep = 0.0f, attack = 0.0f, release = 0.0f, previousResponse = -1.0f;
    float wet = 0.0f, voiced = 0.0f, frequency = 0.0f, envL = 0.0f, envR = 0.0f;
};
}
