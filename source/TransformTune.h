#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <initializer_list>
#include <xmmintrin.h>

namespace transform::tune
{
struct NoteReading
{
    int midi = 0;
    float cents = 0.0f;
};
inline NoteReading noteForFrequency(float hz, float reference = 440.0f) noexcept
{
    if (!std::isfinite(hz) || hz < 1.0f || hz > 20000.0f) return {};
    const double pitch = 69.0 + 12.0 * std::log2(static_cast<double>(hz) / std::clamp(std::isfinite(reference) ? reference : 440.0f, 400.0f, 480.0f));
    const int midi = std::clamp(static_cast<int>(std::lround(pitch)), 0, 127);
    return { midi, static_cast<float>((pitch - midi) * 100.0) };
}

// Equal-tempered scale quantisation. Tie breaks towards the lower note.
inline constexpr std::array<const char*, 12> keyNames {{
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
}};
inline constexpr std::array<const char*, 11> scaleNames {{
    "Chromatic", "Major", "Natural Minor", "Harmonic Minor",
    "Major Pentatonic", "Minor Pentatonic", "Blues", "Dorian", "Mixolydian", "Custom", "MIDI"
}};
constexpr unsigned notes(std::initializer_list<int> intervals) noexcept
{
    unsigned mask = 0;
    for (const int interval : intervals) mask |= 1u << interval;
    return mask;
}
inline constexpr std::array<unsigned, 9> scaleMasks {{
    0xfffu, notes({0,2,4,5,7,9,11}), notes({0,2,3,5,7,8,10}),
    notes({0,2,3,5,7,8,11}), notes({0,2,4,7,9}), notes({0,3,5,7,10}),
    notes({0,3,5,6,7,10}), notes({0,2,3,5,7,9,10}), notes({0,2,4,5,7,9,10})
}};
struct Correction
{
    int targetMidi = -1;
    float semitones = 0.0f;
};
inline constexpr int customScale = 9, midiScale = 10;
inline constexpr std::array<const char*, 12> customIds {{
    "tuneNoteC", "tuneNoteCs", "tuneNoteD", "tuneNoteDs", "tuneNoteE", "tuneNoteF",
    "tuneNoteFs", "tuneNoteG", "tuneNoteGs", "tuneNoteA", "tuneNoteAs", "tuneNoteB"
}};
// Channel-specific held-note state; note-off on one channel cannot clear another.
class HeldNotes
{
public:
    void reset() noexcept { held = {}; counts = {}; }
    bool event(const unsigned char* data, int bytes) noexcept
    {
        if (data == nullptr || bytes < 3) return false;
        const int command = data[0] & 0xf0, channel = data[0] & 15;
        const int note = data[1] & 127;
        if (command == 0xb0 && (note == 120 || note == 123))
        {
            for (int i = 0; i < 128; ++i)
                if (held[channel][i]) { held[channel][i] = false; --counts[i]; }
            return true;
        }
        if (command != 0x80 && command != 0x90) return false;
        const bool on = command == 0x90 && data[2] != 0;
        if (held[channel][note] == on) return false;
        held[channel][note] = on;
        counts[note] += on ? 1 : -1;
        return true;
    }
    bool contains(int note) const noexcept { return counts[note] > 0; }
private:
    std::array<std::array<bool, 128>, 16> held {};
    std::array<int, 128> counts {};
};
inline Correction correctionForFrequency(float hz, int key, int scale, float amount,
                                          float reference = 440.0f, unsigned customMask = 0xfffu,
                                          const HeldNotes* midi = nullptr) noexcept
{
    if (!std::isfinite(hz) || hz < 35.0f || hz > 1600.0f) return {};
    const double pitch = 69.0 + 12.0 * std::log2(static_cast<double>(hz) / std::clamp(std::isfinite(reference) ? reference : 440.0f, 400.0f, 480.0f));
    const int centre = static_cast<int>(std::floor(pitch));
    const auto mask = scale == customScale ? customMask : scaleMasks[static_cast<std::size_t>(std::clamp(scale, 0, static_cast<int>(scaleMasks.size()) - 1))];
    key = std::clamp(key, 0, 11);
    int best = -1;
    double distance = 100.0;
    const int radius = scale == midiScale ? 25 : 6;
    for (int note = std::max(0, centre - radius); note <= std::min(127, centre + radius); ++note)
    {
        if (scale == midiScale)
        {
            if (midi == nullptr || !midi->contains(note) || std::abs(note - pitch) > 24.0) continue;
        }
        else if ((mask & (1u << ((note - (scale == customScale ? 0 : key) + 12) % 12))) == 0) continue;
        const double error = std::abs(note - pitch);
        if (error + 1.0e-6 < distance) { best = note; distance = error; }
    }
    if (best < 0) return {};
    amount = std::clamp(std::isfinite(amount) ? amount : 0.0f, 0.0f, 100.0f);
    const float shift = static_cast<float>(best - pitch) * amount * 0.01f;
    // Ignore tiny corrections to avoid running a spectral shifter for detector noise.
    return { best, std::abs(shift) < 0.005f ? 0.0f : shift };
}

// Smooth the correction error, not the pitch itself. The residual fast
// modulation is retained in the input; reset when the selected note changes.
class Expression
{
public:
    void reset() noexcept { target = -1; mean = 0.0f; }
    float apply(Correction correction, float amount, float tolerance, float vibrato,
                bool fresh) noexcept
    {
        if (correction.targetMidi < 0) { reset(); return 0.0f; }
        const float error = correction.semitones;
        if (target != correction.targetMidi) { target = correction.targetMidi; mean = error; }
        else if (fresh) mean += 0.12967528f * (error - mean); // 25 ms / 180 ms
        const float preserve = std::clamp(vibrato, 0.0f, 100.0f) * 0.01f;
        float shift = preserve == 0.0f ? error : error + preserve * (mean - error);
        const float deadband = std::clamp(tolerance, 0.0f, 100.0f) * 0.01f;
        shift = std::copysign(std::max(0.0f, std::abs(shift) - deadband), shift);
        shift *= std::clamp(amount, 0.0f, 100.0f) * 0.01f;
        return std::abs(shift) < 0.005f ? 0.0f : shift;
    }
private:
    int target = -1;
    float mean = 0.0f;
};

// Monophonic input analysis only. Fixed-size storage, no audio modification,
// no allocations, and one bounded YIN analysis per ~25 ms of input.
class Detector
{
public:
    static constexpr float minimumHz = 35.0f;
    static constexpr float maximumHz = 1600.0f;

    void prepare(double sampleRate, bool bassRange = false) noexcept
    {
        lowerHz = bassRange ? 20.0f : minimumHz;
        upperHz = bassRange ? 500.0f : maximumHz;
        supported = std::isfinite(sampleRate) && sampleRate >= 16000.0
                    && sampleRate <= 384000.0;
        if (supported)
        {
            decimation = std::max(1, static_cast<int>(std::lround(sampleRate / 16000.0)));
            analysisRate = sampleRate / decimation;
            lowpass = static_cast<float>(1.0 - std::exp(-6.283185307179586 * 2300.0 / sampleRate));
            dcRate = static_cast<float>(1.0 - std::exp(-6.283185307179586 * 12.0 / analysisRate));
            minLag = std::max(2, static_cast<int>(std::floor(analysisRate / upperHz)));
            maxLag = static_cast<int>(std::ceil(analysisRate / lowerHz)) + 2;
            window = static_cast<int>(std::ceil(analysisRate * (bassRange ? 0.050 : 0.035)));
            hop = std::max(1, static_cast<int>(std::lround(analysisRate * 0.025)));
            supported = window + maxLag <= capacity && maxLag < capacity;
        }
        reset();
    }

    void reset() noexcept
    {
        channels = {};
        position = count = sinceAnalysis = decimationPhase = 0;
        frequency = 0.0f;
    }

    // Returns true only when a fresh estimate (including no-note) is ready.
    bool process(const float* left, const float* right, int samples) noexcept
    {
        if (!supported || left == nullptr || samples <= 0)
            return false;
        bool updated = false;
        for (int i = 0; i < samples; ++i)
        {
            const auto filter = [this](Channel& c, float x)
            {
                if (!std::isfinite(x)) x = 0.0f;
                x = std::clamp(x, -16.0f, 16.0f);
                c.lp1 += lowpass * (x - c.lp1);
                c.lp2 += lowpass * (c.lp1 - c.lp2);
                c.lp3 += lowpass * (c.lp2 - c.lp3);
            };
            filter(channels[0], left[i]);
            if (right != nullptr) filter(channels[1], right[i]);
            if (++decimationPhase < decimation) continue;
            decimationPhase = 0;
            const int channelCount = right != nullptr ? 2 : 1;
            for (int c = 0; c < channelCount; ++c)
            {
                auto& state = channels[static_cast<std::size_t>(c)];
                state.dc += dcRate * (state.lp3 - state.dc);
                state.ring[static_cast<std::size_t>(position)] = state.lp3 - state.dc;
            }
            position = (position + 1) % capacity;
            count = std::min(count + 1, capacity);
            ++sinceAnalysis;
            if (count >= window + maxLag && sinceAnalysis >= hop)
            {
                sinceAnalysis = 0;
                analyse(channelCount);
                updated = true;
            }
        }
        return updated;
    }

    float getFrequency() const noexcept { return frequency; }

private:
    static constexpr int capacity = 2048;
    struct Channel
    {
        std::array<float, capacity> ring {};
        float lp1 = 0.0f, lp2 = 0.0f, lp3 = 0.0f, dc = 0.0f;
    };
    std::array<Channel, 2> channels {};
    std::array<float, capacity> frame {}, difference {}, rawDifference {};
    bool supported = false;
    int decimation = 1, decimationPhase = 0, position = 0, count = 0;
    int window = 0, minLag = 0, maxLag = 0, hop = 0, sinceAnalysis = 0;
    double analysisRate = 16000.0;
    float lowerHz = minimumHz, upperHz = maximumHz;
    float lowpass = 0.0f, dcRate = 0.0f, frequency = 0.0f;

    void analyse(int channelCount) noexcept
    {
        frequency = 0.0f;
        // Analyse the louder input channel rather than summing stereo. This
        // also handles hard-panned or polarity-inverted monophonic signals.
        float bestEnergy = -1.0f;
        int chosen = 0;
        for (int c = 0; c < channelCount; ++c)
        {
            float energy = 0.0f, mean = 0.0f;
            const auto& ring = channels[static_cast<std::size_t>(c)].ring;
            for (int j = 0; j < window; ++j)
            {
                const float x = ring[static_cast<std::size_t>((position - window + j + capacity) % capacity)];
                energy += x * x;
                mean += x;
            }
            energy = energy / static_cast<float>(window)
                     - (mean / static_cast<float>(window)) * (mean / static_cast<float>(window));
            if (energy > bestEnergy) { bestEnergy = energy; chosen = c; }
        }
        if (bestEnergy < 1.0e-8f) return; // reject silence and nearly constant input
        const auto& ring = channels[static_cast<std::size_t>(chosen)].ring;
        const int length = window + maxLag;
        for (int j = 0; j < length; ++j)
            frame[static_cast<std::size_t>(j)] = ring[static_cast<std::size_t>((position - length + j + capacity) % capacity)];
        difference[0] = 1.0f;
        rawDifference[0] = 0.0f;
        float cumulative = 0.0f;
        int selectedLag = 0;
        for (int lag = 1; lag <= maxLag; ++lag)
        {
            float sum = 0.0f;
            // Compute four independent differences together, then accumulate
            // in the original order (no reassociation of the YIN sum).
            int j = maxLag;
            alignas(16) float squares[4];
            for (; j + 3 < length; j += 4)
            {
                const auto delta = _mm_sub_ps(_mm_loadu_ps(frame.data() + j), _mm_loadu_ps(frame.data() + j - lag));
                _mm_store_ps(squares, _mm_mul_ps(delta, delta));
                sum += squares[0]; sum += squares[1]; sum += squares[2]; sum += squares[3];
            }
            for (; j < length; ++j)
            {
                const float delta = frame[static_cast<std::size_t>(j)] - frame[static_cast<std::size_t>(j - lag)];
                sum += delta * delta;
            }
            rawDifference[static_cast<std::size_t>(lag)] = sum;
            cumulative += sum;
            difference[static_cast<std::size_t>(lag)] = cumulative > 1.0e-20f
                ? sum * static_cast<float>(lag) / cumulative : 1.0f;
            // Stop at the first sufficiently periodic trough, instead of
            // searching for a deeper octave-related minimum. Higher notes
            // therefore need far less analysis work than low notes/noise.
            if (lag > minLag
                && difference[static_cast<std::size_t>(lag - 1)] < 0.15f
                && difference[static_cast<std::size_t>(lag - 1)] <= difference[static_cast<std::size_t>(lag)])
            {
                selectedLag = lag - 1;
                break;
            }
        }
        if (selectedLag == 0) return;
        const int lag = selectedLag;
        const float a = rawDifference[static_cast<std::size_t>(lag - 1)];
        const float b = rawDifference[static_cast<std::size_t>(lag)];
        const float c = rawDifference[static_cast<std::size_t>(lag + 1)];
        const float curvature = a - 2.0f * b + c;
        const float adjustment = curvature > 1.0e-8f ? std::clamp(0.5f * (a - c) / curvature, -0.5f, 0.5f) : 0.0f;
        const float hz = static_cast<float>(analysisRate / (static_cast<double>(lag) + adjustment));
        if (std::isfinite(hz) && hz >= lowerHz && hz <= upperHz) frequency = hz;
    }
};
}
