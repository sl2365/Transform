#pragma once

#include <JuceHeader.h>
#include <array>
#include <algorithm>
#include <cmath>

namespace transform::sequencer
{
constexpr int stepCount = 32;
constexpr int targetSlotCount = 16;
constexpr int rateChoiceCount = 14;

enum class Direction
{
    loop = 0,
    bounce,
    reverse,
    played,
    random
};

enum class GateStepMode
{
    off = 0,
    shortStep,
    longStep,
    rest
};

enum class Waveform
{
    saw = 0,
    sawDouble,
    sawDown,
    sawDownDouble,
    sine,
    sineDouble,
    triangle,
    triangleDouble,
    pulse25,
    pulse25Double,
    square,
    squareDouble,
    pulse75,
    pulse75Double
};

struct TargetInfo
{
    const char* parameterId;
    const char* displayName;
};

inline constexpr std::array<TargetInfo, 116> targets
{{
    { "mix", "Dry / Wet" },
    { "output", "Output" },
    { "sequencerAttack", "Seq Attack" },
    { "sequencerRelease", "Seq Release" },
    { "sequencerPeak", "Seq Depth" },
    { "sequencerMove", "Seq Offset" },
    { "sequencerStart", "Seq Start" },
    { "sequencerEnd", "Seq End" },
    { "sequencerGateShortLength", "Gate Short" },
    { "sequencerGateLongLength", "Gate Long" },
    { "pitchSemitones", "Pitch Shift" },
    { "pitchFine", "Pitch Fine" },
    { "tuneAmount", "Tune Amount" },
    { "tuneSpeed", "Tune Speed" },
    { "pitchFormant", "Pitch Formant" },
    { "harmony1Interval", "Harmony 1 Interval" },
    { "harmony1Pan", "Harmony 1 Pan" },
    { "harmony1Level", "Harmony 1 Level" },
    { "harmony2Interval", "Harmony 2 Interval" },
    { "harmony2Pan", "Harmony 2 Pan" },
    { "harmony2Level", "Harmony 2 Level" },
    { "harmony3Interval", "Harmony 3 Interval" },
    { "harmony3Pan", "Harmony 3 Pan" },
    { "harmony3Level", "Harmony 3 Level" },
    { "harmony4Interval", "Harmony 4 Interval" },
    { "harmony4Pan", "Harmony 4 Pan" },
    { "harmony4Level", "Harmony 4 Level" },
    { "freqShift", "Freq Shift" },
    { "freqOffset", "Freq Offset" },
    { "freqFeedback", "Freq Feedback" },
    { "freqDelay", "Freq Delay" },
    { "freqBlend", "Freq Blend" },
    { "freezePitch", "Freeze Pitch" },
    { "freezeSize", "Freeze Size" },
    { "freezePosition", "Freeze Position" },
    { "freezeBlend", "Freeze Blend" },
    { "morphHarmonics", "Morph Harmonics" },
    { "morphTimbre", "Morph Timbre" },
    { "morphSub", "Morph Sub" },
    { "morphResponse", "Morph Response" },
    { "morphBlend", "Morph Blend" },
    { "vocoderAttack", "Vocoder Attack" },
    { "vocoderRelease", "Vocoder Release" },
    { "vocoderFormant", "Vocoder Formant" },
    { "vocoderWidth", "Vocoder Width" },
    { "vocoderNoise", "Vocoder Noise" },
    { "vocoderBlend", "Vocoder Blend" },
    { "deEssFrequency", "De-ess Frequency" },
    { "deEssThreshold", "De-ess Threshold" },
    { "deEssRange", "De-ess Range" },
    { "deEssAttack", "De-ess Attack" },
    { "deEssRelease", "De-ess Release" },
    { "channelGate", "Channel Gate" },
    { "channelComp", "Channel Compressor" },
    { "channelLowCut", "Channel Low Cut" },
    { "channelLow", "Channel Low" },
    { "channelMid", "Channel Mid" },
    { "channelHigh", "Channel High" },
    { "channelPresence", "Channel Presence" },
    { "tuneTolerance", "Tune Tolerance" },
    { "tuneVibrato", "Tune Vibrato" },
    { "tuneReference", "Tune Reference" },
    { "pitchPreserve", "Pitch Preserve" },
    { "pitchGlide", "Pitch Glide" },
    { "pitchDetune", "Pitch Detune" },
    { "pitchTransient", "Pitch Transient" },
    { "pitchBlend", "Pitch Blend" },
    { "pitchFeedback", "Pitch Feedback" },
    { "freqModRate", "Freq Mod Rate" },
    { "freqModDepth", "Freq Mod Depth" },
    { "freqPhase", "Freq Phase" },
    { "freqCutoff", "Freq Feedback LP" },
    { "freqDivision", "Freq Delay Division" },
    { "harmony1Formant", "Harmony 1 Formant" },
    { "harmony1Human", "Harmony 1 Humanise" },
    { "harmony1Delay", "Harmony 1 Delay" },
    { "harmony2Formant", "Harmony 2 Formant" },
    { "harmony2Human", "Harmony 2 Humanise" },
    { "harmony2Delay", "Harmony 2 Delay" },
    { "harmony3Formant", "Harmony 3 Formant" },
    { "harmony3Human", "Harmony 3 Humanise" },
    { "harmony3Delay", "Harmony 3 Delay" },
    { "harmony4Formant", "Harmony 4 Formant" },
    { "harmony4Human", "Harmony 4 Humanise" },
    { "harmony4Delay", "Harmony 4 Delay" },
    { "freezeDensity", "Freeze Density" },
    { "freezeSmooth", "Freeze Smooth" },
    { "freezeDrift", "Freeze Drift" },
    { "freezeCapture", "Freeze Capture" },
    { "vocoderBandwidth", "Vocoder Bandwidth" },
    { "vocoderDetune", "Vocoder Detune" },
    { "vocoderUnvoiced", "Vocoder Unvoiced" },
    { "vocoderBands", "Vocoder Bands" },
    { "morphDensity", "Morph Density" },
    { "morphSmear", "Morph Smear" },
    { "morphModRate", "Morph Mod Rate" },
    { "morphModDepth", "Morph Mod Depth" },
    { "deEssBandwidth", "De-ess Bandwidth" },
    { "deEssAdaptive", "De-ess Adaptive" },
    { "channelThreshold", "Channel Threshold" },
    { "channelRatio", "Channel Ratio" },
    { "channelAttack", "Channel Attack" },
    { "channelRelease", "Channel Release" },
    { "channelSaturation", "Channel Saturation" },
    { "vocoderShift", "Vocoder Shift" },
    { "vocoderContour", "Vocoder Contour" },
    { "vocoderCompand", "Vocoder Compand" },
    { "vocoderModIn", "Vocoder Modulator Input" },
    { "vocoderCarIn", "Vocoder Carrier Input" },
    { "vocoderHarmonic", "Vocoder Harmonic" },
    { "vocoderSub", "Vocoder Sub" },
    { "freezeFormant", "Freeze Formant" },
    { "freezeAttack", "Freeze Attack" },
    { "freezeRelease", "Freeze Release" },
    { "freqRelativeShift", "Freq Relative Amount" },
    { "freezeGlide", "Freeze Glide" }
}};

inline constexpr int mixTargetChoice = 1;
inline constexpr int outputTargetChoice = 2;
inline constexpr int sequencerAttackTargetChoice = 3;
inline constexpr int sequencerReleaseTargetChoice = 4;
inline constexpr int sequencerDepthTargetChoice = 5;
inline constexpr int sequencerOffsetTargetChoice = 6;
inline constexpr int sequencerStartTargetChoice = 7;
inline constexpr int sequencerEndTargetChoice = 8;
inline constexpr int sequencerGateShortTargetChoice = 9;
inline constexpr int sequencerGateLongTargetChoice = 10;
inline constexpr int pitchSemitonesTargetChoice = 11;
inline constexpr int pitchFineTargetChoice = 12;
inline constexpr int tuneAmountTargetChoice = 13;
inline constexpr int tuneSpeedTargetChoice = 14;
inline constexpr int pitchFormantTargetChoice = 15;
inline constexpr int harmonyFirstTargetChoice = 16;
inline constexpr int freqFirstTargetChoice = 28;
inline constexpr int freezeFirstTargetChoice = 33;
inline constexpr int morphFirstTargetChoice = 37;
inline constexpr int vocoderFirstTargetChoice = 42;
inline constexpr int deEssFirstTargetChoice = 48;
inline constexpr int channelFirstTargetChoice = 53;
inline constexpr int tuneToleranceTargetChoice = 60;
inline constexpr int tuneVibratoTargetChoice = 61;
inline constexpr int tuneReferenceTargetChoice = 62;
inline constexpr int pitchPreserveTargetChoice = 63;
inline constexpr int pitchGlideTargetChoice = 64;
inline constexpr int pitchDetuneTargetChoice = 65;
inline constexpr int pitchTransientTargetChoice = 66;
inline constexpr int pitchBlendTargetChoice = 67;
inline constexpr int pitchFeedbackTargetChoice = 68;
inline constexpr int freqExtraFirstTargetChoice = 69;
inline constexpr int freqDivisionTargetChoice = 73;
inline constexpr int harmonyExtraFirstTargetChoice = 74;
inline constexpr int freezeExtraFirstTargetChoice = 86;
inline constexpr int vocoderExtraFirstTargetChoice = 90;
inline constexpr int vocoderBandsTargetChoice = 93;
inline constexpr int morphExtraFirstTargetChoice = 94;
inline constexpr int deEssExtraFirstTargetChoice = 98;
inline constexpr int channelExtraFirstTargetChoice = 100;
inline constexpr int vocoderToneFirstTargetChoice = 105;
inline constexpr int freezeCreativeFirstTargetChoice = 112;
inline constexpr int freqRelativeTargetChoice = 115;
inline constexpr int freezeGlideTargetChoice = 116;

inline int targetChoiceForParameter(const juce::String& parameterId)
{
    for (size_t index = 0; index < targets.size(); ++index)
        if (parameterId == targets[index].parameterId)
            return static_cast<int>(index) + 1;
    return 0;
}

inline juce::String targetName(int choice)
{
    if (choice <= 0 || choice > static_cast<int>(targets.size()))
        return {};
    return targets[static_cast<size_t>(choice - 1)].displayName;
}

inline juce::String stepParameterId(int step)
{
    return "seqStep" + juce::String(step + 1);
}

inline juce::String gateStepParameterId(int step)
{
    return "sequencerGateStep" + juce::String(step + 1);
}

inline GateStepMode gateStepModeFromChoice(float choice) noexcept
{
    return static_cast<GateStepMode>(juce::jlimit(
        static_cast<int>(GateStepMode::off),
        static_cast<int>(GateStepMode::rest),
        static_cast<int>(std::lround(choice))));
}

inline float gateStepTarget(GateStepMode mode, float position,
                            float shortLength, float longLength) noexcept
{
    position = juce::jlimit(0.0f, 1.0f, position);
    shortLength = juce::jlimit(0.10f, 0.60f, shortLength);
    longLength = juce::jlimit(0.65f, 0.95f, longLength);

    switch (mode)
    {
        case GateStepMode::shortStep:
            return position < shortLength ? 1.0f : 0.0f;
        case GateStepMode::longStep:
            return position < longLength ? 1.0f : 0.0f;
        case GateStepMode::rest:
            return 1.0f;
        case GateStepMode::off:
        default:
            return 0.0f;
    }
}

inline juce::String targetParameterId(int slot)
{
    return "seqTarget" + juce::String(slot + 1);
}

inline juce::String targetEnabledParameterId(int slot)
{
    return "seqTargetEnabled" + juce::String(slot + 1);
}

inline juce::String targetDepthParameterId(int slot)
{
    return targetParameterId(slot) + "_depth";
}

inline float applyTargetDepth(float canonicalSequenceValue, bool,
                              float depth) noexcept
{
    // Both sequence displays share one canonical domain: 0.5 is
    // neutral, Unipolar uses 0.5-1.0 and Bipolar uses 0.0-1.0. Moving towards
    // that neutral point makes 0% hold the saved destination and 100% retain
    // the established modulation exactly.
    canonicalSequenceValue = juce::jlimit(
        0.0f, 1.0f, canonicalSequenceValue);
    depth = juce::jlimit(0.0f, 1.0f, depth);
    return 0.5f + (canonicalSequenceValue - 0.5f) * depth;
}

inline double beatsForRate(int rateIndex) noexcept
{
    constexpr std::array<double, rateChoiceCount> beats
    {
        1.0 / 32.0, 1.0 / 24.0, 1.0 / 16.0, 1.0 / 12.0,
        1.0 / 8.0, 1.0 / 6.0, 1.0 / 4.0, 1.0 / 3.0,
        1.0 / 2.0, 2.0 / 3.0, 1.0, 4.0 / 3.0, 2.0, 4.0
    };
    return beats[static_cast<size_t>(juce::jlimit(
        0, rateChoiceCount - 1, rateIndex))];
}

inline unsigned hash(unsigned value) noexcept
{
    value ^= value >> 16;
    value *= 0x7feb352dU;
    value ^= value >> 15;
    value *= 0x846ca68bU;
    value ^= value >> 16;
    return value;
}

inline int greatestCommonDivisor(int left, int right) noexcept
{
    while (right != 0)
    {
        const int remainder = left % right;
        left = right;
        right = remainder;
    }
    return left;
}

inline int randomOffset(int tick, int length) noexcept
{
    if (length <= 1)
        return 0;
    const int shuffle = tick / length;
    const int position = tick % length;
    const unsigned seed = hash(static_cast<unsigned>(length * 131
                                                     + shuffle * 977));
    int stride = 1 + static_cast<int>(seed
        % static_cast<unsigned>(length - 1));
    while (greatestCommonDivisor(stride, length) != 1)
        stride = stride % (length - 1) + 1;
    const int offset = static_cast<int>(
        hash(static_cast<unsigned>(length) ^ 0x9e3779b9U)
        % static_cast<unsigned>(length));
    return (offset + (shuffle & 1) + position * stride) % length;
}

struct Position
{
    int current = 0;
    int previous = 0;
    float fraction = 0.0f;
};

inline Position positionForPhase(double phase, int length,
                                 Direction direction) noexcept
{
    length = juce::jmax(1, length);
    const int cycleLength = direction == Direction::bounce && length > 1
                          ? 2 * (length - 1)
                          : direction == Direction::random ? length * 8
                          : length;
    phase = std::fmod(phase, static_cast<double>(cycleLength));
    if (phase < 0.0)
        phase += static_cast<double>(cycleLength);
    const int tick = juce::jlimit(
        0, cycleLength - 1, static_cast<int>(std::floor(phase)));
    const auto offsetAt = [=](int value)
    {
        value = (value % cycleLength + cycleLength) % cycleLength;
        if (direction == Direction::reverse)
            return length - 1 - value;
        if (direction == Direction::bounce && value >= length)
            return cycleLength - value;
        if (direction == Direction::random)
            return randomOffset(value, length);
        return value;
    };
    return { offsetAt(tick), offsetAt(tick - 1),
             static_cast<float>(phase - std::floor(phase)) };
}

inline float transition(float previous, float current, float position,
                        float attack, float release) noexcept
{
    previous = juce::jlimit(0.0f, 1.0f, previous);
    current = juce::jlimit(0.0f, 1.0f, current);
    if (std::abs(current - previous) <= 0.0001f)
        return current;
    const float amount = juce::jlimit(
        0.0f, 1.0f, current >= previous ? attack : release);
    if (amount <= 0.000001f)
        return current;
    const float progress = juce::jlimit(0.0f, 1.0f, position / amount);
    const float eased = 1.0f - (1.0f - progress) * (1.0f - progress);
    return previous + eased * (current - previous);
}

inline float applyDepthOffset(float value, float minimum, float maximum,
                              float depth, float offsetAmount,
                              bool bipolar) noexcept
{
    value = juce::jlimit(0.0f, 1.0f, value);
    minimum = juce::jlimit(0.0f, 1.0f, minimum);
    maximum = juce::jlimit(minimum, 1.0f, maximum);
    depth = juce::jlimit(-1.0f, 1.0f, depth);
    offsetAmount = juce::jlimit(-1.0f, 1.0f, offsetAmount);
    const float lowerLimit = bipolar ? -1.0f : 0.0f;
    const auto toWorking = [lowerLimit](float input)
    {
        return juce::jlimit(lowerLimit, 1.0f, 2.0f * input - 1.0f);
    };
    const auto fromWorking = [](float input)
    {
        return juce::jlimit(0.0f, 1.0f, 0.5f * (input + 1.0f));
    };
    float workingValue = toWorking(value);
    float workingMinimum = toWorking(minimum);
    float workingMaximum = toWorking(maximum);
    const float excursion = bipolar
        ? juce::jmax(std::abs(workingMinimum),
                     std::abs(workingMaximum))
        : workingMaximum;
    float scale = depth <= 0.0f ? 1.0f + depth : 1.0f;
    if (depth > 0.0f && excursion > 0.000001f)
        scale = 1.0f + depth * (1.0f / excursion - 1.0f);
    workingValue *= scale;
    workingMinimum *= scale;
    workingMaximum *= scale;
    const float offset = offsetAmount >= 0.0f
        ? offsetAmount * (1.0f - workingMaximum)
        : (-offsetAmount) * (lowerLimit - workingMinimum);
    return fromWorking(workingValue + offset);
}

inline float evaluate(
    const std::array<float, stepCount>& steps,
    double phase, int firstStep, int lastStep, Direction direction,
    float attack, float release, int* activeStep = nullptr) noexcept
{
    firstStep = juce::jlimit(0, stepCount - 1, firstStep);
    lastStep = juce::jlimit(firstStep, stepCount - 1, lastStep);
    const auto position = positionForPhase(
        phase, lastStep - firstStep + 1, direction);
    const int currentStep = firstStep + position.current;
    const int previousStep = firstStep + position.previous;
    if (activeStep != nullptr)
        *activeStep = currentStep;
    return transition(steps[static_cast<size_t>(previousStep)],
                      steps[static_cast<size_t>(currentStep)],
                      position.fraction, attack, release);
}

inline std::array<float, stepCount> makeWaveform(Waveform waveform,
                                                 bool bipolar) noexcept
{
    std::array<float, stepCount> result {};
    const int preset = static_cast<int>(waveform);
    const bool doubled = (preset & 1) != 0;
    const int shape = preset / 2;
    const int period = doubled ? stepCount / 2 : stepCount;
    for (int step = 0; step < stepCount; ++step)
    {
        const int local = step % period;
        const float phase = static_cast<float>(local)
                          / static_cast<float>(period);
        float value = 0.0f;
        if (shape == 0)
            value = static_cast<float>(local) / static_cast<float>(period - 1);
        else if (shape == 1)
            value = 1.0f - static_cast<float>(local)
                          / static_cast<float>(period - 1);
        else if (shape == 2)
            value = 0.5f + 0.5f * std::sin(
                juce::MathConstants<float>::twoPi * phase);
        else if (shape == 3)
            value = 1.0f - std::abs(2.0f * phase - 1.0f);
        else if (shape == 4)
            value = phase < 0.25f ? 1.0f : 0.0f;
        else if (shape == 5)
            value = phase < 0.50f ? 1.0f : 0.0f;
        else
            value = phase < 0.75f ? 1.0f : 0.0f;
        result[static_cast<size_t>(step)] = bipolar
            ? juce::jlimit(0.0f, 1.0f, value)
            : 0.5f + 0.5f * juce::jlimit(0.0f, 1.0f, value);
    }
    return result;
}
}
