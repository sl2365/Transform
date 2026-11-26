#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstring>
#include <vector>

namespace transform::pitch
{
struct Enhancements
{
    float preserve = 0.0f, glide = 0.0f, detune = 0.0f, transient = 0.0f;
};
// Per-instance analysis cache. Only reuse an identical windowed frame;
// each engine still owns its phase history, controls and overlap-add state.
struct AnalysisCache
{
    struct Frame
    {
        std::vector<std::complex<float>> input, spectrum;
        std::vector<float> magnitude;
        bool valid = false;
    };
    std::array<Frame, 2> channels;
    void prepare(int n)
    {
        for (auto& c : channels)
        {
            c.input.resize(n / 2);
            c.spectrum.resize(n / 2 + 1);
            c.magnitude.resize(n / 2 + 1);
            c.valid = false;
        }
    }
};
// Streaming phase vocoder. Fixed N-sample latency, including the neutral path.
// All storage, FFT tables and windows are prepared off the audio thread.
class Engine
{
public:
    void prepare(double sampleRate, int channels)
    {
        const double sr = std::isfinite(sampleRate) ? std::clamp(sampleRate, 8000.0, 384000.0) : 44100.0;
        n = 256;
        while (n < sr * 0.032 && n < 16384) n *= 2;
        hop = n / 4;
        hopSeconds = static_cast<float>(hop / sr);
        bins = n / 2 + 1;
        // Smooth over the harmonic comb with a sample-rate-independent
        // triangular spectral window (two bounded moving averages).
        envelopeRadius = std::clamp(static_cast<int>(std::lround(250.0 * n / sr)), 1, bins - 1);
        stereo = channels > 1;
        window.resize(n);
        reverse.resize(n / 2);
        twiddle.resize(n / 2);
        for (int i = 0; i < n; ++i)
        {
            window[i] = std::sqrt(0.5f - 0.5f * std::cos(twoPi * static_cast<float>(i) / static_cast<float>(n)));
            if (i < n / 2)
            {
                unsigned x = static_cast<unsigned>(i), y = 0;
                for (int bit = n / 2; bit > 1; bit >>= 1) { y = (y << 1) | (x & 1u); x >>= 1; }
                reverse[i] = y;
            }
        }
        for (int k = 0; k < n / 2; ++k)
        {
            const float a = -twoPi * static_cast<float>(k) / static_cast<float>(n);
            twiddle[k] = { std::cos(a), std::sin(a) };
        }
        for (auto& c : channel)
        {
            c.input.assign(n, 0.0f);
            c.dry.assign(n, 0.0f);
            c.ola.assign(n, 0.0f);
            c.fft.resize(bins);
            c.packed.resize(n / 2);
            c.previousSpectrum.assign(bins, Complex{});
            c.analysisSpectrum.resize(bins);
            c.phase.assign(bins, 0.0f);
            c.magnitude.resize(bins);
            c.frequency.resize(bins);
            c.live.assign(bins, false);
            c.envelope.resize(bins);
            c.prefix.resize(bins + 1);
            c.harmonyPeaks.resize(bins);
            c.lastMagnitude.assign(bins, 0.0f);
        }
        reset();
    }
    int latency() const noexcept { return n; }
    void reset() noexcept
    {
        position = elapsed = 0;
        blend = 0.0f;
        ratio = targetRatio = 1.0f;
        requested = running = manualRequested = false;
        manualRatio = 1.0f;
        formantTarget = formantCurrent = 0.0f;
        correctionTarget = correctionCurrent = 0.0f;
        lastSpeed = -1.0f;
        preserve = detune = transient = 0.0f;
        preserveCurrent = detuneCurrent = 0.0f;
        glideSlew = 1.0f; glideMs = 0.0f;
        manualEnabled = false;
        for (auto& c : channel)
        {
            std::fill(c.input.begin(), c.input.end(), 0.0f);
            std::fill(c.dry.begin(), c.dry.end(), 0.0f);
            std::fill(c.ola.begin(), c.ola.end(), 0.0f);
            std::fill(c.live.begin(), c.live.end(), false);
            c.first = true;
            c.haveFlux = false;
        }
    }
    void setControls(bool enabled, float semitones, float cents, float formant = 0.0f) noexcept
    {
        const float shift = std::clamp(std::isfinite(semitones) ? semitones : 0.0f, -24.0f, 24.0f)
                          + 0.01f * std::clamp(std::isfinite(cents) ? cents : 0.0f, -100.0f, 100.0f);
        manualEnabled = enabled;
        manualRequested = enabled && std::abs(shift) >= 0.0001f;
        manualRatio = manualRequested ? std::exp2(shift / 12.0f) : 1.0f;
        formantTarget = enabled ? std::clamp(std::isfinite(formant) ? formant : 0.0f, -24.0f, 24.0f) : 0.0f;
        refreshRequest();
    }
    void setEnhancements(bool enabled, const Enhancements& values) noexcept
    {
        const auto bounded = [](float v, float hi) noexcept
        { return std::clamp(std::isfinite(v) ? v : 0.0f, 0.0f, hi); };
        preserve = enabled ? bounded(values.preserve, 100.0f) * 0.01f : 0.0f;
        detune = enabled && stereo ? bounded(values.detune, 100.0f) : 0.0f;
        const float newTransient = enabled ? bounded(values.transient, 100.0f) * 0.01f : 0.0f;
        if (newTransient > 0.0f && transient == 0.0f)
            for (auto& c : channel) c.haveFlux = false;
        transient = newTransient;
        const float ms = enabled ? bounded(values.glide, 500.0f) : 0.0f;
        if (ms != glideMs)
        {
            glideMs = ms;
            // GLIDE specifies the time to traverse 99% of a pitch change.
            glideSlew = ms == 0.0f ? 1.0f : 1.0f - std::exp(-4.605170186f * hopSeconds * 1000.0f / ms);
        }
        refreshRequest();
    }
    // Harmony intervals are relative to the manual lead pitch, so their
    // combined range can exceed the PITCH control's +/- two octaves.
    void setHarmonyControls(float semitones, float formant) noexcept
    {
        manualEnabled = false;
        glideSlew = 0.5f; // smooth independent harmony voice interval changes
        const float shift = std::clamp(std::isfinite(semitones) ? semitones : 0.0f, -49.0f, 49.0f);
        manualRequested = std::abs(shift) >= 0.0001f;
        manualRatio = manualRequested ? std::exp2(shift / 12.0f) : 1.0f;
        formantTarget = std::clamp(std::isfinite(formant) ? formant : 0.0f, -24.0f, 24.0f);
        refreshRequest();
    }
    void setCorrection(float semitones, float speedMs) noexcept
    {
        correctionTarget = std::clamp(std::isfinite(semitones) ? semitones : 0.0f, -24.0f, 24.0f);
        const float speed = std::clamp(std::isfinite(speedMs) ? speedMs : 40.0f, 0.0f, 200.0f);
        if (speed != lastSpeed)
        {
            lastSpeed = speed;
            correctionSlew = speed <= 0.0f ? 1.0f : 1.0f - std::exp(-hopSeconds * 1000.0f / speed);
        }
        refreshRequest();
    }
    // dryIn is the original input; wetIn can include the sequencer input gate.
    // Both outputs are aligned to the same fixed latency.
    void process(float& wetL, float& wetR, float& dryL, float& dryR, AnalysisCache* cache = nullptr) noexcept
    {
        if (n == 0) return;
        if (requested && !running)
        {
            running = true;
            ratio = glideMs > 0.0f ? 1.0f : targetRatio;
            formantCurrent = formantTarget;
            preserveCurrent = preserve;
            detuneCurrent = detune;
            for (auto& c : channel) { c.first = true; c.haveFlux = false; std::fill(c.live.begin(), c.live.end(), false); }
        }
        const float step = 1.0f / static_cast<float>(n);
        blend = std::clamp(blend + (requested ? step : -step), 0.0f, 1.0f);
        processChannel(channel[0], wetL, dryL);
        if (stereo) processChannel(channel[1], wetR, dryR);
        else { wetR = wetL; dryR = dryL; }
        position = (position + 1) & (n - 1);
        if (++elapsed == hop)
        {
            elapsed = 0;
            if (running)
            {
                // One-pole frame-rate slew limits discontinuities on automation.
                // Smooth in musical pitch, not linear frequency ratio.
                if (glideSlew >= 1.0f) ratio = targetRatio;
                else ratio = std::exp2(std::log2(ratio) + glideSlew * (std::log2(targetRatio) - std::log2(ratio)));
                if (std::abs(ratio - targetRatio) < 0.0001f) ratio = targetRatio;
                refreshRequest();
                if (requested)
                {
                    // Release to neutral promptly when input is unvoiced, without
                    // changing the independent manual PITCH interval.
                    const float slew = correctionTarget == 0.0f ? 1.0f : correctionSlew;
                    correctionCurrent += slew * (correctionTarget - correctionCurrent);
                    if (std::abs(correctionCurrent - correctionTarget) < 0.0001f)
                        correctionCurrent = correctionTarget;
                }
                const float combined = correctionCurrent == 0.0f ? ratio
                    : ratio * std::exp2(correctionCurrent / 12.0f);
                formantCurrent += 0.5f * (formantTarget - formantCurrent);
                if (std::abs(formantCurrent - formantTarget) < 0.0001f)
                    formantCurrent = formantTarget;
                const float formantRatio = formantCurrent == 0.0f ? 1.0f : std::exp2(formantCurrent / 12.0f);
                preserveCurrent += 0.5f * (preserve - preserveCurrent);
                detuneCurrent += 0.5f * (detune - detuneCurrent);
                if (std::abs(preserveCurrent - preserve) < 0.0001f) preserveCurrent = preserve;
                if (std::abs(detuneCurrent - detune) < 0.0001f) detuneCurrent = detune;
                const float spread = detuneCurrent == 0.0f ? 1.0f : std::exp2(detuneCurrent / 2400.0f);
                const float leftRatio = combined / spread;
                const float rightRatio = combined * spread;
                const auto compensated = [&](float r) noexcept
                { return preserveCurrent == 0.0f ? formantRatio : formantRatio * std::pow(r, -preserveCurrent); };
                render(channel[0], leftRatio, compensated(leftRatio), cache ? &cache->channels[0] : nullptr);
                if (stereo) render(channel[1], rightRatio, compensated(rightRatio), cache ? &cache->channels[1] : nullptr);
            }
        }
        if (!requested && running && blend <= 0.0f)
        {
            running = false;
            correctionCurrent = 0.0f;
            formantCurrent = 0.0f;
            for (auto& c : channel) std::fill(c.ola.begin(), c.ola.end(), 0.0f);
        }
    }
private:
    using Complex = std::complex<float>;
    static constexpr float twoPi = 6.2831853071795864769f;
    struct Channel
    {
        std::vector<float> input, dry, ola, phase, magnitude, frequency;
        std::vector<Complex> fft, packed, previousSpectrum, analysisSpectrum;
        std::vector<float> envelope;
        std::vector<double> prefix;
        std::vector<int> harmonyPeaks;
        std::vector<float> lastMagnitude;
        std::vector<bool> live;
        bool first = true, haveFlux = false;
    };
    Channel channel[2];
    int n = 0, hop = 0, bins = 0, position = 0, elapsed = 0;
    int envelopeRadius = 1;
    bool stereo = true, requested = false, running = false;
    float blend = 0.0f, ratio = 1.0f, targetRatio = 1.0f;
    bool manualRequested = false, manualEnabled = false;
    float manualRatio = 1.0f, correctionTarget = 0.0f, correctionCurrent = 0.0f;
    float hopSeconds = 0.0f, lastSpeed = -1.0f, correctionSlew = 1.0f;
    float formantTarget = 0.0f, formantCurrent = 0.0f;
    float preserve = 0.0f, detune = 0.0f, transient = 0.0f, glideMs = 0.0f, glideSlew = 1.0f;
    float preserveCurrent = 0.0f, detuneCurrent = 0.0f;
    void refreshRequest() noexcept
    {
        targetRatio = manualRatio;
        const bool glidingToNeutral = manualEnabled && glideMs > 0.0f && std::abs(ratio - 1.0f) >= 0.0001f;
        requested = manualRequested || glidingToNeutral || correctionTarget != 0.0f || formantTarget != 0.0f || detune != 0.0f;
    }
    std::vector<float> window;
    std::vector<unsigned> reverse;
    std::vector<Complex> twiddle;

    static float wrap(float x) noexcept { return std::remainder(x, twoPi); }
    void processChannel(Channel& c, float& wet, float& dry) noexcept
    {
        const float delayed = c.input[position];
        const float shifted = c.ola[position];
        c.ola[position] = 0.0f;
        c.input[position] = std::isfinite(wet) ? wet : 0.0f;
        const float delayedDry = c.dry[position];
        c.dry[position] = std::isfinite(dry) ? dry : 0.0f;
        dry = delayedDry;
        wet = running ? delayed + (shifted - delayed) * blend : delayed;
    }
    void fft(std::vector<Complex>& a, bool inverse) noexcept
    {
        const int size = n / 2;
        for (int i = 0; i < size; ++i)
            if (static_cast<unsigned>(i) < reverse[i]) std::swap(a[i], a[reverse[i]]);
        for (int length = 2; length <= size; length *= 2)
        {
            const int half = length / 2, stride = n / length;
            for (int start = 0; start < size; start += length)
                for (int j = 0; j < half; ++j)
                {
                    const auto w = twiddle[j * stride];
                    const auto v = a[start + j + half] * (inverse ? std::conj(w) : w);
                    const auto u = a[start + j];
                    a[start + j] = u + v;
                    a[start + j + half] = u - v;
                }
        }
        // Inverse normalization is included in overlap-add below.
    }
    // Estimate the broad spectral envelope in power units. Prefix sums
    // make both smoothing passes O(bins), with no additional FFT or allocation.
    float estimateEnvelope(Channel& c) noexcept
    {
        c.prefix[0] = 0.0;
        for (int k = 0; k < bins; ++k)
            c.prefix[k + 1] = c.prefix[k] + static_cast<double>(std::norm(c.fft[k]));
        const auto smooth = [&]() noexcept
        {
            for (int k = 0; k < bins; ++k)
            {
                const int lo = std::max(0, k - envelopeRadius);
                const int hi = std::min(bins - 1, k + envelopeRadius);
                c.envelope[k] = static_cast<float>(std::max(0.0, c.prefix[hi + 1] - c.prefix[lo]) / (hi - lo + 1));
            }
        };
        smooth();
        for (int k = 0; k < bins; ++k)
            c.prefix[k + 1] = c.prefix[k] + c.envelope[k];
        smooth();
        const float peak = *std::max_element(c.envelope.begin(), c.envelope.end());
        return std::max(1.0e-20f, peak * 1.0e-6f);
    }
    float formantGain(const Channel& c, int k, float inverseFormantRatio, float floor) const noexcept
    {
        // Warp only the envelope, not the bin frequencies. Relative to the
        // transposed spectrum, the original spectral residual is retained.
        const float at = static_cast<float>(k) * inverseFormantRatio;
        const int lo = std::min(bins - 1, static_cast<int>(at));
        const int hi = std::min(bins - 1, lo + 1);
        const float frac = std::clamp(at - static_cast<float>(lo), 0.0f, 1.0f);
        const float desired = c.envelope[lo] + frac * (c.envelope[hi] - c.envelope[lo]);
        // Bound the gain (+/- 12 dB approximately) to avoid amplifying
        // unexcited spectral regions or numerical noise excessively.
        return std::sqrt(std::clamp((desired + floor) / (c.envelope[k] + floor), 0.0625f, 16.0f));
    }
    // Phase-locked peak-region translation. Move each spectral peak
    // together with its surrounding bins, preserving their relative phases
    // and bandwidth. Stretching the entire bin grid can cause cancellation
    // when adjacent components represent the same sinusoid.
    void renderPhaseLocked(Channel& c, float combinedRatio, float formantRatio) noexcept
    {
        const bool warpFormant = formantRatio != 1.0f;
        const float floor = warpFormant ? estimateEnvelope(c) : 0.0f;
        const float inverse = warpFormant ? 1.0f / formantRatio : 1.0f;
        constexpr float expectedPerBin = twoPi * 0.25f;
        float maxMag = 0.0f;
        for (int k = 0; k < bins; ++k)
        {
            c.analysisSpectrum[k] = c.fft[k];
            maxMag = std::max(maxMag, c.magnitude[k]);
        }
        int peakCount = 0;
        const float threshold = std::max(1.0e-8f, maxMag * 0.0001f);
        for (int k = 0; k < bins; ++k)
            if (c.magnitude[k] >= threshold
                && (k == 0 || c.magnitude[k] > c.magnitude[k - 1])
                && (k == bins - 1 || c.magnitude[k] >= c.magnitude[k + 1]))
                c.harmonyPeaks[peakCount++] = k;
        // Only peak frequencies are consumed by the synthesis stage. Keep
        // phase history for every bin, but avoid wrapping non-peak phases.
        for (int i = 0; i < peakCount; ++i)
        {
            const int p = c.harmonyPeaks[i];
            const float expected = expectedPerBin * static_cast<float>(p);
            const float ph = std::arg(c.analysisSpectrum[p]);
            c.frequency[p] = c.first ? expected : expected + wrap(ph - std::arg(c.previousSpectrum[p]) - expected);
        }
        c.previousSpectrum.swap(c.analysisSpectrum);
        std::fill(c.fft.begin(), c.fft.end(), Complex{});
        for (int i = 0; i < peakCount; ++i)
        {
            const int p = c.harmonyPeaks[i];
            const float advance = c.frequency[p] * combinedRatio;
            const int destination = static_cast<int>(std::lround(advance / expectedPerBin));
            if (destination < 0 || destination >= bins || advance < 0.0f
                || advance > expectedPerBin * static_cast<float>(bins - 1))
                continue;
            const int delta = destination - p;
            const float analysisPhase = std::arg(c.previousSpectrum[p]);
            const float seed = (std::abs(delta) % 2 != 0)
                ? wrap(analysisPhase + twoPi * 0.5f) : analysisPhase;
            c.phase[p] = c.first || !c.live[p] ? seed : wrap(c.phase[p] + advance);
            const int lo = i == 0 ? 0 : (c.harmonyPeaks[i - 1] + p) / 2 + 1;
            const int hi = i == peakCount - 1 ? bins - 1 : (p + c.harmonyPeaks[i + 1]) / 2;
            // All bins in a peak region share the same phase rotation.
            // Rotate the original complex bins instead of converting each
            // bin to polar coordinates and back. The phase-lock relationship
            // and magnitude/formant gain are mathematically unchanged.
            const float angle = c.phase[p] - analysisPhase;
            const float rotationR = std::cos(angle), rotationI = std::sin(angle);
            for (int k = lo; k <= hi; ++k)
            {
                const int dst = k + delta;
                if (dst < 0 || dst >= bins || c.magnitude[k] <= 1.0e-8f) continue;
                const float gain = warpFormant && k != 0 ? formantGain(c, k, inverse, floor) : 1.0f;
                const auto z = c.previousSpectrum[k];
                c.fft[dst] += Complex{ (z.real() * rotationR - z.imag() * rotationI) * gain,
                                       (z.real() * rotationI + z.imag() * rotationR) * gain };
            }
        }
        std::fill(c.live.begin(), c.live.end(), false);
        for (int i = 0; i < peakCount; ++i)
        {
            const int p = c.harmonyPeaks[i];
            const float advance = c.frequency[p] * combinedRatio;
            c.live[p] = advance >= 0.0f && advance <= expectedPerBin * static_cast<float>(bins - 1);
        }
        c.first = false;
        finishFrame(c);
    }
    void finishFrame(Channel& c) noexcept
    {
        // Reconstruct the packed even/odd time-domain transform from the
        // positive-frequency bins. The inverse is unnormalised (N/2).
        const int half = n / 2;
        c.packed[0] = { 0.5f * (c.fft[0].real() + c.fft[half].real()),
                        0.5f * (c.fft[0].real() - c.fft[half].real()) };
        for (int k = 1; k < half; ++k)
        {
            const auto a = c.fft[k], b = std::conj(c.fft[half - k]);
            const auto d = (a - b) * std::conj(twiddle[k]);
            c.packed[k] = 0.5f * (a + b + Complex{-d.imag(), d.real()});
        }
        fft(c.packed, true);
        const float norm = 1.0f / static_cast<float>(n); // four sqrt-Hann overlaps, N/2 inverse
        for (int i = 0; i < half; ++i)
        {
            const int j = 2 * i;
            c.ola[(position + j) & (n - 1)] += c.packed[i].real() * window[j] * norm;
            c.ola[(position + j + 1) & (n - 1)] += c.packed[i].imag() * window[j + 1] * norm;
        }
    }
    void render(Channel& c, float combinedRatio, float formantRatio, AnalysisCache::Frame* cache) noexcept
    {
        // A real N-point FFT needs only one N/2 complex FFT. Pack adjacent
        // even/odd windowed samples, then recover the nonredundant spectrum.
        const int half = n / 2;
        for (int i = 0; i < half; ++i)
        {
            const int j = 2 * i;
            c.packed[i] = { c.input[(position + j) & (n - 1)] * window[j],
                            c.input[(position + j + 1) & (n - 1)] * window[j + 1] };
        }
        const bool useCache = cache != nullptr && cache->input.size() == c.packed.size()
            && cache->spectrum.size() == c.fft.size() && cache->magnitude.size() == c.magnitude.size();
        const bool hit = useCache && cache->valid
            && std::memcmp(cache->input.data(), c.packed.data(), c.packed.size() * sizeof(Complex)) == 0;
        if (hit)
        {
            std::copy(cache->spectrum.begin(), cache->spectrum.end(), c.fft.begin());
            std::copy(cache->magnitude.begin(), cache->magnitude.end(), c.magnitude.begin());
        }
        else
        {
            if (useCache) std::copy(c.packed.begin(), c.packed.end(), cache->input.begin());
            fft(c.packed, false);
            c.fft[0] = { c.packed[0].real() + c.packed[0].imag(), 0.0f };
            c.fft[half] = { c.packed[0].real() - c.packed[0].imag(), 0.0f };
            for (int k = 1; k < half; ++k)
            {
                const auto a = c.packed[k], b = std::conj(c.packed[half - k]);
                const auto d = (a - b) * twiddle[k];
                c.fft[k] = 0.5f * (a + b + Complex{d.imag(), -d.real()});
            }
            for (int k = 0; k < bins; ++k) c.magnitude[k] = std::abs(c.fft[k]);
            if (useCache)
            {
                std::copy(c.fft.begin(), c.fft.end(), cache->spectrum.begin());
                std::copy(c.magnitude.begin(), c.magnitude.end(), cache->magnitude.begin());
                cache->valid = true;
            }
        }
        // Spectral flux detects new attacks; reseed synthesis phases without
        // discarding the analysis phase history needed for frequency estimation.
        if (transient > 0.0f)
        {
            double rise = 0.0, total = 0.0;
            for (int k = 0; k < bins; ++k)
            {
                const float mag = c.magnitude[k];
                rise += std::max(0.0f, mag - c.lastMagnitude[k]);
                total += mag;
                c.lastMagnitude[k] = mag;
            }
            if (c.haveFlux && total > 1.0e-6 && rise / total > 0.85 - 0.65 * transient)
                std::fill(c.live.begin(), c.live.end(), false);
            c.haveFlux = true;
        }
        renderPhaseLocked(c, combinedRatio, formantRatio);
    }
};
}
