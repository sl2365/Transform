#pragma once

#include "TransformPitchRouting.h"
#include "TransformHarmony.h"
#include "TransformFreq.h"
#include "TransformFreeze.h"
#include "TransformMorph.h"
#include "TransformVocoder.h"
#include "TransformDeEss.h"
#include "TransformChannel.h"
#include <array>
#include <cstddef>

namespace transform::modules
{
enum class Id : std::size_t
{
    tune, pitch, freq, harmony, freeze, vocoder, morph, deEss, channel, count
};
inline constexpr std::size_t count = static_cast<std::size_t>(Id::count);
constexpr std::size_t index(Id id) noexcept { return static_cast<std::size_t>(id); }

struct Info
{
    const char* parameterId;
    const char* name;
    const char* description;
    const char* plannedControls;
};
inline constexpr std::array<Info, count> info {{
    { "tuneEnabled", "TUNE", "Detect notes and correct pitch to a selected scale.", "Chromatic tuner | Scale-based spectral correction" },
    { "pitchEnabled", "PITCH", "Transpose notes and change vocal resonances independently.", "Spectral, vocal and granular pitch shifting | Cascading feedback" },
    { "freqEnabled", "FREQ", "Displace frequencies by a fixed number of Hz.", "Shift in Hz | Stereo offset | Feedback | Sidebands" },
    { "harmonyEnabled", "HARMONY", "Add up to four pitched harmony voices to the lead.", "Four parallel voices | Interval | Pan | Level" },
    { "freezeEnabled", "FREEZE", "Capture audio as grains or pitched micro-loops, or convert rhythm to shaker percussion.", "Granular | Micro-loop | Shaker" },
    { "vocoderEnabled", "VOCODER", "Apply the input spectral envelope to a synth, noise or external carrier.", "8–48-band filter bank | MIDI synth | Noise | Sidechain" },
    { "morphEnabled", "MORPH", "Resynthesise detected notes with adjustable harmonics and sub-octave.", "Pitch-following additive resynthesis | Harmonics | Subharmonic" },
    { "deEssEnabled", "DE-ESS", "Reduce excessive sibilant frequencies in the input.", "Frequency | Threshold | Reduction | Attack and release" },
    { "channelEnabled", "CHANNEL", "Shape vocal dynamics, tone and harmonic saturation.", "Gate | Adjustable compressor | Low cut | Low, mid and high EQ | Presence | Saturation" }
}};

// Provisional serial order, independent of the order of editing tabs.
// DE-ESS conditions the input; CHANNEL is the final tonal/dynamics stage.
inline constexpr std::array<Id, count> signalOrder {{
    Id::deEss, Id::tune, Id::pitch, Id::harmony, Id::freeze,
    Id::morph, Id::vocoder, Id::freq, Id::channel
}};
using Enabled = std::array<bool, count>;

// TUNE and PITCH share a fixed-latency lead stage, with a corrected reference for PITCH BLEND.
class Chain
{
public:
    void prepare(double sampleRate, int channels) { pitchEngine.prepare(sampleRate, channels); analysisCache.prepare(pitchEngine.latency()); harmonyEngine.prepare(sampleRate, channels); freqEngine.prepare(sampleRate, channels); freezeEngine.prepare(sampleRate, channels); morphEngine.prepare(sampleRate, channels); vocoderEngine.prepare(sampleRate, channels, pitchEngine.latency()); deEssEngine.prepare(sampleRate, channels); channelEngine.prepare(sampleRate, channels); }
    void reset() noexcept { pitchEngine.reset(); harmonyEngine.reset(); freqEngine.reset(); freezeEngine.reset(); morphEngine.reset(); vocoderEngine.reset(); deEssEngine.reset(); channelEngine.reset(); }
    int latency() const noexcept { return pitchEngine.latency(); }
    void setEnabled(const Enabled& states) noexcept { enabled = states; }
    void setPitchControls(float semitones, float cents, float formant, bool masterEnabled, const pitch::Enhancements& enhancements = {},
                          float blend = 100.0f, float feedback = 0.0f, int algorithm = 0) noexcept
    {
        const bool active = masterEnabled && isEnabled(Id::pitch);
        pitchEngine.setControls(active, semitones, cents, formant, enhancements, blend, feedback, algorithm);
        // Alternator is centred around the corrected lead, not one of its two grain pitches.
        leadShift = active && algorithm != pitch::alternatorMode ? std::clamp(semitones, -24.0f, 24.0f) + 0.01f * std::clamp(cents, -100.0f, 100.0f) : 0.0f;
        leadFormant = active && algorithm == 0 ? formant : 0.0f;
    }
    void setHarmonyControls(const harmony::VoiceControls& values, bool masterEnabled) noexcept
    {
        harmonyEngine.setControls(masterEnabled && isEnabled(Id::harmony), values, leadShift, leadFormant);
    }
    float harmonyLeadShift() const noexcept { return leadShift; }
    void disableHarmony() noexcept { harmonyEngine.disable(); }
    void setFreqControls(const freq::Controls& values, bool masterEnabled) noexcept
    { freqEngine.setControls(masterEnabled && isEnabled(Id::freq), values); }
    void disableFreq() noexcept { freqEngine.disable(); }
    void setFreezeControls(const freeze::Controls& values, bool masterEnabled) noexcept
    { freezeEngine.setControls(masterEnabled && isEnabled(Id::freeze), values); }
    void disableFreeze() noexcept { freezeEngine.disable(); }
    void setMorphControls(const morph::Controls& values, bool masterEnabled) noexcept
    { morphEngine.setControls(masterEnabled && isEnabled(Id::morph), values); }
    void disableMorph() noexcept { morphEngine.disable(); }
    void setVocoderControls(const vocoder::Controls& values, bool masterEnabled) noexcept
    { vocoderEngine.setControls(masterEnabled && isEnabled(Id::vocoder), values); }
    void disableVocoder() noexcept { vocoderEngine.disable(); }
    void setDeEssControls(const deess::Controls& values, bool masterEnabled) noexcept
    { deEssEngine.setControls(masterEnabled && isEnabled(Id::deEss), values); }
    void disableDeEss() noexcept { deEssEngine.disable(); }
    float deEssReductionDb() const noexcept { return deEssEngine.reductionDb(); }
    void setChannelControls(const channel::Controls& values, bool masterEnabled) noexcept
    { channelEngine.setControls(masterEnabled && isEnabled(Id::channel), values); }
    void disableChannel() noexcept { channelEngine.disable(); }
    float channelReductionDb() const noexcept { return channelEngine.reductionDb(); }
    bool vocoderAcceptsMidi() const noexcept { return vocoderEngine.acceptsMidi(); }
    void vocoderMidiEvent(const std::uint8_t* data, int bytes) noexcept { vocoderEngine.midiEvent(data, bytes); }
    bool freezeAcceptsMidi() const noexcept { return freezeEngine.acceptsMidi(); }
    void freezeMidiEvent(const std::uint8_t* data, int bytes) noexcept { freezeEngine.midiEvent(data, bytes); }
    freeze::Status freezeStatus() const noexcept { return freezeEngine.status(); }
    void setTuneCorrection(float semitones, float speedMs) noexcept
    {
        pitchEngine.setCorrection(semitones, speedMs);
        harmonyEngine.setCorrection(semitones, speedMs);
    }
    bool isEnabled(Id id) const noexcept { return enabled[index(id)]; }
    void processFrame(float& left, float& right, float& dryL, float& dryR, float sideL = 0.0f, float sideR = 0.0f, bool sideAvailable = false) noexcept
    {
        deEssEngine.process(left, right);
        const float harmonyInputL = left, harmonyInputR = right;
        pitchEngine.process(left, right, dryL, dryR, &analysisCache);
        harmonyEngine.process(harmonyInputL, harmonyInputR, left, right, &analysisCache);
        freezeEngine.process(left, right);
        morphEngine.process(left, right);
        vocoderEngine.process(left, right, sideL, sideR, sideAvailable);
        freqEngine.process(left, right);
        channelEngine.process(left, right);
    }
private:
    Enabled enabled {};
    pitch::AnalysisCache analysisCache;
    pitch::Lead pitchEngine;
    harmony::Engine harmonyEngine;
    freq::Engine freqEngine;
    freeze::Engine freezeEngine;
    morph::Engine morphEngine;
    vocoder::Engine vocoderEngine;
    deess::Engine deEssEngine;
    channel::Engine channelEngine;
    float leadShift = 0.0f, leadFormant = 0.0f;
};
}
