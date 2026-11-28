#pragma once

#include <JuceHeader.h>
#include "TransformSequencer.h"
#include "TransformModules.h"
#include "TransformTune.h"
#include <cstdint>
#include <array>
#include <optional>

class TransformAudioProcessor final : public juce::AudioProcessor, private juce::Timer
{
public:
    TransformAudioProcessor();
    ~TransformAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    using juce::AudioProcessor::processBlock;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlockBypassed;
    void processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    bool isBusesLayoutSupported(const BusesLayout&) const override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    // Internal power is an ordinary automatable parameter, not the VST3
    // host bypass flag. JUCE supplies the separate native bypass parameter.
    // Keep CC capture in both audio callbacks, including host bypass.
    double getTailLengthSeconds() const override { return std::max({ 15.0, static_cast<double>(freqTailSeconds.load(std::memory_order_relaxed)), 2.0 * moduleChain.latency() / currentSampleRate }); }
    int getEditorModule() const noexcept { return editorModule.load(std::memory_order_relaxed); }
    void setEditorModule(int index) noexcept
    { editorModule.store(juce::jlimit(0, static_cast<int>(transform::modules::count) - 1, index), std::memory_order_relaxed); }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    // MIDI learn is independent of preset state. All mapping edits and host
    // notifications run on the message thread; audio only captures CC bytes.
    bool isMidiLearnTarget(const juce::String& id) const;
    int getEditorZoomPercent() const noexcept { return editorZoomPercent.load(); }
    void setEditorZoomPercent(int percent) noexcept { editorZoomPercent.store(juce::jlimit(75, 300, percent)); }
    bool isSequencerColourLocked() const;
    bool toggleSequencerColourLock();
    void refreshUserSequences();
    const juce::Array<juce::File>& getUserSequenceFiles() const noexcept { return userSequenceFiles; }
    std::uint32_t getSequenceSelectionRevision() noexcept;
    int getSelectedWaveform() const noexcept { return selectedWaveform; }
    juce::String getSelectedUserSequence() const { return selectedUserSequence; }
    void selectWaveform(int index);
    bool selectUserSequence(int index);
    void armMidiLearn(const juce::String& id);
    void cancelMidiLearn();
    void clearMidiLearn(const juce::String& id);
    int getMidiLearnCC(const juce::String& id) const;
    juce::String getArmedMidiLearn() const { return midiArmedId; }
    juce::AudioProcessorValueTreeState parameters;
    static juce::AudioProcessorValueTreeState::ParameterLayout
        createParameterLayout();
    static juce::File getDataDirectory();
    static juce::File getSettingsDirectory();
    static juce::File getPresetsDirectory();
    static juce::File getSequencesDirectory();
    juce::Result savePresetToFile(const juce::File&);
    juce::Result loadPresetFromFile(const juce::File&);
    juce::File getCurrentPresetFile() const;
    std::optional<float> getSavedParameterValue(
        const juce::String& parameterId) const;
    float getTuneFrequency() const noexcept
    {
        return tuneFrequency.load(std::memory_order_relaxed);
    }
    int getTuneTarget() const noexcept { return tuneTarget.load(std::memory_order_relaxed); }
    float getTuneReference() const noexcept { return tuneReference.load(std::memory_order_relaxed); }
    std::uint32_t getTuneRevision() const noexcept
    {
        return tuneRevision.load(std::memory_order_acquire);
    }
    transform::freeze::Status getFreezeStatus() const noexcept
    { return freezeStatus.load(std::memory_order_relaxed); }
    float getDeEssReductionDb() const noexcept { return deEssReductionDb.load(std::memory_order_relaxed); }
    float getChannelReductionDb() const noexcept { return channelReductionDb.load(std::memory_order_relaxed); }
    int getActiveSequencerStep() const noexcept
    {
        return activeSequencerStep.load(std::memory_order_relaxed);
    }
    std::optional<float> getSequencerAnimatedNormalised(
        const juce::String& parameterId) const noexcept;

private:
    static constexpr int midiCCCount = 120; // Channel-mode CCs 120–127 are reserved.
    std::array<std::atomic<std::uint64_t>, midiCCCount> midiIncoming {};
    std::array<std::uint64_t, midiCCCount> midiConsumed {};
    std::array<juce::String, midiCCCount> midiMapping {};
    std::atomic<std::uint64_t> midiLatest { 0 };
    std::atomic<bool> midiCaptureEnabled { false };
    std::uint64_t midiSerial = 0; // Audio thread only.
    std::uint64_t midiArmAfter = 0;
    std::uint64_t midiLastDrained = 0;
    juce::String midiArmedId;
    void captureMidiCC(const juce::MidiBuffer&) noexcept;
    void timerCallback() override;
    void updateMidiCapture();
    void loadMidiMappings();
    void saveMidiMappings() const;
    std::atomic<int> editorModule { 0 };
    std::atomic<int> editorZoomPercent { 100 };
    std::array<std::atomic<float>, 3> defaultSequencerColour {};
    std::atomic<bool> hasDefaultSequencerColour { false };
    mutable std::atomic<bool> colourLockSuppressed { false };
    void loadDefaultInterfacePreferences();
    void applyDefaultSequencerColour();
    void applyMidiLearnValue(const juce::String&, int value);
    // Message-thread-only action selections; the actual steps live in APVTS.
    juce::Array<juce::File> userSequenceFiles;
    int selectedWaveform = -1;
    juce::String selectedUserSequence;
    juce::uint32 userSequenceRefreshTime = 0;
    std::uint32_t sequenceSelectionRevision = 0;
    std::atomic<bool> resetSequenceSelection { false };
    static constexpr int stateSchemaVersion = 5;
    static constexpr int channelCount = 2;
    using LinearSmoother = juce::SmoothedValue<
        float, juce::ValueSmoothingTypes::Linear>;

    struct ParameterPointers
    {
        std::array<std::atomic<float>*, transform::modules::count> moduleEnabled {};
        std::atomic<float>* pitchSemitones = nullptr;
        std::atomic<float>* pitchFine = nullptr;
        std::atomic<float>* pitchFormant = nullptr;
        std::array<std::atomic<float>*, 6> pitchExtra {};
        std::atomic<float>* pitchAlgorithm = nullptr;
        std::array<std::array<std::atomic<float>*, transform::harmony::controlsPerVoice>,
                   transform::harmony::voiceCount> harmony {};
        std::array<std::atomic<float>*, 5> freq {};
        std::atomic<float>* freqMode = nullptr;
        std::array<std::atomic<float>*, 4> freqExtra {};
        std::atomic<float>* freqSync = nullptr;
        std::atomic<float>* freqRelative = nullptr;
        std::atomic<float>* freqRelativeShift = nullptr;
        std::atomic<float>* freqDivision = nullptr;
        std::array<std::atomic<float>*, transform::harmony::voiceCount> harmonyPower {};
        std::atomic<float>* harmonyMode = nullptr;
        std::array<std::atomic<float>*, 4> freeze {};
        std::array<std::atomic<float>*, 4> freezeExtra {};
        std::atomic<float>* freezeMode = nullptr;
        std::atomic<float>* freezeDirection = nullptr;
        std::atomic<float>* freezeAlgorithm = nullptr;
        std::atomic<float>* freezeVoiceMode = nullptr;
        std::atomic<float>* freezeGlide = nullptr;
        std::array<std::atomic<float>*, 3> freezeCreative {};
        std::array<std::atomic<float>*, 5> morph {};
        std::array<std::atomic<float>*, 4> morphExtra {};
        std::atomic<float>* morphMode = nullptr;
        std::array<std::atomic<float>*, 6> vocoder {};
        std::array<std::atomic<float>*, 3> vocoderExtra {};
        std::array<std::atomic<float>*, 7> vocoderTone {};
        std::atomic<float>* vocoderCarrier = nullptr;
        std::atomic<float>* vocoderBands = nullptr;
        std::array<std::atomic<float>*, 7> deEss {};
        std::atomic<float>* deEssMode = nullptr;
        std::atomic<float>* deEssMonitor = nullptr;
        std::array<std::atomic<float>*, 12> channel {};
        std::atomic<float>* tuneKey = nullptr;
        std::atomic<float>* tuneScale = nullptr;
        std::atomic<float>* tuneAmount = nullptr;
        std::atomic<float>* tuneSpeed = nullptr;
        std::atomic<float>* tuneTolerance = nullptr;
        std::atomic<float>* tuneVibrato = nullptr;
        std::atomic<float>* tuneReference = nullptr;
        std::array<std::atomic<float>*, 12> tuneCustom {};
        std::atomic<float>* mix = nullptr;
        std::atomic<float>* output = nullptr;
        std::atomic<float>* bypass = nullptr;
        std::atomic<float>* sequencerEnabled = nullptr;
        std::atomic<float>* sequencerHostSync = nullptr;
        std::atomic<float>* sequencerRate = nullptr;
        std::atomic<float>* sequencerStart = nullptr;
        std::atomic<float>* sequencerEnd = nullptr;
        std::atomic<float>* sequencerDirection = nullptr;
        std::atomic<float>* sequencerBipolar = nullptr;
        std::atomic<float>* sequencerUnipolarNegative = nullptr;
        std::atomic<float>* sequencerAttack = nullptr;
        std::atomic<float>* sequencerRelease = nullptr;
        std::atomic<float>* sequencerPeak = nullptr;
        std::atomic<float>* sequencerMove = nullptr;
        std::atomic<float>* sequencerGateShortLength = nullptr;
        std::atomic<float>* sequencerGateLongLength = nullptr;
        std::atomic<float>* sequencerGateMode = nullptr;
        std::array<std::atomic<float>*, transform::sequencer::stepCount>
            sequencerSteps {};
        std::array<std::atomic<float>*, transform::sequencer::stepCount>
            sequencerGateSteps {};
        std::array<std::atomic<float>*, transform::sequencer::targetSlotCount>
            sequencerTargets {};
        std::array<std::atomic<float>*, transform::sequencer::targetSlotCount>
            sequencerTargetEnabled {};
        std::array<std::atomic<float>*, transform::sequencer::targetSlotCount>
            sequencerTargetDepth {};
    } raw;
    std::atomic<float> freqTailSeconds { 15.0f };

    void bindParameterPointers();
    void setSavedParameterState(const juce::ValueTree&);
    static float load(const std::atomic<float>*) noexcept;
    bool isSequencerTargetAssigned(int targetChoice) const noexcept;
    float sequencerTargetDepth(int targetChoice) const noexcept;
    float sequencerModulatedValue(
        int targetChoice, float baseValue, float canonicalSequenceValue,
        bool bipolar, bool unipolarNegative,
        juce::RangedAudioParameter* valueParameter = nullptr) const noexcept;

    static float sanitiseSample(float sample, float limit) noexcept;

    std::atomic<int> activeSequencerStep { -1 };
    double sequencerFreePhase = 0.0;
    int sequencerHeldNotes = 0;
    float previousSequencerCanonical = 1.0f;
    float sequencerGateGain = 1.0f;
    std::array<juce::RangedAudioParameter*, transform::sequencer::targets.size()>
        sequencerTargetParameters {};
    mutable std::array<std::atomic<float>, transform::sequencer::targets.size()>
        sequencerAnimatedNormalised {};

    double currentSampleRate = 44100.0;
    LinearSmoother mixValue;
    LinearSmoother outputGainValue;
    LinearSmoother bypassValue;
    transform::modules::Chain moduleChain;
    std::atomic<float> deEssReductionDb { 0.0f };
    std::atomic<float> channelReductionDb { 0.0f };
    std::atomic<transform::freeze::Status> freezeStatus { transform::freeze::Status::bypassed };
    transform::tune::Detector tuneDetector;
    transform::tune::Expression tuneExpression;
    transform::tune::HeldNotes tuneHeldNotes;
    std::atomic<int> tuneTarget { -1 };
    std::atomic<float> tuneReference { 440.0f };
    bool tuneWasEnabled = false;
    std::atomic<float> tuneFrequency { 0.0f };
    std::atomic<std::uint32_t> tuneRevision { 0 };
    void publishTuneFrequency(float hz) noexcept
    {
        tuneFrequency.store(hz, std::memory_order_relaxed);
        tuneRevision.fetch_add(1, std::memory_order_release);
    }

    mutable juce::CriticalSection savedParameterStateLock;
    juce::ValueTree savedParameterState;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TransformAudioProcessor)
};
