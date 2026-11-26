#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace transform::channel
{
struct Controls
{
    float gate = -80.0f, comp = 0.0f, lowCut = 0.0f;
    float low = 0.0f, mid = 0.0f, high = 0.0f, presence = 0.0f;
    float threshold = -42.0f, ratio = 8.0f, compAttackMs = 3.0f, compReleaseMs = 100.0f, saturation = 0.0f;
};

// Simple stereo-linked dynamics and complementary one-pole tonal bands.
// Neutral and settled bypass are exact passthrough, with no audio allocation.
class Engine
{
public:
    void prepare(double rate, int channels) noexcept
    {
        sr = std::clamp(rate, 8000.0, 384000.0);
        stereo = channels > 1;
        smooth = timeCoeff(20.0f);
        attack = timeCoeff(3.0f);
        release = timeCoeff(90.0f);
        gateAttack = timeCoeff(4.0f);
        gateRelease = timeCoeff(120.0f);
        compAttack = timeCoeff(3.0f);
        compRelease = timeCoeff(100.0f);
        meterRelease = timeCoeff(250.0f);
        bassCoeff = filterCoeff(250.0f);
        trebleCoeff = filterCoeff(2500.0f);
        presenceLoCoeff = filterCoeff(2000.0f);
        presenceHiCoeff = filterCoeff(5000.0f);
        fadeStep = static_cast<float>(1.0 / (sr * 0.02));
        reset();
    }
    void reset() noexcept
    {
        enabled = false; wet = 0.0f; env = 0.0f; gateGain = compGain = 1.0f;
        current = {}; target = {}; states = {}; cutCoeff = filterCoeff(20.0f);
        cutMix = 0.0f; cutTarget = 0.0f;
        previous = {}; haveControls = false; meterPeak = 0.0f;
    }
    void disable() noexcept { enabled = false; }
    void setControls(bool active, const Controls& v) noexcept
    {
        if (!active) { disable(); return; }
        const Controls c { std::clamp(v.gate, -80.0f, -20.0f), std::clamp(v.comp, 0.0f, 100.0f),
            std::clamp(v.lowCut, 0.0f, 300.0f), std::clamp(v.low, -24.0f, 24.0f),
            std::clamp(v.mid, -24.0f, 24.0f), std::clamp(v.high, -24.0f, 24.0f),
            std::clamp(v.presence, -24.0f, 24.0f),
            std::clamp(v.threshold, -60.0f, 0.0f), std::clamp(v.ratio, 1.0f, 20.0f),
            std::clamp(v.compAttackMs, 0.1f, 100.0f), std::clamp(v.compReleaseMs, 10.0f, 1000.0f),
            std::clamp(v.saturation, 0.0f, 100.0f) };
        const bool nonNeutral = c.gate > -80.0f || c.comp > 0.0f || c.lowCut > 0.0f
            || c.low != 0.0f || c.mid != 0.0f || c.high != 0.0f || c.presence != 0.0f || c.saturation > 0.0f;
        if (!nonNeutral) { disable(); return; }
        if (!enabled && wet == 0.0f)
        {
            states = {}; env = 0.0f; gateGain = compGain = 1.0f;
            current = {}; cutMix = 0.0f; haveControls = false;
        }
        enabled = true;
        if (!haveControls || c.gate != previous.gate) target[0] = c.gate > -80.0f ? db(c.gate) : 0.0f;
        target[1] = c.comp * 0.01f;
        if (!haveControls || c.lowCut != previous.lowCut)
            cutCoeffTarget = filterCoeff(std::max(1.0f, c.lowCut));
        cutTarget = c.lowCut > 0.0f ? 1.0f : 0.0f;
        if (!haveControls || c.low != previous.low) target[2] = db(c.low) - 1.0f;
        if (!haveControls || c.mid != previous.mid) target[3] = db(c.mid) - 1.0f;
        if (!haveControls || c.high != previous.high) target[4] = db(c.high) - 1.0f;
        if (!haveControls || c.presence != previous.presence) target[5] = db(c.presence) - 1.0f;
        target[6] = c.saturation * 0.01f;
        if (!haveControls || c.threshold != previous.threshold) compThreshold = db(c.threshold);
        if (!haveControls || c.ratio != previous.ratio) compExponent = 1.0f - 1.0f / c.ratio;
        if (!haveControls || c.compAttackMs != previous.compAttackMs) compAttack = timeCoeff(c.compAttackMs);
        if (!haveControls || c.compReleaseMs != previous.compReleaseMs) compRelease = timeCoeff(c.compReleaseMs);
        previous = c; haveControls = true;
    }
    float reductionDb() const noexcept { return -20.0f * std::log10(std::max(1.0f - meterPeak, 0.0001f)); }
    void process(float& left, float& right) noexcept
    {
        if (!enabled && wet == 0.0f) return;
        wet = enabled ? std::min(1.0f, wet + fadeStep) : std::max(0.0f, wet - fadeStep);
        if (wet == 0.0f) { meterPeak = 0.0f; return; }
        for (std::size_t i = 0; i < current.size(); ++i)
        {
            current[i] += smooth * (target[i] - current[i]);
            if (std::abs(current[i] - target[i]) < 1.0e-7f) current[i] = target[i];
        }
        cutCoeff += smooth * (cutCoeffTarget - cutCoeff);
        cutMix += smooth * (cutTarget - cutMix);
        const float peak = stereo ? std::max(std::abs(left), std::abs(right)) : std::abs(left);
        env += (peak > env ? attack : release) * (peak - env);
        // A 12 dB soft opening region avoids hard threshold chatter.
        const float gateThreshold = current[0];
        const float wantedGate = gateThreshold > 0.0f
            ? std::clamp((env / gateThreshold - 0.25f) / 0.75f, 0.0f, 1.0f) : 1.0f;
        gateGain += (wantedGate > gateGain ? gateAttack : gateRelease) * (wantedGate - gateGain);
        // COMP blends the requested compression curve and proportional makeup.
        float wantedComp = 1.0f;
        if (current[1] > 0.0f && compExponent > 0.0f && env > compThreshold)
            wantedComp += current[1] * (std::pow(compThreshold / env, compExponent) - 1.0f);
        compGain += (wantedComp < compGain ? compAttack : compRelease) * (wantedComp - compGain);
        meterPeak = std::max(wet * (1.0f - compGain), meterPeak * (1.0f - meterRelease));
        // Scale the existing automatic makeup with the selected ratio.
        const float makeup = 1.0f + current[1] * 0.584893192f * (compExponent / 0.875f);
        const float dynamics = gateGain * compGain * makeup;
        const float l = tone(left, states[0], dynamics);
        const float r = stereo ? tone(right, states[1], dynamics) : l;
        left += wet * (l - left);
        right += wet * (r - right);
    }
private:
    struct State { float cut = 0, bass = 0, treble = 0, presenceLo = 0, presenceHi = 0; };
    static float db(float v) noexcept { return std::pow(10.0f, v * 0.05f); }
    float timeCoeff(float ms) const noexcept { return static_cast<float>(-std::expm1(-1000.0 / (sr * ms))); }
    float filterCoeff(float hz) const noexcept { return static_cast<float>(-std::expm1(-6.283185307179586 * std::min<double>(hz, sr * .45) / sr)); }
    static float lp(float input, float& z, float a) noexcept { z += a * (input - z); return z; }
    float tone(float input, State& s, float dynamics) const noexcept
    {
        const float x = (input - cutMix * lp(input, s.cut, cutCoeff)) * dynamics;
        const float bass = lp(x, s.bass, bassCoeff);
        const float belowTreble = lp(x, s.treble, trebleCoeff);
        const float pLo = lp(x, s.presenceLo, presenceLoCoeff);
        const float pHi = lp(x, s.presenceHi, presenceHiCoeff);
        const float eq = x + current[2] * bass + current[3] * (belowTreble - bass)
            + current[4] * (x - belowTreble) + current[5] * (pHi - pLo);
        // Continuous, symmetric soft saturation; unity at SAT = 0.
        const float drive = 4.0f * current[6];
        return drive > 0.0f ? eq * ((1.0f + drive) / (1.0f + drive * std::abs(eq))) : eq;
    }
    double sr = 44100.0;
    bool stereo = true, enabled = false, haveControls = false;
    float smooth = 0, attack = 0, release = 0, gateAttack = 0, gateRelease = 0, compAttack = 0, compRelease = 0;
    float bassCoeff = 0, trebleCoeff = 0, presenceLoCoeff = 0, presenceHiCoeff = 0, fadeStep = 0;
    float wet = 0, env = 0, gateGain = 1, compGain = 1, cutCoeff = 0, cutCoeffTarget = 0, cutMix = 0, cutTarget = 0;
    float compThreshold = 0.00794328235f, compExponent = 0.875f, meterPeak = 0.0f, meterRelease = 0.0f;
    std::array<float, 7> current {}, target {};
    std::array<State, 2> states {};
    Controls previous {};
};
}
