#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>
#include <algorithm>

#if JUCE_WINDOWS
 #include <windows.h>
#endif

namespace
{
namespace ParameterIds
{
constexpr auto mix = "mix";
constexpr auto output = "output";
constexpr auto bypass = "bypass";
}

constexpr float smoothingSeconds = 0.020f;
constexpr std::array<const char*, 3> colourIds {{ "sequencerHue", "sequencerSaturation", "sequencerLuminance" }};
constexpr auto waveformLearnId = "sequencerWaveformAction";
constexpr auto userWaveformLearnId = "sequencerUserWaveformAction";

void setActual(juce::AudioProcessorValueTreeState& parameters, const juce::String& id, float value)
{
    if (auto* parameter = parameters.getParameter(id))
    {
        if (! std::isfinite(value)) return;
        const auto normalised = parameter->convertTo0to1(parameter->getNormalisableRange().snapToLegalValue(
            juce::jlimit(parameter->getNormalisableRange().start, parameter->getNormalisableRange().end, value)));
        if (parameter->getValue() == normalised) return;
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost(normalised);
        parameter->endChangeGesture();
    }
}


}

TransformAudioProcessor::TransformAudioProcessor()
    : AudioProcessor(BusesProperties()
          .withInput("Input", juce::AudioChannelSet::stereo(), true)
          .withInput("Sidechain", juce::AudioChannelSet::stereo(), false)
          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "TransformParameters", createParameterLayout())
{
    bindParameterPointers();
    for (auto& value : sequencerAnimatedNormalised)
        value.store(-1.0f, std::memory_order_relaxed);
    parameters.state.setProperty("schemaVersion", stateSchemaVersion, nullptr);
    getSettingsDirectory().createDirectory();
    getPresetsDirectory().createDirectory();
    getSequencesDirectory().createDirectory();
    loadDefaultInterfacePreferences();
    setSavedParameterState(parameters.copyState());
    refreshUserSequences();
    for (auto& event : midiIncoming) event.store(0, std::memory_order_relaxed);
    loadMidiMappings();
    updateMidiCapture();
}

TransformAudioProcessor::~TransformAudioProcessor()
{
    midiCaptureEnabled.store(false, std::memory_order_release);
    stopTimer();
}

void TransformAudioProcessor::loadMidiMappings()
{
    const auto file = getSettingsDirectory().getChildFile("MidiLearn.ini");
    juce::StringArray lines;
    lines.addLines(file.loadFileAsString());
    for (const auto& line : lines)
    {
        const auto id = line.upToFirstOccurrenceOf("=", false, false).trim();
        const auto ccText = line.fromFirstOccurrenceOf("=", false, false).trim();
        if (id.isEmpty() || ccText.isEmpty() || !ccText.containsOnly("0123456789")
            || !isMidiLearnTarget(id)) continue;
        const int cc = ccText.getIntValue();
        if (cc < 0 || cc >= midiCCCount) continue;
        for (auto& mapped : midiMapping) if (mapped == id) mapped.clear();
        midiMapping[static_cast<std::size_t>(cc)] = id;
    }
}

void TransformAudioProcessor::saveMidiMappings() const
{
    juce::String text = "[MidiLearn]\n";
    for (int cc = 0; cc < midiCCCount; ++cc)
        if (midiMapping[static_cast<std::size_t>(cc)].isNotEmpty())
            text += midiMapping[static_cast<std::size_t>(cc)] + "=" + juce::String(cc) + "\n";
    getSettingsDirectory().getChildFile("MidiLearn.ini").replaceWithText(text);
}

int TransformAudioProcessor::getMidiLearnCC(const juce::String& id) const
{
    if (id.isEmpty()) return -1;
    for (int cc = 0; cc < midiCCCount; ++cc)
        if (midiMapping[static_cast<std::size_t>(cc)] == id) return cc;
    return -1;
}

void TransformAudioProcessor::updateMidiCapture()
{
    const bool needed = midiArmedId.isNotEmpty() || std::any_of(midiMapping.begin(), midiMapping.end(),
        [](const juce::String& id) { return id.isNotEmpty(); });
    midiCaptureEnabled.store(needed, std::memory_order_release);
    if (needed) { if (!isTimerRunning()) startTimerHz(60); }
    else stopTimer();
}

void TransformAudioProcessor::armMidiLearn(const juce::String& id)
{
    if (!isMidiLearnTarget(id)) return;
    midiArmAfter = midiLatest.load(std::memory_order_acquire) >> 14;
    midiArmedId = id;
    updateMidiCapture();
}

void TransformAudioProcessor::cancelMidiLearn()
{
    midiArmedId.clear();
    updateMidiCapture();
}

void TransformAudioProcessor::clearMidiLearn(const juce::String& id)
{
    if (midiArmedId == id) midiArmedId.clear();
    const int cc = getMidiLearnCC(id);
    if (cc >= 0)
    {
        midiMapping[static_cast<std::size_t>(cc)].clear();
        saveMidiMappings();
    }
    updateMidiCapture();
}

void TransformAudioProcessor::captureMidiCC(const juce::MidiBuffer& midi) noexcept
{
    if (!midiCaptureEnabled.load(std::memory_order_acquire)) return;
    for (const auto event : midi)
    {
        if (event.numBytes != 3 || (event.data[0] & 0xf0) != 0xb0) continue;
        const int cc = event.data[1] & 0x7f;
        if (cc >= midiCCCount) continue;
        const auto packed = (++midiSerial << 14) | (static_cast<std::uint64_t>(cc) << 7)
            | static_cast<std::uint64_t>(event.data[2] & 0x7f);
        midiIncoming[static_cast<std::size_t>(cc)].store(packed, std::memory_order_release);
        midiLatest.store(packed, std::memory_order_release);
    }
}

void TransformAudioProcessor::timerCallback()
{
    const auto latest = midiLatest.load(std::memory_order_acquire);
    if (latest == midiLastDrained) return;
    midiLastDrained = latest;
    if (midiArmedId.isNotEmpty() && (latest >> 14) > midiArmAfter)
    {
        const auto cc = static_cast<std::size_t>((latest >> 7) & 0x7f);
        if (cc < midiMapping.size())
        {
            for (auto& id : midiMapping) if (id == midiArmedId) id.clear();
            midiMapping[cc] = midiArmedId;
            midiArmedId.clear();
            saveMidiMappings();
            // Apply the learning event even if this CC was already consumed.
            applyMidiLearnValue(midiMapping[cc], static_cast<int>(latest & 0x7f));
            // Do not apply the learning event twice in the drain below.
            midiConsumed[cc] = latest;
        }
    }
    for (std::size_t cc = 0; cc < midiIncoming.size(); ++cc)
    {
        const auto event = midiIncoming[cc].load(std::memory_order_acquire);
        if (event == midiConsumed[cc]) continue;
        midiConsumed[cc] = event;
        if (midiMapping[cc].isEmpty()) continue;
        applyMidiLearnValue(midiMapping[cc], static_cast<int>(event & 0x7f));
    }
}

bool TransformAudioProcessor::isMidiLearnTarget(const juce::String& id) const
{
    return id == waveformLearnId || id == userWaveformLearnId || parameters.getParameter(id) != nullptr;
}

void TransformAudioProcessor::loadDefaultInterfacePreferences()
{
    juce::StringArray lines;
    lines.addLines(getSettingsDirectory().getChildFile("Settings.ini").loadFileAsString());
    bool gui = false;
    for (auto line : lines)
    {
        line = line.trim();
        if (line.startsWithChar('[')) gui = line.equalsIgnoreCase("[GUI]");
        else if (gui && line.upToFirstOccurrenceOf("=", false, false).trim().equalsIgnoreCase("ScalePercent"))
            setEditorZoomPercent(line.fromFirstOccurrenceOf("=", false, false).trim().getIntValue());
    }
    const auto xml = juce::XmlDocument::parse(getSettingsDirectory().getChildFile("SequencerColour.xml"));
    if (xml == nullptr || !xml->hasTagName("SequencerColour")) return;
    std::array<float, 3> values {};
    for (std::size_t i = 0; i < values.size(); ++i)
    {
        if (!xml->hasAttribute(colourIds[i])) return;
        const auto value = xml->getDoubleAttribute(colourIds[i]);
        if (!std::isfinite(value) || value < (i == 2 ? 0.2 : 0.0) - 0.00001 || value > 1.0) return;
        values[i] = static_cast<float>(value);
    }
    for (std::size_t i = 0; i < values.size(); ++i) defaultSequencerColour[i].store(values[i]);
    hasDefaultSequencerColour.store(true);
    applyDefaultSequencerColour();
}

bool TransformAudioProcessor::isSequencerColourLocked() const
{
    if (!hasDefaultSequencerColour.load()) return false;
    for (std::size_t i = 0; i < colourIds.size(); ++i)
        if (std::abs(parameters.getRawParameterValue(colourIds[i])->load() - defaultSequencerColour[i].load()) > 0.00049f)
        {
            colourLockSuppressed.store(false);
            return false;
        }
    return !colourLockSuppressed.load();
}

void TransformAudioProcessor::applyDefaultSequencerColour()
{
    if (!hasDefaultSequencerColour.load()) return;
    for (std::size_t i = 0; i < colourIds.size(); ++i)
        setActual(parameters, colourIds[i], defaultSequencerColour[i].load());
}

bool TransformAudioProcessor::toggleSequencerColourLock()
{
    if (isSequencerColourLocked())
    {
        colourLockSuppressed.store(true);
        return true;
    }
    std::array<float, 3> values {};
    juce::XmlElement xml("SequencerColour");
    for (std::size_t i = 0; i < values.size(); ++i)
    {
        values[i] = parameters.getRawParameterValue(colourIds[i])->load();
        xml.setAttribute(colourIds[i], static_cast<double>(values[i]));
    }
    const juce::TemporaryFile temporary(getSettingsDirectory().getChildFile("SequencerColour.xml"));
    if (!xml.writeTo(temporary.getFile()) || !temporary.overwriteTargetFileWithTemporary()) return false;
    for (std::size_t i = 0; i < values.size(); ++i) defaultSequencerColour[i].store(values[i]);
    hasDefaultSequencerColour.store(true);
    colourLockSuppressed.store(false);
    return true;
}

std::uint32_t TransformAudioProcessor::getSequenceSelectionRevision() noexcept
{
    if (resetSequenceSelection.exchange(false))
    {
        selectedWaveform = -1;
        selectedUserSequence.clear();
        ++sequenceSelectionRevision;
    }
    return sequenceSelectionRevision;
}

void TransformAudioProcessor::refreshUserSequences()
{
    (void)getSequenceSelectionRevision();
    userSequenceRefreshTime = juce::Time::getMillisecondCounter();
    juce::Array<juce::File> files;
    getSequencesDirectory().findChildFiles(files, juce::File::findFiles, false, "*.transformseq");
    std::sort(files.begin(), files.end(), [](const juce::File& a, const juce::File& b)
    {
        return a.getFileNameWithoutExtension().compareNatural(b.getFileNameWithoutExtension(), true) < 0;
    });
    if (userSequenceFiles != files)
    {
        userSequenceFiles = std::move(files);
        ++sequenceSelectionRevision;
    }
    bool found = false;
    for (const auto& file : userSequenceFiles)
        if (file.getFullPathName() == selectedUserSequence) { found = true; break; }
    if (!found && selectedUserSequence.isNotEmpty())
    {
        selectedUserSequence.clear();
        ++sequenceSelectionRevision;
    }
}

void TransformAudioProcessor::selectWaveform(int index)
{
    (void)getSequenceSelectionRevision();
    if (index < 0 || index >= 14) return;
    const bool bipolar = parameters.getRawParameterValue("sequencerBipolar")->load() >= 0.5f;
    const auto values = transform::sequencer::makeWaveform(static_cast<transform::sequencer::Waveform>(index), bipolar);
    for (int i = 0; i < transform::sequencer::stepCount; ++i)
        setActual(parameters, transform::sequencer::stepParameterId(i), values[static_cast<std::size_t>(i)]);
    selectedWaveform = index;
    selectedUserSequence.clear();
    ++sequenceSelectionRevision;
}

bool TransformAudioProcessor::selectUserSequence(int index)
{
    (void)getSequenceSelectionRevision();
    if (!juce::isPositiveAndBelow(index, userSequenceFiles.size())) return false;
    const auto file = userSequenceFiles[index];
    const auto xml = juce::XmlDocument::parse(file);
    if (xml == nullptr || !xml->hasTagName("TransformSequence")) return false;
    if (xml->hasAttribute("gateShort")) setActual(parameters, "sequencerGateShortLength", static_cast<float>(xml->getDoubleAttribute("gateShort", 0.5)));
    if (xml->hasAttribute("gateLong")) setActual(parameters, "sequencerGateLongLength", static_cast<float>(xml->getDoubleAttribute("gateLong", 0.9)));
    for (auto* step : xml->getChildWithTagNameIterator("Step"))
    {
        const auto i = step->getIntAttribute("index", -1);
        if (!juce::isPositiveAndBelow(i, transform::sequencer::stepCount)) continue;
        setActual(parameters, transform::sequencer::stepParameterId(i), static_cast<float>(step->getDoubleAttribute("value", 1.0)));
        if (step->hasAttribute("gate")) setActual(parameters, transform::sequencer::gateStepParameterId(i), static_cast<float>(step->getDoubleAttribute("gate", 3.0)));
    }
    selectedUserSequence = file.getFullPathName();
    selectedWaveform = -1;
    ++sequenceSelectionRevision;
    return true;
}

void TransformAudioProcessor::applyMidiLearnValue(const juce::String& id, int value)
{
    (void)getSequenceSelectionRevision();
    value = juce::jlimit(0, 127, value);
    if (id == waveformLearnId)
    {
        const int index = value * 14 / 128;
        if (index != selectedWaveform) selectWaveform(index);
        return;
    }
    if (id == userWaveformLearnId)
    {
        if (juce::Time::getMillisecondCounter() - userSequenceRefreshTime >= 1000u) refreshUserSequences();
        const auto count = userSequenceFiles.size();
        if (count == 0) return;
        const auto index = static_cast<int>(static_cast<std::int64_t>(value) * count / 128);
        if (userSequenceFiles[index].getFullPathName() != selectedUserSequence) selectUserSequence(index);
        return;
    }
    if (auto* parameter = parameters.getParameter(id))
    {
        const float snapped = parameter->convertTo0to1(parameter->convertFrom0to1(static_cast<float>(value) / 127.0f));
        if (parameter->getValue() != snapped) parameter->setValueNotifyingHost(snapped);
        if (id == colourIds[0] || id == colourIds[1] || id == colourIds[2]) (void)isSequencerColourLocked();
    }
}

juce::File TransformAudioProcessor::getDataDirectory()
{
#if JUCE_WINDOWS
    std::array<wchar_t, 32768> modulePath {};
    const auto moduleHandle = static_cast<HMODULE>(
        juce::Process::getCurrentModuleInstanceHandle());
    if (moduleHandle != nullptr)
    {
        const auto pathLength = ::GetModuleFileNameW(
            moduleHandle, modulePath.data(),
            static_cast<DWORD>(modulePath.size()));
        if (pathLength > 0 && pathLength < modulePath.size() - 1)
        {
            const juce::File moduleFile(juce::String(modulePath.data()));
            auto folder = moduleFile.getParentDirectory();
            for (int depth = 0; depth < 6; ++depth)
            {
                if (folder.hasFileExtension(".vst3"))
                    return folder.getParentDirectory().getChildFile(juce::String(JucePlugin_Name) + "_Data");
                const auto parent = folder.getParentDirectory();
                if (parent == folder)
                    break;
                folder = parent;
            }
            return moduleFile.getParentDirectory().getChildFile(juce::String(JucePlugin_Name) + "_Data");
        }
    }
#endif
    return juce::File::getSpecialLocation(juce::File::currentApplicationFile)
        .getParentDirectory().getChildFile(juce::String(JucePlugin_Name) + "_Data");
}

juce::File TransformAudioProcessor::getSettingsDirectory()
{
    return getDataDirectory().getChildFile("Settings");
}

juce::File TransformAudioProcessor::getPresetsDirectory()
{
    return getDataDirectory().getChildFile("Presets");
}

juce::File TransformAudioProcessor::getSequencesDirectory()
{
    return getDataDirectory().getChildFile("Sequences");
}

juce::Result TransformAudioProcessor::savePresetToFile(
    const juce::File& presetFile)
{
    if (presetFile == juce::File())
        return juce::Result::fail("No preset file was selected.");

    const auto parent = presetFile.getParentDirectory();
    const auto root = getPresetsDirectory();
    if (parent != root && ! parent.isAChildOf(root))
        return juce::Result::fail(
            "Presets must be saved inside the Transform Presets folder.");
    if (! parent.isDirectory() && parent.createDirectory().failed())
        return juce::Result::fail("The preset folder could not be created.");

    auto state = parameters.copyState();
    state.setProperty("schemaVersion", stateSchemaVersion, nullptr);
    state.removeProperty("currentPresetPath", nullptr);
    const auto xml = state.createXml();
    if (xml == nullptr)
        return juce::Result::fail("The plugin state could not be encoded.");

    const juce::TemporaryFile temporaryFile(presetFile);
    if (! temporaryFile.getFile().replaceWithText(xml->toString())
        || ! temporaryFile.overwriteTargetFileWithTemporary())
        return juce::Result::fail("The preset could not be written safely.");

    parameters.state.setProperty(
        "currentPresetPath", presetFile.getFullPathName(), nullptr);
    setSavedParameterState(state);
    return juce::Result::ok();
}

juce::Result TransformAudioProcessor::loadPresetFromFile(
    const juce::File& presetFile)
{
    if (! presetFile.existsAsFile())
        return juce::Result::fail("The preset file no longer exists.");
    const auto parent = presetFile.getParentDirectory();
    const auto root = getPresetsDirectory();
    if (parent != root && ! parent.isAChildOf(root))
        return juce::Result::fail(
            "Presets must be loaded from the Transform Presets folder.");

    const auto xml = juce::XmlDocument::parse(presetFile);
    if (xml == nullptr || ! xml->hasTagName("TransformParameters"))
        return juce::Result::fail("This is not a Transform preset.");
    auto restored = juce::ValueTree::fromXml(*xml);
    if (static_cast<int>(restored.getProperty("schemaVersion", 0))
        != stateSchemaVersion)
        return juce::Result::fail(
            "This preset does not use the current Transform schema.");

    const bool keepDefaultColour = isSequencerColourLocked();
    restored.removeProperty("currentPresetPath", nullptr);
    restored.removeProperty("editorZoomPercent", nullptr);
    restored.removeProperty("colourLockSuppressed", nullptr);
    setSavedParameterState(restored);
    parameters.replaceState(restored);
    colourLockSuppressed.store(false);
    if (keepDefaultColour) applyDefaultSequencerColour();
    resetSequenceSelection.store(true);
    parameters.state.setProperty(
        "currentPresetPath", presetFile.getFullPathName(), nullptr);
    return juce::Result::ok();
}

juce::File TransformAudioProcessor::getCurrentPresetFile() const
{
    const auto path = parameters.state.getProperty(
        "currentPresetPath", juce::String()).toString();
    return path.isEmpty() ? juce::File() : juce::File(path);
}

void TransformAudioProcessor::setSavedParameterState(const juce::ValueTree& state)
{
    const juce::ScopedLock lock(savedParameterStateLock);
    savedParameterState = state.createCopy();
    savedParameterState.removeProperty("currentPresetPath", nullptr);
}

std::optional<float> TransformAudioProcessor::getSavedParameterValue(
    const juce::String& parameterId) const
{
    const juce::ScopedLock lock(savedParameterStateLock);
    for (int index = 0; index < savedParameterState.getNumChildren(); ++index)
    {
        const auto child = savedParameterState.getChild(index);
        if (child.getProperty("id").toString() != parameterId)
            continue;
        const float value = child.getProperty("value").toString()
            .getFloatValue();
        return std::isfinite(value) ? std::optional<float>(value)
                                    : std::nullopt;
    }
    return std::nullopt;
}

juce::AudioProcessorValueTreeState::ParameterLayout
TransformAudioProcessor::createParameterLayout()
{
    using APF = juce::AudioParameterFloat;
    using APB = juce::AudioParameterBool;
    using APC = juce::AudioParameterChoice;
    using API = juce::AudioParameterInt;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> layout;

    const auto percentageAttributes = juce::AudioParameterFloatAttributes()
        .withLabel("%");
    const auto targetDepthAttributes = juce::AudioParameterFloatAttributes()
        .withStringFromValueFunction([](float value, int)
        {
            return juce::String(juce::roundToInt(value * 100.0f)) + "%";
        })
        .withValueFromStringFunction([](const juce::String& text)
        {
            return text.getFloatValue() * 0.01f;
        });
    auto outputRange = juce::NormalisableRange<float>(-24.0f, 12.0f, 0.1f);
    outputRange.setSkewForCentre(0.0f);

    for (const auto& module : transform::modules::info)
        layout.push_back(std::make_unique<APB>(
            juce::ParameterID(module.parameterId, 1),
            juce::String(module.name) + " Enabled", false));
    layout.push_back(std::make_unique<APF>(
        juce::ParameterID(ParameterIds::mix, 1), "Mix",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 35.0f,
        percentageAttributes));
    layout.push_back(std::make_unique<APF>(
        juce::ParameterID(ParameterIds::output, 1), "Output", outputRange,
        0.0f, juce::AudioParameterFloatAttributes().withLabel("dB")));
    layout.push_back(std::make_unique<APB>(
        juce::ParameterID(ParameterIds::bypass, 1), "Internal Bypass", false));

    layout.push_back(std::make_unique<APB>(
        juce::ParameterID("sequencerEnabled", 1), "Sequencer Enabled", true));
    layout.push_back(std::make_unique<APB>(
        juce::ParameterID("sequencerHostSync", 1), "Sequencer Host Sync", true));
    layout.push_back(std::make_unique<APC>(
        juce::ParameterID("sequencerRate", 1), "Sequencer Rate",
        juce::StringArray { "1/128", "1/64T", "1/64", "1/32T", "1/32",
            "1/16T", "1/16", "1/8T", "1/8", "1/4T", "1/4", "1/2T",
            "1/2", "1/1" }, 6));
    layout.push_back(std::make_unique<API>(
        juce::ParameterID("sequencerStart", 1), "Sequencer Start", 1,
        transform::sequencer::stepCount - 1, 1));
    layout.push_back(std::make_unique<API>(
        juce::ParameterID("sequencerEnd", 1), "Sequencer End", 2,
        transform::sequencer::stepCount, transform::sequencer::stepCount));
    layout.push_back(std::make_unique<APB>(
        juce::ParameterID("sequencerRangeLink", 1),
        "Sequencer Range Link", false));
    layout.push_back(std::make_unique<APC>(
        juce::ParameterID("sequencerDirection", 1), "Sequencer Direction",
        juce::StringArray { "Loop", "Bounce", "Reverse", "Played", "Random" },
        0));
    layout.push_back(std::make_unique<APB>(
        juce::ParameterID("sequencerBipolar", 1), "Sequencer Bipolar", false));
    layout.push_back(std::make_unique<APB>(
        juce::ParameterID("sequencerUnipolarNegative", 1),
        "Sequencer Unipolar Negative", false));
    layout.push_back(std::make_unique<APF>(
        juce::ParameterID("sequencerAttack", 1), "Sequencer Attack",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.0f,
        targetDepthAttributes));
    layout.push_back(std::make_unique<APF>(
        juce::ParameterID("sequencerRelease", 1), "Sequencer Release",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.0f,
        targetDepthAttributes));
    layout.push_back(std::make_unique<APF>(
        juce::ParameterID("sequencerPeak", 1), "Sequencer Depth",
        juce::NormalisableRange<float>(-1.0f, 1.0f, 0.001f), 0.0f));
    layout.push_back(std::make_unique<APF>(
        juce::ParameterID("sequencerMove", 1), "Sequencer Offset",
        juce::NormalisableRange<float>(-1.0f, 1.0f, 0.001f), 0.0f));
    layout.push_back(std::make_unique<APF>(
        juce::ParameterID("sequencerHue", 1), "Sequencer Hue",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.5f));
    layout.push_back(std::make_unique<APF>(
        juce::ParameterID("sequencerSaturation", 1), "Sequencer Saturation",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.5f));
    layout.push_back(std::make_unique<APF>(
        juce::ParameterID("sequencerLuminance", 1), "Sequencer Luminance",
        juce::NormalisableRange<float>(0.2f, 1.0f, 0.001f), 0.5f));

    for (int step = 0; step < transform::sequencer::stepCount; ++step)
        layout.push_back(std::make_unique<APF>(
            juce::ParameterID(transform::sequencer::stepParameterId(step), 1),
            "Sequencer Step " + juce::String(step + 1),
            juce::NormalisableRange<float>(0.0f, 1.0f, 0.0001f), 1.0f));

    juce::StringArray targetNames { "None" };
    for (const auto& target : transform::sequencer::targets)
        targetNames.add(target.displayName);
    for (int slot = 0; slot < transform::sequencer::targetSlotCount; ++slot)
    {
        layout.push_back(std::make_unique<APC>(
            juce::ParameterID(transform::sequencer::targetParameterId(slot), 1),
            "Sequencer Target " + juce::String(slot + 1), targetNames, 0));
        layout.push_back(std::make_unique<APB>(
            juce::ParameterID(
                transform::sequencer::targetEnabledParameterId(slot), 1),
            "Sequencer Target " + juce::String(slot + 1) + " Enabled", true));
        layout.push_back(std::make_unique<APF>(
            juce::ParameterID(
                transform::sequencer::targetDepthParameterId(slot), 1),
            "Sequencer Target " + juce::String(slot + 1) + " Depth",
            juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 1.0f,
            targetDepthAttributes));
    }

    layout.push_back(std::make_unique<APF>(
        juce::ParameterID("sequencerGateShortLength", 1),
        "Sequencer Gate Short Length",
        juce::NormalisableRange<float>(0.10f, 0.60f, 0.01f), 0.50f,
        targetDepthAttributes));
    layout.push_back(std::make_unique<APF>(
        juce::ParameterID("sequencerGateLongLength", 1),
        "Sequencer Gate Long Length",
        juce::NormalisableRange<float>(0.65f, 0.95f, 0.01f), 0.90f,
        targetDepthAttributes));
    for (int step = 0; step < transform::sequencer::stepCount; ++step)
        layout.push_back(std::make_unique<APC>(
            juce::ParameterID(transform::sequencer::gateStepParameterId(step), 1),
            "Sequencer Gate Step " + juce::String(step + 1),
            juce::StringArray { "Off", "Short", "Long", "Rest" },
            static_cast<int>(transform::sequencer::GateStepMode::rest)));
    layout.push_back(std::make_unique<APC>(
        juce::ParameterID("sequencerGateMode", 1), "Sequencer Gate Mode",
        juce::StringArray { "Input", "Effect", "Output", "Off" }, 0));

    // Append new parameters; preserve existing parameter IDs and order.
    layout.push_back(std::make_unique<APF>(
        juce::ParameterID("pitchSemitones", 1), "Pitch Shift",
        juce::NormalisableRange<float>(-24.0f, 24.0f, 1.0f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel("st")));
    layout.push_back(std::make_unique<APF>(
        juce::ParameterID("pitchFine", 1), "Pitch Fine",
        juce::NormalisableRange<float>(-100.0f, 100.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel("ct")));
    juce::StringArray tuneKeys, tuneScales;
    for (const auto* name : transform::tune::keyNames) tuneKeys.add(name);
    for (const auto* name : transform::tune::scaleNames) tuneScales.add(name);
    layout.push_back(std::make_unique<APC>(
        juce::ParameterID("tuneKey", 1), "Tune Key", tuneKeys, 0));
    layout.push_back(std::make_unique<APC>(
        juce::ParameterID("tuneScale", 1), "Tune Scale", tuneScales, 0));
    layout.push_back(std::make_unique<APF>(
        juce::ParameterID("tuneAmount", 1), "Tune Amount",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 0.0f, percentageAttributes));
    layout.push_back(std::make_unique<APF>(
        juce::ParameterID("tuneSpeed", 1), "Tune Speed",
        juce::NormalisableRange<float>(0.0f, 200.0f, 1.0f), 40.0f,
        juce::AudioParameterFloatAttributes().withLabel("ms")));
    layout.push_back(std::make_unique<APF>(
        juce::ParameterID("pitchFormant", 1), "Pitch Formant",
        juce::NormalisableRange<float>(-24.0f, 24.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel("st")));
    for (std::size_t i = 0; i < transform::harmony::voiceCount; ++i)
    {
        const auto& voice = transform::harmony::info[i];
        const auto name = juce::String("Harmony ") + juce::String(static_cast<int>(i + 1));
        layout.push_back(std::make_unique<APB>(juce::ParameterID(transform::harmony::powerIds[i], 1), name + " Enabled", true));
        layout.push_back(std::make_unique<APF>(
            juce::ParameterID(voice.intervalId, 1), name + " Interval",
            juce::NormalisableRange<float>(-24.0f, 24.0f, 1.0f), voice.defaultInterval,
            juce::AudioParameterFloatAttributes().withLabel("st")));
        layout.push_back(std::make_unique<APF>(
            juce::ParameterID(voice.panId, 1), name + " Pan",
            juce::NormalisableRange<float>(-100.0f, 100.0f, 1.0f), 0.0f, percentageAttributes));
        layout.push_back(std::make_unique<APF>(
            juce::ParameterID(voice.levelId, 1), name + " Level",
            juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 0.0f, percentageAttributes));
        layout.push_back(std::make_unique<APF>(juce::ParameterID(voice.formantId, 1), name + " Formant",
            juce::NormalisableRange<float>(-24.0f, 24.0f, 0.1f), 0.0f, juce::AudioParameterFloatAttributes().withLabel("st")));
        layout.push_back(std::make_unique<APF>(juce::ParameterID(voice.humanId, 1), name + " Humanise",
            juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 0.0f, percentageAttributes));
        layout.push_back(std::make_unique<APF>(juce::ParameterID(voice.delayId, 1), name + " Delay",
            juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 0.0f, juce::AudioParameterFloatAttributes().withLabel("ms")));
    }
    juce::StringArray harmonyModes;
    for (const auto* name : transform::harmony::modeNames) harmonyModes.add(name);
    layout.push_back(std::make_unique<APC>(juce::ParameterID("harmonyMode", 1), "Harmony Mode", harmonyModes, 0));
    layout.push_back(std::make_unique<APF>(
        juce::ParameterID("freqShift", 1), "Freq Shift",
        juce::NormalisableRange<float>(-2000.0f, 2000.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel("Hz")));
    layout.push_back(std::make_unique<APF>(
        juce::ParameterID("freqOffset", 1), "Freq Stereo Offset",
        juce::NormalisableRange<float>(-1000.0f, 1000.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel("Hz")));
    layout.push_back(std::make_unique<APF>(
        juce::ParameterID("freqFeedback", 1), "Freq Feedback",
        juce::NormalisableRange<float>(0.0f, 95.0f, 0.1f), 0.0f, percentageAttributes));
    layout.push_back(std::make_unique<APF>(
        juce::ParameterID("freqDelay", 1), "Freq Feedback Delay",
        juce::NormalisableRange<float>(1.0f, 100.0f, 0.1f), 20.0f,
        juce::AudioParameterFloatAttributes().withLabel("ms")));
    layout.push_back(std::make_unique<APF>(
        juce::ParameterID("freqBlend", 1), "Freq Blend",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 100.0f, percentageAttributes));
    juce::StringArray freqModes;
    for (const auto* name : transform::freq::modeNames) freqModes.add(name);
    layout.push_back(std::make_unique<APC>(
        juce::ParameterID("freqMode", 1), "Freq Sideband", freqModes, 0));
    layout.push_back(std::make_unique<APF>(juce::ParameterID("freqModRate", 1), "Freq Mod Rate",
        juce::NormalisableRange<float>(0.05f, 20.0f, 0.01f, 0.4f), 1.0f, juce::AudioParameterFloatAttributes().withLabel("Hz")));
    layout.push_back(std::make_unique<APF>(juce::ParameterID("freqModDepth", 1), "Freq Mod Depth",
        juce::NormalisableRange<float>(0.0f, 1000.0f, 0.1f, 0.5f), 0.0f, juce::AudioParameterFloatAttributes().withLabel("Hz")));
    layout.push_back(std::make_unique<APF>(juce::ParameterID("freqPhase", 1), "Freq Phase",
        juce::NormalisableRange<float>(-180.0f, 180.0f, 1.0f), 0.0f, juce::AudioParameterFloatAttributes().withLabel("deg")));
    layout.push_back(std::make_unique<APF>(juce::ParameterID("freqCutoff", 1), "Freq Feedback LP",
        juce::NormalisableRange<float>(100.0f, 20000.0f, 1.0f, 0.4f), 12000.0f, juce::AudioParameterFloatAttributes().withLabel("Hz")));
    layout.push_back(std::make_unique<APB>(juce::ParameterID("freqSync", 1), "Freq Delay Sync", false));
    layout.push_back(std::make_unique<APB>(juce::ParameterID("freqRelative", 1), "Freq Relative Shift", false));
    layout.push_back(std::make_unique<APF>(juce::ParameterID("freqRelativeShift", 1), "Freq Relative Amount",
        juce::NormalisableRange<float>(-200.0f, 200.0f, 0.1f), 0.0f, percentageAttributes));
    layout.push_back(std::make_unique<APF>(juce::ParameterID("freqDivision", 1), "Freq Delay Division",
        juce::NormalisableRange<float>(1.0f, 8.0f, 1.0f), 3.0f));
    layout.push_back(std::make_unique<APF>(
        juce::ParameterID("freezePitch", 1), "Freeze Pitch",
        juce::NormalisableRange<float>(-24.0f, 24.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel("st")));
    layout.push_back(std::make_unique<APF>(
        juce::ParameterID("freezeSize", 1), "Freeze Size",
        juce::NormalisableRange<float>(20.0f, 500.0f, 1.0f), 100.0f,
        juce::AudioParameterFloatAttributes().withLabel("ms")));
    layout.push_back(std::make_unique<APF>(
        juce::ParameterID("freezePosition", 1), "Freeze Position",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 0.0f, percentageAttributes));
    layout.push_back(std::make_unique<APF>(
        juce::ParameterID("freezeBlend", 1), "Freeze Blend",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 100.0f, percentageAttributes));
    layout.push_back(std::make_unique<APF>(juce::ParameterID("freezeDensity", 1), "Freeze Density",
        juce::NormalisableRange<float>(2.0f, 8.0f, 1.0f), 2.0f, juce::AudioParameterFloatAttributes().withLabel("gr")));
    layout.push_back(std::make_unique<APF>(juce::ParameterID("freezeSmooth", 1), "Freeze Smooth",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 100.0f, percentageAttributes));
    layout.push_back(std::make_unique<APF>(juce::ParameterID("freezeDrift", 1), "Freeze Drift",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 0.0f, percentageAttributes));
    layout.push_back(std::make_unique<APF>(juce::ParameterID("freezeCapture", 1), "Freeze Capture",
        juce::NormalisableRange<float>(100.0f, 8000.0f, 1.0f, 0.5f), 2000.0f, juce::AudioParameterFloatAttributes().withLabel("ms")));
    layout.push_back(std::make_unique<APF>(juce::ParameterID("freezeFormant", 1), "Freeze Formant",
        juce::NormalisableRange<float>(-24.0f, 24.0f, 0.1f), 0.0f, juce::AudioParameterFloatAttributes().withLabel("st")));
    layout.push_back(std::make_unique<APF>(juce::ParameterID("freezeAttack", 1), "Freeze Attack",
        juce::NormalisableRange<float>(1.0f, 500.0f, 1.0f, 0.5f), 15.0f, juce::AudioParameterFloatAttributes().withLabel("ms")));
    layout.push_back(std::make_unique<APF>(juce::ParameterID("freezeRelease", 1), "Freeze Release",
        juce::NormalisableRange<float>(1.0f, 1000.0f, 1.0f, 0.5f), 15.0f, juce::AudioParameterFloatAttributes().withLabel("ms")));
    layout.push_back(std::make_unique<APF>(juce::ParameterID("freezeGlide", 1), "Freeze Glide",
        juce::NormalisableRange<float>(0.0f, 500.0f, 1.0f, 0.5f), 0.0f, juce::AudioParameterFloatAttributes().withLabel("ms")));
    juce::StringArray freezeVoiceModes;
    for (const auto* name : transform::freeze::voiceModeNames) freezeVoiceModes.add(name);
    layout.push_back(std::make_unique<APC>(juce::ParameterID("freezeVoiceMode", 1), "Freeze Voice Mode", freezeVoiceModes, 0));
    juce::StringArray freezeAlgorithms;
    for (const auto* name : transform::freeze::algorithmNames) freezeAlgorithms.add(name);
    layout.push_back(std::make_unique<APC>(juce::ParameterID("freezeAlgorithm", 1), "Freeze Algorithm", freezeAlgorithms, 0));
    juce::StringArray freezeModes, freezeDirections;
    for (const auto* name : transform::freeze::modeNames) freezeModes.add(name);
    for (const auto* name : transform::freeze::directionNames) freezeDirections.add(name);
    layout.push_back(std::make_unique<APC>(
        juce::ParameterID("freezeMode", 1), "Freeze Mode", freezeModes, 0));
    layout.push_back(std::make_unique<APC>(
        juce::ParameterID("freezeDirection", 1), "Freeze Direction", freezeDirections, 0));
    layout.push_back(std::make_unique<APF>(
        juce::ParameterID("morphHarmonics", 1), "Morph Harmonics",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 50.0f, percentageAttributes));
    layout.push_back(std::make_unique<APF>(
        juce::ParameterID("morphTimbre", 1), "Morph Timbre",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 50.0f, percentageAttributes));
    layout.push_back(std::make_unique<APF>(
        juce::ParameterID("morphSub", 1), "Morph Sub",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 0.0f, percentageAttributes));
    layout.push_back(std::make_unique<APF>(
        juce::ParameterID("morphResponse", 1), "Morph Response",
        juce::NormalisableRange<float>(5.0f, 200.0f, 1.0f), 30.0f, juce::AudioParameterFloatAttributes().withLabel("ms")));
    layout.push_back(std::make_unique<APF>(
        juce::ParameterID("morphBlend", 1), "Morph Blend",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 0.0f, percentageAttributes));
    layout.push_back(std::make_unique<APF>(
        juce::ParameterID("morphDensity", 1), "Morph Density",
        juce::NormalisableRange<float>(1.0f, 16.0f, 1.0f), 6.0f));
    layout.push_back(std::make_unique<APF>(
        juce::ParameterID("morphSmear", 1), "Morph Smear",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 0.0f, percentageAttributes));
    layout.push_back(std::make_unique<APF>(
        juce::ParameterID("morphModRate", 1), "Morph Mod Rate",
        juce::NormalisableRange<float>(0.05f, 20.0f, 0.01f, 0.5f), 1.0f, juce::AudioParameterFloatAttributes().withLabel("Hz")));
    layout.push_back(std::make_unique<APF>(
        juce::ParameterID("morphModDepth", 1), "Morph Mod Depth",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 0.0f, percentageAttributes));
    juce::StringArray morphModes;
    for (const auto* name : transform::morph::modeNames) morphModes.add(name);
    layout.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID("morphMode", 1), "Morph Mode", morphModes, 0));
    layout.push_back(std::make_unique<APF>(
        juce::ParameterID("vocoderAttack", 1), "Vocoder Attack",
        juce::NormalisableRange<float>(1.0f, 100.0f, 1.0f), 5.0f, juce::AudioParameterFloatAttributes().withLabel("ms")));
    layout.push_back(std::make_unique<APF>(
        juce::ParameterID("vocoderRelease", 1), "Vocoder Release",
        juce::NormalisableRange<float>(10.0f, 500.0f, 1.0f), 45.0f, juce::AudioParameterFloatAttributes().withLabel("ms")));
    layout.push_back(std::make_unique<APF>(
        juce::ParameterID("vocoderFormant", 1), "Vocoder Formant",
        juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f), 0.0f, juce::AudioParameterFloatAttributes().withLabel("st")));
    layout.push_back(std::make_unique<APF>(
        juce::ParameterID("vocoderWidth", 1), "Vocoder Width",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 100.0f, percentageAttributes));
    layout.push_back(std::make_unique<APF>(
        juce::ParameterID("vocoderNoise", 1), "Vocoder Noise",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 0.0f, percentageAttributes));
    layout.push_back(std::make_unique<APF>(
        juce::ParameterID("vocoderBlend", 1), "Vocoder Blend",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 0.0f, percentageAttributes));
    layout.push_back(std::make_unique<APF>(
        juce::ParameterID("vocoderBandwidth", 1), "Vocoder Bandwidth",
        juce::NormalisableRange<float>(50.0f, 200.0f, 0.1f), 100.0f, percentageAttributes));
    layout.push_back(std::make_unique<APF>(
        juce::ParameterID("vocoderDetune", 1), "Vocoder Detune",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 0.0f, juce::AudioParameterFloatAttributes().withLabel("ct")));
    layout.push_back(std::make_unique<APF>(
        juce::ParameterID("vocoderUnvoiced", 1), "Vocoder Unvoiced",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 65.0f, percentageAttributes));
    layout.push_back(std::make_unique<APF>(
        juce::ParameterID("vocoderShift", 1), "Vocoder Shift",
        juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f), 0.0f, juce::AudioParameterFloatAttributes().withLabel("st")));
    layout.push_back(std::make_unique<APF>(
        juce::ParameterID("vocoderContour", 1), "Vocoder Contour",
        juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f), 0.0f, juce::AudioParameterFloatAttributes().withLabel("dB")));
    layout.push_back(std::make_unique<APF>(
        juce::ParameterID("vocoderCompand", 1), "Vocoder Compand",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 0.0f, percentageAttributes));
    layout.push_back(std::make_unique<APF>(
        juce::ParameterID("vocoderModIn", 1), "Vocoder Modulator Input",
        juce::NormalisableRange<float>(-24.0f, 24.0f, 0.1f), 0.0f, juce::AudioParameterFloatAttributes().withLabel("dB")));
    layout.push_back(std::make_unique<APF>(
        juce::ParameterID("vocoderCarIn", 1), "Vocoder Carrier Input",
        juce::NormalisableRange<float>(-24.0f, 24.0f, 0.1f), 0.0f, juce::AudioParameterFloatAttributes().withLabel("dB")));
    layout.push_back(std::make_unique<APF>(
        juce::ParameterID("vocoderHarmonic", 1), "Vocoder Harmonic",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 0.0f, percentageAttributes));
    layout.push_back(std::make_unique<APF>(
        juce::ParameterID("vocoderSub", 1), "Vocoder Sub",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 0.0f, percentageAttributes));
    juce::StringArray vocoderCarriers, vocoderBandChoices;
    for (const auto* name : transform::vocoder::carrierNames) vocoderCarriers.add(name);
    for (const auto* name : transform::vocoder::bandNames) vocoderBandChoices.add(name);
    layout.push_back(std::make_unique<APC>(juce::ParameterID("vocoderCarrier", 1), "Vocoder Carrier", vocoderCarriers, 0));
    layout.push_back(std::make_unique<APC>(juce::ParameterID("vocoderBands", 1), "Vocoder Bands", vocoderBandChoices, 3));
    layout.push_back(std::make_unique<APF>(
        juce::ParameterID("deEssFrequency", 1), "De-ess Frequency",
        juce::NormalisableRange<float>(2000.0f, 12000.0f, 1.0f), 6500.0f, juce::AudioParameterFloatAttributes().withLabel("Hz")));
    layout.push_back(std::make_unique<APF>(
        juce::ParameterID("deEssThreshold", 1), "De-ess Threshold",
        juce::NormalisableRange<float>(-60.0f, 0.0f, 0.1f), -24.0f, juce::AudioParameterFloatAttributes().withLabel("dB")));
    layout.push_back(std::make_unique<APF>(
        juce::ParameterID("deEssRange", 1), "De-ess Range",
        juce::NormalisableRange<float>(0.0f, 24.0f, 0.1f), 0.0f, juce::AudioParameterFloatAttributes().withLabel("dB")));
    layout.push_back(std::make_unique<APF>(
        juce::ParameterID("deEssAttack", 1), "De-ess Attack",
        juce::NormalisableRange<float>(0.1f, 20.0f, 0.1f), 2.0f, juce::AudioParameterFloatAttributes().withLabel("ms")));
    layout.push_back(std::make_unique<APF>(
        juce::ParameterID("deEssRelease", 1), "De-ess Release",
        juce::NormalisableRange<float>(10.0f, 500.0f, 1.0f), 80.0f, juce::AudioParameterFloatAttributes().withLabel("ms")));
    layout.push_back(std::make_unique<APF>(
        juce::ParameterID("deEssBandwidth", 1), "De-ess Bandwidth",
        juce::NormalisableRange<float>(25.0f, 200.0f, 0.1f), 100.0f, percentageAttributes));
    layout.push_back(std::make_unique<APF>(
        juce::ParameterID("deEssAdaptive", 1), "De-ess Adaptive",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 0.0f, percentageAttributes));
    juce::StringArray deEssModes, deEssMonitors;
    for (const auto* name : transform::deess::modeNames) deEssModes.add(name);
    for (const auto* name : transform::deess::monitorNames) deEssMonitors.add(name);
    layout.push_back(std::make_unique<APC>(juce::ParameterID("deEssMode", 1), "De-ess Mode", deEssModes, 0));
    layout.push_back(std::make_unique<APC>(juce::ParameterID("deEssMonitor", 1), "De-ess Monitor", deEssMonitors, 0));
    layout.push_back(std::make_unique<APF>(
        juce::ParameterID("channelGate", 1), "Channel Gate",
        juce::NormalisableRange<float>(-80.0f, -20.0f, 0.1f), -80.0f, juce::AudioParameterFloatAttributes().withLabel("dB")));
    layout.push_back(std::make_unique<APF>(
        juce::ParameterID("channelComp", 1), "Channel Compressor",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 0.0f, percentageAttributes));
    layout.push_back(std::make_unique<APF>(
        juce::ParameterID("channelLowCut", 1), "Channel Low Cut",
        juce::NormalisableRange<float>(0.0f, 300.0f, 1.0f), 0.0f, juce::AudioParameterFloatAttributes().withLabel("Hz")));
    layout.push_back(std::make_unique<APF>(
        juce::ParameterID("channelLow", 1), "Channel Low",
        juce::NormalisableRange<float>(-24.0f, 24.0f, 0.1f), 0.0f, juce::AudioParameterFloatAttributes().withLabel("dB")));
    layout.push_back(std::make_unique<APF>(
        juce::ParameterID("channelMid", 1), "Channel Mid",
        juce::NormalisableRange<float>(-24.0f, 24.0f, 0.1f), 0.0f, juce::AudioParameterFloatAttributes().withLabel("dB")));
    layout.push_back(std::make_unique<APF>(
        juce::ParameterID("channelHigh", 1), "Channel High",
        juce::NormalisableRange<float>(-24.0f, 24.0f, 0.1f), 0.0f, juce::AudioParameterFloatAttributes().withLabel("dB")));
    layout.push_back(std::make_unique<APF>(
        juce::ParameterID("channelPresence", 1), "Channel Presence",
        juce::NormalisableRange<float>(-24.0f, 24.0f, 0.1f), 0.0f, juce::AudioParameterFloatAttributes().withLabel("dB")));
    layout.push_back(std::make_unique<APF>(juce::ParameterID("channelThreshold", 1), "Channel Threshold",
        juce::NormalisableRange<float>(-60.0f, 0.0f, 0.1f), -42.0f, juce::AudioParameterFloatAttributes().withLabel("dB")));
    layout.push_back(std::make_unique<APF>(juce::ParameterID("channelRatio", 1), "Channel Ratio",
        juce::NormalisableRange<float>(1.0f, 20.0f, 0.1f), 8.0f, juce::AudioParameterFloatAttributes().withLabel(":1")));
    layout.push_back(std::make_unique<APF>(juce::ParameterID("channelAttack", 1), "Channel Attack",
        juce::NormalisableRange<float>(0.1f, 100.0f, 0.1f), 3.0f, juce::AudioParameterFloatAttributes().withLabel("ms")));
    layout.push_back(std::make_unique<APF>(juce::ParameterID("channelRelease", 1), "Channel Release",
        juce::NormalisableRange<float>(10.0f, 1000.0f, 1.0f), 100.0f, juce::AudioParameterFloatAttributes().withLabel("ms")));
    layout.push_back(std::make_unique<APF>(juce::ParameterID("channelSaturation", 1), "Channel Saturation",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 0.0f, percentageAttributes));
    layout.push_back(std::make_unique<APF>(juce::ParameterID("tuneTolerance", 1), "Tune Tolerance",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 0.0f, juce::AudioParameterFloatAttributes().withLabel("ct")));
    layout.push_back(std::make_unique<APF>(juce::ParameterID("tuneVibrato", 1), "Tune Vibrato",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 0.0f, percentageAttributes));
    layout.push_back(std::make_unique<APF>(juce::ParameterID("tuneReference", 1), "Tune Reference",
        juce::NormalisableRange<float>(400.0f, 480.0f, 0.1f), 440.0f, juce::AudioParameterFloatAttributes().withLabel("Hz")));
    for (std::size_t i = 0; i < transform::tune::customIds.size(); ++i)
        layout.push_back(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID(transform::tune::customIds[i], 1),
            juce::String("Tune Note ") + transform::tune::keyNames[i], true));
    layout.push_back(std::make_unique<APF>(juce::ParameterID("pitchPreserve", 1), "Pitch Preserve",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 0.0f, percentageAttributes));
    layout.push_back(std::make_unique<APF>(juce::ParameterID("pitchGlide", 1), "Pitch Glide",
        juce::NormalisableRange<float>(0.0f, 500.0f, 1.0f), 0.0f, juce::AudioParameterFloatAttributes().withLabel("ms")));
    layout.push_back(std::make_unique<APF>(juce::ParameterID("pitchDetune", 1), "Pitch Detune",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 0.0f, juce::AudioParameterFloatAttributes().withLabel("ct")));
    layout.push_back(std::make_unique<APF>(juce::ParameterID("pitchTransient", 1), "Pitch Transient",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 0.0f, percentageAttributes));
    layout.push_back(std::make_unique<APF>(juce::ParameterID("pitchBlend", 1), "Pitch Blend",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 100.0f, percentageAttributes));
    layout.push_back(std::make_unique<APF>(juce::ParameterID("pitchFeedback", 1), "Pitch Feedback",
        juce::NormalisableRange<float>(0.0f, 95.0f, 0.1f), 0.0f, percentageAttributes));
    juce::StringArray pitchAlgorithms;
    for (const auto* name : transform::pitch::algorithmNames) pitchAlgorithms.add(name);
    layout.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID("pitchAlgorithm", 1), "Pitch Algorithm", pitchAlgorithms, 0));
    return { layout.begin(), layout.end() };
}

void TransformAudioProcessor::bindParameterPointers()
{
    const auto bind = [this](const juce::String& id)
    {
        auto* value = parameters.getRawParameterValue(id);
        jassert(value != nullptr);
        return value;
    };
    for (size_t i = 0; i < transform::modules::count; ++i)
        raw.moduleEnabled[i] = bind(transform::modules::info[i].parameterId);
    raw.pitchSemitones = bind("pitchSemitones");
    raw.pitchFine = bind("pitchFine");
    raw.pitchFormant = bind("pitchFormant");
    raw.pitchExtra = { bind("pitchPreserve"), bind("pitchGlide"), bind("pitchDetune"), bind("pitchTransient"),
                       bind("pitchBlend"), bind("pitchFeedback") };
    raw.pitchAlgorithm = bind("pitchAlgorithm");
    for (std::size_t i = 0; i < transform::harmony::voiceCount; ++i)
    {
        const auto& voice = transform::harmony::info[i];
        raw.harmonyPower[i] = bind(transform::harmony::powerIds[i]);
        raw.harmony[i] = { bind(voice.intervalId), bind(voice.panId), bind(voice.levelId), bind(voice.formantId), bind(voice.humanId), bind(voice.delayId) };
    }
    raw.freq = { bind("freqShift"), bind("freqOffset"), bind("freqFeedback"), bind("freqDelay"), bind("freqBlend") };
    raw.freqMode = bind("freqMode");
    raw.freqExtra = { bind("freqModRate"), bind("freqModDepth"), bind("freqPhase"), bind("freqCutoff") };
    raw.freqSync = bind("freqSync");
    raw.freqRelative = bind("freqRelative");
    raw.freqRelativeShift = bind("freqRelativeShift");
    raw.freqDivision = bind("freqDivision");
    raw.harmonyMode = bind("harmonyMode");
    raw.freeze = { bind("freezePitch"), bind("freezeSize"), bind("freezePosition"), bind("freezeBlend") };
    raw.freezeExtra = { bind("freezeDensity"), bind("freezeSmooth"), bind("freezeDrift"), bind("freezeCapture") };
    raw.freezeMode = bind("freezeMode");
    raw.freezeDirection = bind("freezeDirection");
    raw.freezeAlgorithm = bind("freezeAlgorithm");
    raw.freezeVoiceMode = bind("freezeVoiceMode"); raw.freezeGlide = bind("freezeGlide");
    raw.freezeCreative = { bind("freezeFormant"), bind("freezeAttack"), bind("freezeRelease") };
    raw.morph = { bind("morphHarmonics"), bind("morphTimbre"), bind("morphSub"), bind("morphResponse"), bind("morphBlend") };
    raw.morphExtra = { bind("morphDensity"), bind("morphSmear"), bind("morphModRate"), bind("morphModDepth") };
    raw.morphMode = bind("morphMode");
    raw.vocoder = { bind("vocoderAttack"), bind("vocoderRelease"), bind("vocoderFormant"), bind("vocoderWidth"), bind("vocoderNoise"), bind("vocoderBlend") };
    raw.vocoderExtra = { bind("vocoderBandwidth"), bind("vocoderDetune"), bind("vocoderUnvoiced") };
    raw.vocoderCarrier = bind("vocoderCarrier");
    raw.vocoderTone = { bind("vocoderShift"), bind("vocoderContour"), bind("vocoderCompand"), bind("vocoderModIn"), bind("vocoderCarIn"), bind("vocoderHarmonic"), bind("vocoderSub") };
    raw.vocoderBands = bind("vocoderBands");
    raw.deEss = { bind("deEssFrequency"), bind("deEssThreshold"), bind("deEssRange"), bind("deEssAttack"), bind("deEssRelease"), bind("deEssBandwidth"), bind("deEssAdaptive") };
    raw.deEssMode = bind("deEssMode");
    raw.deEssMonitor = bind("deEssMonitor");
    raw.channel = { bind("channelGate"), bind("channelComp"), bind("channelLowCut"), bind("channelLow"), bind("channelMid"), bind("channelHigh"), bind("channelPresence"), bind("channelThreshold"), bind("channelRatio"), bind("channelAttack"), bind("channelRelease"), bind("channelSaturation") };
    raw.tuneKey = bind("tuneKey");
    raw.tuneScale = bind("tuneScale");
    raw.tuneAmount = bind("tuneAmount");
    raw.tuneSpeed = bind("tuneSpeed");
    raw.tuneTolerance = bind("tuneTolerance");
    raw.tuneVibrato = bind("tuneVibrato");
    raw.tuneReference = bind("tuneReference");
    for (std::size_t i = 0; i < raw.tuneCustom.size(); ++i) raw.tuneCustom[i] = bind(transform::tune::customIds[i]);
    raw.mix = bind(ParameterIds::mix);
    raw.output = bind(ParameterIds::output);
    raw.bypass = bind(ParameterIds::bypass);
    raw.sequencerEnabled = bind("sequencerEnabled");
    raw.sequencerHostSync = bind("sequencerHostSync");
    raw.sequencerRate = bind("sequencerRate");
    raw.sequencerStart = bind("sequencerStart");
    raw.sequencerEnd = bind("sequencerEnd");
    raw.sequencerDirection = bind("sequencerDirection");
    raw.sequencerBipolar = bind("sequencerBipolar");
    raw.sequencerUnipolarNegative = bind("sequencerUnipolarNegative");
    raw.sequencerAttack = bind("sequencerAttack");
    raw.sequencerRelease = bind("sequencerRelease");
    raw.sequencerPeak = bind("sequencerPeak");
    raw.sequencerMove = bind("sequencerMove");
    raw.sequencerGateShortLength = bind("sequencerGateShortLength");
    raw.sequencerGateLongLength = bind("sequencerGateLongLength");
    raw.sequencerGateMode = bind("sequencerGateMode");
    for (int step = 0; step < transform::sequencer::stepCount; ++step)
    {
        const auto index = static_cast<size_t>(step);
        raw.sequencerSteps[index] = bind(
            transform::sequencer::stepParameterId(step));
        raw.sequencerGateSteps[index] = bind(
            transform::sequencer::gateStepParameterId(step));
    }
    for (int slot = 0; slot < transform::sequencer::targetSlotCount; ++slot)
    {
        const auto index = static_cast<size_t>(slot);
        raw.sequencerTargets[index] = bind(
            transform::sequencer::targetParameterId(slot));
        raw.sequencerTargetEnabled[index] = bind(
            transform::sequencer::targetEnabledParameterId(slot));
        raw.sequencerTargetDepth[index] = bind(
            transform::sequencer::targetDepthParameterId(slot));
    }
    for (size_t target = 0; target < transform::sequencer::targets.size(); ++target)
        sequencerTargetParameters[target] = parameters.getParameter(
            transform::sequencer::targets[target].parameterId);
}

float TransformAudioProcessor::load(const std::atomic<float>* value) noexcept
{
    const float loaded = value != nullptr
        ? value->load(std::memory_order_relaxed) : 0.0f;
    return std::isfinite(loaded) ? loaded : 0.0f;
}

bool TransformAudioProcessor::isSequencerTargetAssigned(int targetChoice) const noexcept
{
    if (targetChoice <= 0)
        return false;
    for (size_t slot = 0; slot < raw.sequencerTargets.size(); ++slot)
        if (load(raw.sequencerTargetEnabled[slot]) >= 0.5f
            && static_cast<int>(std::lround(load(raw.sequencerTargets[slot])))
                   == targetChoice)
            return true;
    return false;
}

float TransformAudioProcessor::sequencerTargetDepth(int targetChoice) const noexcept
{
    if (targetChoice <= 0)
        return 1.0f;
    for (size_t slot = 0; slot < raw.sequencerTargets.size(); ++slot)
        if (load(raw.sequencerTargetEnabled[slot]) >= 0.5f
            && static_cast<int>(std::lround(load(raw.sequencerTargets[slot])))
                   == targetChoice)
            return juce::jlimit(
                0.0f, 1.0f, load(raw.sequencerTargetDepth[slot]));
    return 1.0f;
}

float TransformAudioProcessor::sequencerModulatedValue(
    int targetChoice, float baseValue, float canonicalSequenceValue,
    bool bipolar, bool unipolarNegative,
    juce::RangedAudioParameter* valueParameter) const noexcept
{
    if (! isSequencerTargetAssigned(targetChoice)
        || targetChoice > static_cast<int>(sequencerTargetParameters.size()))
        return baseValue;
    auto* parameter = valueParameter != nullptr
        ? valueParameter
        : sequencerTargetParameters[static_cast<size_t>(targetChoice - 1)];
    if (parameter == nullptr)
        return baseValue;

    canonicalSequenceValue = transform::sequencer::applyTargetDepth(
        canonicalSequenceValue, bipolar, sequencerTargetDepth(targetChoice));
    const float baseNormalised = juce::jlimit(
        0.0f, 1.0f, parameter->convertTo0to1(baseValue));
    float modulatedNormalised = baseNormalised;
    if (bipolar)
    {
        const float deviation = 2.0f * canonicalSequenceValue - 1.0f;
        const float radius = juce::jmin(
            baseNormalised, 1.0f - baseNormalised);
        modulatedNormalised = baseNormalised + deviation * radius;
    }
    else
    {
        const float amount = juce::jlimit(
            0.0f, 1.0f, 2.0f * canonicalSequenceValue - 1.0f);
        modulatedNormalised = unipolarNegative
            ? baseNormalised - amount * baseNormalised
            : baseNormalised + amount * (1.0f - baseNormalised);
    }
    modulatedNormalised = juce::jlimit(
        0.0f, 1.0f, modulatedNormalised);
    sequencerAnimatedNormalised[static_cast<size_t>(targetChoice - 1)].store(
        modulatedNormalised, std::memory_order_relaxed);
    return parameter->convertFrom0to1(modulatedNormalised);
}

std::optional<float> TransformAudioProcessor::getSequencerAnimatedNormalised(
    const juce::String& parameterId) const noexcept
{
    const int choice = transform::sequencer::targetChoiceForParameter(parameterId);
    if (choice <= 0
        || choice > static_cast<int>(sequencerAnimatedNormalised.size())
        || load(raw.sequencerEnabled) < 0.5f
        || ! isSequencerTargetAssigned(choice))
        return std::nullopt;
    const float value = sequencerAnimatedNormalised[
        static_cast<size_t>(choice - 1)].load(std::memory_order_relaxed);
    if (! std::isfinite(value) || value < 0.0f)
        return std::nullopt;
    return juce::jlimit(0.0f, 1.0f, value);
}

bool TransformAudioProcessor::isBusesLayoutSupported(
    const BusesLayout& layouts) const
{
    const auto input = layouts.getMainInputChannelSet();
    const auto output = layouts.getMainOutputChannelSet();
    const auto sidechain = layouts.getChannelSet(true, 1);
    return input == output
        && (output == juce::AudioChannelSet::mono()
            || output == juce::AudioChannelSet::stereo())
        && (sidechain.isDisabled() || sidechain == juce::AudioChannelSet::mono()
            || sidechain == juce::AudioChannelSet::stereo());
}

void TransformAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused(samplesPerBlock);
    currentSampleRate = juce::jmax(1.0, sampleRate);
    moduleChain.prepare(currentSampleRate, getTotalNumOutputChannels());
    deEssReductionDb.store(0.0f, std::memory_order_relaxed);
    channelReductionDb.store(0.0f, std::memory_order_relaxed);
    setLatencySamples(moduleChain.latency());
    freqTailSeconds.store(15.0f, std::memory_order_relaxed);
    freezeStatus.store(transform::freeze::Status::bypassed, std::memory_order_relaxed);
    tuneDetector.prepare(currentSampleRate);
    tuneExpression.reset();
    tuneHeldNotes.reset();
    tuneTarget.store(-1, std::memory_order_relaxed);
    tuneWasEnabled = false;
    publishTuneFrequency(0.0f);
    const auto resetSmoother = [this](LinearSmoother& smoother, float value)
    {
        smoother.reset(currentSampleRate, smoothingSeconds);
        smoother.setCurrentAndTargetValue(value);
    };
    resetSmoother(mixValue,
        juce::jlimit(0.0f, 1.0f, load(raw.mix) * 0.01f));
    resetSmoother(outputGainValue,
        juce::Decibels::decibelsToGain(load(raw.output)));
    resetSmoother(bypassValue, load(raw.bypass) >= 0.5f ? 1.0f : 0.0f);
    sequencerFreePhase = 0.0;
    sequencerHeldNotes = 0;
    previousSequencerCanonical = 1.0f;
    sequencerGateGain = 1.0f;
}

void TransformAudioProcessor::releaseResources()
{
    moduleChain.reset();
    deEssReductionDb.store(0.0f, std::memory_order_relaxed);
    channelReductionDb.store(0.0f, std::memory_order_relaxed);
    freqTailSeconds.store(15.0f, std::memory_order_relaxed);
    freezeStatus.store(transform::freeze::Status::bypassed, std::memory_order_relaxed);
    tuneDetector.reset();
    tuneExpression.reset();
    tuneHeldNotes.reset();
    tuneTarget.store(-1, std::memory_order_relaxed);
    tuneWasEnabled = false;
    publishTuneFrequency(0.0f);
    activeSequencerStep.store(-1, std::memory_order_relaxed);
}

float TransformAudioProcessor::sanitiseSample(float sample, float limit) noexcept
{
    if (! std::isfinite(sample))
        return 0.0f;
    return juce::jlimit(-limit, limit, sample);
}

void TransformAudioProcessor::processBlock(
    juce::AudioBuffer<float>& hostBuffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    captureMidiCC(midi);
    auto buffer = getBusBuffer(hostBuffer, false, 0);
    const float* sideL = nullptr;
    const float* sideR = nullptr;
    if (getChannelCountOfBus(true, 1) > 0)
    {
        const auto side = getBusBuffer(hostBuffer, true, 1);
        if (side.getNumChannels() > 0)
        {
            sideL = side.getReadPointer(0);
            sideR = side.getNumChannels() > 1 ? side.getReadPointer(1) : sideL;
        }
    }
    const int sampleCount = buffer.getNumSamples();
    const int hostChannels = buffer.getNumChannels();
    const int activeChannels = juce::jmin(channelCount, hostChannels);
    for (int channel = activeChannels; channel < hostChannels; ++channel)
        buffer.clear(channel, 0, sampleCount);
    if (sampleCount <= 0 || activeChannels <= 0)
        return;
    const bool sequenceEnabled = load(raw.sequencerEnabled) >= 0.5f;
    const bool hostSync = load(raw.sequencerHostSync) >= 0.5f;
    const auto direction = static_cast<transform::sequencer::Direction>(
        juce::jlimit(0, 4, static_cast<int>(std::lround(
            load(raw.sequencerDirection)))));

    juce::Optional<juce::AudioPlayHead::PositionInfo> hostPosition;
    const bool freqNeedsTempo = load(raw.freqSync) >= 0.5f
        && load(raw.moduleEnabled[transform::modules::index(transform::modules::Id::freq)]) >= 0.5f
        && load(raw.bypass) < 0.5f;
    if ((sequenceEnabled && hostSync && direction != transform::sequencer::Direction::played) || freqNeedsTempo)
        if (auto* playhead = getPlayHead())
            hostPosition = playhead->getPosition();
    double hostTempo = 120.0;
    if (hostPosition.hasValue())
        if (const auto hostBpm = hostPosition->getBpm())
            if (std::isfinite(*hostBpm) && *hostBpm > 0.0)
                hostTempo = juce::jlimit(10.0, 1000.0, *hostBpm);
    const double bpm = sequenceEnabled && hostSync && direction != transform::sequencer::Direction::played
        ? hostTempo : 120.0;

    bool phraseTriggered = false;
    if (sequenceEnabled && direction == transform::sequencer::Direction::played)
    {
        for (const auto metadata : midi)
        {
            const auto message = metadata.getMessage();
            if (message.isNoteOn())
            {
                if (sequencerHeldNotes == 0)
                    phraseTriggered = true;
                ++sequencerHeldNotes;
            }
            else if (message.isNoteOff())
                sequencerHeldNotes = juce::jmax(0, sequencerHeldNotes - 1);
            else if (message.isAllNotesOff() || message.isAllSoundOff())
                sequencerHeldNotes = 0;
        }
    }
    else
        sequencerHeldNotes = 0;

    std::array<float, transform::sequencer::stepCount> steps {};
    float sequenceMinimum = 1.0f;
    float sequenceMaximum = 0.0f;
    for (int step = 0; step < transform::sequencer::stepCount; ++step)
    {
        const auto index = static_cast<size_t>(step);
        steps[index] = juce::jlimit(
            0.0f, 1.0f, load(raw.sequencerSteps[index]));
        sequenceMinimum = juce::jmin(sequenceMinimum, steps[index]);
        sequenceMaximum = juce::jmax(sequenceMaximum, steps[index]);
    }
    const bool bipolar = load(raw.sequencerBipolar) >= 0.5f;
    const bool unipolarNegative = ! bipolar
        && load(raw.sequencerUnipolarNegative) >= 0.5f;
    const auto modulatedPrevious = [this, sequenceEnabled, bipolar,
                                    unipolarNegative](
        int target, const std::atomic<float>* parameter)
    {
        const float base = load(parameter);
        return sequenceEnabled
            ? sequencerModulatedValue(target, base,
                previousSequencerCanonical, bipolar, unipolarNegative)
            : base;
    };
    const float peak = juce::jlimit(-1.0f, 1.0f, modulatedPrevious(
        transform::sequencer::sequencerDepthTargetChoice, raw.sequencerPeak));
    const float move = juce::jlimit(-1.0f, 1.0f, modulatedPrevious(
        transform::sequencer::sequencerOffsetTargetChoice, raw.sequencerMove));
    if (sequenceEnabled
        && (std::abs(peak) > 0.000001f || std::abs(move) > 0.000001f))
        for (auto& value : steps)
            value = transform::sequencer::applyDepthOffset(
                value, sequenceMinimum, sequenceMaximum,
                peak, move, bipolar);

    const int rate = juce::jlimit(
        0, transform::sequencer::rateChoiceCount - 1,
        static_cast<int>(std::lround(load(raw.sequencerRate))));
    const double beatsPerStep = transform::sequencer::beatsForRate(rate);
    const double phaseIncrement = bpm / (
        60.0 * beatsPerStep * currentSampleRate);
    if (phraseTriggered)
        sequencerFreePhase = 0.0;
    double gatePhaseAtBlockStart = sequencerFreePhase;
    double sequencePhase = sequencerFreePhase
        + 0.5 * static_cast<double>(sampleCount) * phaseIncrement;
    if (sequenceEnabled && hostSync
        && direction != transform::sequencer::Direction::played
        && hostPosition.hasValue())
        if (const auto ppq = hostPosition->getPpqPosition())
            if (std::isfinite(*ppq))
            {
                sequencePhase = *ppq / beatsPerStep;
                gatePhaseAtBlockStart = sequencePhase;
            }
    if (sequenceEnabled)
        sequencerFreePhase += static_cast<double>(sampleCount) * phaseIncrement;
    if (sequencerFreePhase > 1048576.0)
        sequencerFreePhase = std::fmod(
            sequencerFreePhase,
            static_cast<double>(transform::sequencer::stepCount * 8));

    const int firstStep = juce::jlimit(
        1, transform::sequencer::stepCount - 1,
        static_cast<int>(std::lround(modulatedPrevious(
            transform::sequencer::sequencerStartTargetChoice,
            raw.sequencerStart)))) - 1;
    const int lastStep = juce::jlimit(
        firstStep + 2, transform::sequencer::stepCount,
        static_cast<int>(std::lround(modulatedPrevious(
            transform::sequencer::sequencerEndTargetChoice,
            raw.sequencerEnd)))) - 1;
    const float attack = juce::jlimit(0.0f, 1.0f, modulatedPrevious(
        transform::sequencer::sequencerAttackTargetChoice, raw.sequencerAttack));
    const float release = juce::jlimit(0.0f, 1.0f, modulatedPrevious(
        transform::sequencer::sequencerReleaseTargetChoice,
        raw.sequencerRelease));
    int activeStep = -1;
    float canonical = 1.0f;
    if (sequenceEnabled)
        canonical = transform::sequencer::evaluate(
            steps, sequencePhase, firstStep, lastStep, direction,
            attack, release, &activeStep);
    previousSequencerCanonical = canonical;
    activeSequencerStep.store(activeStep, std::memory_order_relaxed);

    const auto sequenced = [this, sequenceEnabled, canonical, bipolar,
                            unipolarNegative](
        int target, const std::atomic<float>* parameter)
    {
        const float base = load(parameter);
        return sequenceEnabled
            ? sequencerModulatedValue(target, base, canonical,
                bipolar, unipolarNegative)
            : base;
    };
    mixValue.setTargetValue(juce::jlimit(
        0.0f, 1.0f, sequenced(transform::sequencer::mixTargetChoice,
            raw.mix) * 0.01f));
    outputGainValue.setTargetValue(juce::Decibels::decibelsToGain(
        sequenced(transform::sequencer::outputTargetChoice, raw.output)));
    bypassValue.setTargetValue(load(raw.bypass) >= 0.5f ? 1.0f : 0.0f);

    transform::modules::Enabled moduleStates {};
    for (size_t i = 0; i < moduleStates.size(); ++i)
        moduleStates[i] = load(raw.moduleEnabled[i]) >= 0.5f;
    moduleChain.setEnabled(moduleStates);
    moduleChain.setPitchControls(
        sequenced(transform::sequencer::pitchSemitonesTargetChoice, raw.pitchSemitones),
        sequenced(transform::sequencer::pitchFineTargetChoice, raw.pitchFine),
        sequenced(transform::sequencer::pitchFormantTargetChoice, raw.pitchFormant),
        load(raw.bypass) < 0.5f,
        { sequenced(transform::sequencer::pitchPreserveTargetChoice, raw.pitchExtra[0]),
          sequenced(transform::sequencer::pitchGlideTargetChoice, raw.pitchExtra[1]),
          sequenced(transform::sequencer::pitchDetuneTargetChoice, raw.pitchExtra[2]),
          sequenced(transform::sequencer::pitchTransientTargetChoice, raw.pitchExtra[3]) },
        sequenced(transform::sequencer::pitchBlendTargetChoice, raw.pitchExtra[4]),
        sequenced(transform::sequencer::pitchFeedbackTargetChoice, raw.pitchExtra[5]),
        static_cast<int>(load(raw.pitchAlgorithm)));
    transform::harmony::VoiceControls harmonyValues {};
    for (std::size_t i = 0; i < harmonyValues.size(); ++i)
    {
        const int first = transform::sequencer::harmonyFirstTargetChoice + static_cast<int>(i) * 3;
        harmonyValues[i] = { sequenced(first, raw.harmony[i][0]),
                             sequenced(first + 1, raw.harmony[i][1]),
                             load(raw.harmonyPower[i]) >= 0.5f ? sequenced(first + 2, raw.harmony[i][2]) : 0.0f,
                             sequenced(transform::sequencer::harmonyExtraFirstTargetChoice + static_cast<int>(i) * 3, raw.harmony[i][3]),
                             sequenced(transform::sequencer::harmonyExtraFirstTargetChoice + static_cast<int>(i) * 3 + 1, raw.harmony[i][4]),
                             sequenced(transform::sequencer::harmonyExtraFirstTargetChoice + static_cast<int>(i) * 3 + 2, raw.harmony[i][5]) };
    }
    const int harmonyMode = static_cast<int>(load(raw.harmonyMode));
    const bool harmonyAudible = std::any_of(harmonyValues.begin(), harmonyValues.end(),
        [](const auto& voice) { return voice.level > 0.0f; });
    const bool harmonyTracks = harmonyMode != 0 && harmonyAudible && moduleStates[transform::modules::index(transform::modules::Id::harmony)]
        && load(raw.bypass) < 0.5f;
    if (!harmonyTracks) moduleChain.setHarmonyControls(harmonyValues, load(raw.bypass) < 0.5f);
    const int freqFirst = transform::sequencer::freqFirstTargetChoice;
    const int freqExtraFirst = transform::sequencer::freqExtraFirstTargetChoice;
    const transform::freq::Controls freqValues { sequenced(freqFirst, raw.freq[0]),
        sequenced(freqFirst + 1, raw.freq[1]), sequenced(freqFirst + 2, raw.freq[2]),
        sequenced(freqFirst + 3, raw.freq[3]), sequenced(freqFirst + 4, raw.freq[4]),
        static_cast<int>(load(raw.freqMode)), sequenced(freqExtraFirst, raw.freqExtra[0]),
        sequenced(freqExtraFirst + 1, raw.freqExtra[1]), sequenced(freqExtraFirst + 2, raw.freqExtra[2]),
        sequenced(freqExtraFirst + 3, raw.freqExtra[3]), load(raw.freqSync) >= 0.5f ? juce::roundToInt(sequenced(transform::sequencer::freqDivisionTargetChoice, raw.freqDivision)) : 0, hostTempo, load(raw.freqRelative) >= 0.5f,
        sequenced(transform::sequencer::freqRelativeTargetChoice, raw.freqRelativeShift) };
    moduleChain.setFreqControls(freqValues, load(raw.bypass) < 0.5f);
    // Approximate the feedback decay to -60 dB; other effects retain the 15 s estimate.
    const double fb = std::clamp(static_cast<double>(freqValues.feedback) * 0.01, 0.0, 0.95);
    const bool freqActive = moduleStates[transform::modules::index(transform::modules::Id::freq)]
        && load(raw.bypass) < 0.5f && freqValues.blend > 0.0f;
    const double tail = freqActive && fb > 0.0 ? -6.907755278982137 / std::log(fb)
        * transform::freq::delayMs(freqValues.delay, freqValues.sync, hostTempo) * 0.001 : 0.0;
    freqTailSeconds.store(static_cast<float>(tail + 0.1), std::memory_order_relaxed);

    const int freezeFirst = transform::sequencer::freezeFirstTargetChoice;
    const int freezeExtra = transform::sequencer::freezeExtraFirstTargetChoice;
    moduleChain.setFreezeControls({ sequenced(freezeFirst, raw.freeze[0]),
        sequenced(freezeFirst + 1, raw.freeze[1]), sequenced(freezeFirst + 2, raw.freeze[2]),
        sequenced(freezeFirst + 3, raw.freeze[3]), static_cast<int>(load(raw.freezeMode)),
        static_cast<int>(load(raw.freezeDirection)), sequenced(freezeExtra, raw.freezeExtra[0]),
        sequenced(freezeExtra + 1, raw.freezeExtra[1]), sequenced(freezeExtra + 2, raw.freezeExtra[2]),
        sequenced(freezeExtra + 3, raw.freezeExtra[3]), static_cast<int>(load(raw.freezeAlgorithm)),
        sequenced(transform::sequencer::freezeCreativeFirstTargetChoice, raw.freezeCreative[0]),
        sequenced(transform::sequencer::freezeCreativeFirstTargetChoice + 1, raw.freezeCreative[1]),
        sequenced(transform::sequencer::freezeCreativeFirstTargetChoice + 2, raw.freezeCreative[2]),
        static_cast<int>(load(raw.freezeVoiceMode)), sequenced(transform::sequencer::freezeGlideTargetChoice, raw.freezeGlide) }, load(raw.bypass) < 0.5f);
    const int morphFirst = transform::sequencer::morphFirstTargetChoice;
    const int morphExtra = transform::sequencer::morphExtraFirstTargetChoice;
    moduleChain.setMorphControls({ sequenced(morphFirst, raw.morph[0]),
        sequenced(morphFirst + 1, raw.morph[1]), sequenced(morphFirst + 2, raw.morph[2]),
        sequenced(morphFirst + 3, raw.morph[3]), sequenced(morphFirst + 4, raw.morph[4]),
        sequenced(morphExtra, raw.morphExtra[0]), sequenced(morphExtra + 1, raw.morphExtra[1]),
        sequenced(morphExtra + 2, raw.morphExtra[2]), sequenced(morphExtra + 3, raw.morphExtra[3]),
        static_cast<int>(load(raw.morphMode)) }, load(raw.bypass) < 0.5f);
    const int vocoderFirst = transform::sequencer::vocoderFirstTargetChoice;
    moduleChain.setVocoderControls({ sequenced(vocoderFirst, raw.vocoder[0]),
        sequenced(vocoderFirst + 1, raw.vocoder[1]), sequenced(vocoderFirst + 2, raw.vocoder[2]),
        sequenced(vocoderFirst + 3, raw.vocoder[3]), sequenced(vocoderFirst + 4, raw.vocoder[4]),
        sequenced(vocoderFirst + 5, raw.vocoder[5]), static_cast<int>(load(raw.vocoderCarrier)),
        juce::roundToInt(sequenced(transform::sequencer::vocoderBandsTargetChoice, raw.vocoderBands)),
        sequenced(transform::sequencer::vocoderExtraFirstTargetChoice, raw.vocoderExtra[0]),
        sequenced(transform::sequencer::vocoderExtraFirstTargetChoice + 1, raw.vocoderExtra[1]),
        sequenced(transform::sequencer::vocoderExtraFirstTargetChoice + 2, raw.vocoderExtra[2]),
        sequenced(transform::sequencer::vocoderToneFirstTargetChoice + 0, raw.vocoderTone[0]),
        sequenced(transform::sequencer::vocoderToneFirstTargetChoice + 1, raw.vocoderTone[1]),
        sequenced(transform::sequencer::vocoderToneFirstTargetChoice + 2, raw.vocoderTone[2]),
        sequenced(transform::sequencer::vocoderToneFirstTargetChoice + 3, raw.vocoderTone[3]),
        sequenced(transform::sequencer::vocoderToneFirstTargetChoice + 4, raw.vocoderTone[4]),
        sequenced(transform::sequencer::vocoderToneFirstTargetChoice + 5, raw.vocoderTone[5]),
        sequenced(transform::sequencer::vocoderToneFirstTargetChoice + 6, raw.vocoderTone[6]) }, load(raw.bypass) < 0.5f);
    const int deEssFirst = transform::sequencer::deEssFirstTargetChoice;
    moduleChain.setDeEssControls({ sequenced(deEssFirst, raw.deEss[0]),
        sequenced(deEssFirst + 1, raw.deEss[1]), sequenced(deEssFirst + 2, raw.deEss[2]),
        sequenced(deEssFirst + 3, raw.deEss[3]), sequenced(deEssFirst + 4, raw.deEss[4]),
        static_cast<int>(load(raw.deEssMode)), static_cast<int>(load(raw.deEssMonitor)),
        sequenced(transform::sequencer::deEssExtraFirstTargetChoice, raw.deEss[5]),
        sequenced(transform::sequencer::deEssExtraFirstTargetChoice + 1, raw.deEss[6]) }, load(raw.bypass) < 0.5f);
    const int channelFirst = transform::sequencer::channelFirstTargetChoice;
    moduleChain.setChannelControls({ sequenced(channelFirst, raw.channel[0]),
        sequenced(channelFirst + 1, raw.channel[1]),
        sequenced(channelFirst + 2, raw.channel[2]),
        sequenced(channelFirst + 3, raw.channel[3]),
        sequenced(channelFirst + 4, raw.channel[4]),
        sequenced(channelFirst + 5, raw.channel[5]),
        sequenced(channelFirst + 6, raw.channel[6]),
        sequenced(transform::sequencer::channelExtraFirstTargetChoice + 0, raw.channel[7]),
        sequenced(transform::sequencer::channelExtraFirstTargetChoice + 1, raw.channel[8]),
        sequenced(transform::sequencer::channelExtraFirstTargetChoice + 2, raw.channel[9]),
        sequenced(transform::sequencer::channelExtraFirstTargetChoice + 3, raw.channel[10]),
        sequenced(transform::sequencer::channelExtraFirstTargetChoice + 4, raw.channel[11]) }, load(raw.bypass) < 0.5f);
    const bool vocoderMidiActive = moduleChain.vocoderAcceptsMidi();
    const bool freezeMidiActive = moduleChain.freezeAcceptsMidi();
    auto freezeMidi = midi.begin();
    const auto freezeMidiEnd = midi.end();

    // The tuner analyses the original input before gate, mix or output gain.
    // It does not write to the buffer and adds no audio-path latency.
    const bool tuneEnabled = moduleStates[transform::modules::index(transform::modules::Id::tune)]
                             && load(raw.bypass) < 0.5f;
    const bool analyseTune = tuneEnabled || harmonyTracks;
    if (analyseTune)
    {
        if (!tuneWasEnabled)
        {
            tuneDetector.reset();
            publishTuneFrequency(0.0f);
        }
    }
    else if (tuneWasEnabled)
    {
        tuneDetector.reset();
        publishTuneFrequency(0.0f);
    }
    tuneWasEnabled = analyseTune;
    const int tuneKey = juce::roundToInt(load(raw.tuneKey));
    const int tuneScale = juce::roundToInt(load(raw.tuneScale));
    const float tuneAmount = tuneEnabled ? sequenced(transform::sequencer::tuneAmountTargetChoice, raw.tuneAmount) : 0.0f;
    const float tuneSpeed = sequenced(transform::sequencer::tuneSpeedTargetChoice, raw.tuneSpeed);
    const float tolerance = sequenced(transform::sequencer::tuneToleranceTargetChoice, raw.tuneTolerance);
    const float vibrato = sequenced(transform::sequencer::tuneVibratoTargetChoice, raw.tuneVibrato);
    const float reference = sequenced(transform::sequencer::tuneReferenceTargetChoice, raw.tuneReference);
    tuneReference.store(reference, std::memory_order_relaxed);
    unsigned customMask = 0;
    if (tuneScale == transform::tune::customScale)
        for (std::size_t i = 0; i < raw.tuneCustom.size(); ++i)
            if (load(raw.tuneCustom[i]) >= 0.5f) customMask |= 1u << i;
    const bool midiTune = tuneScale == transform::tune::midiScale;
    if (!analyseTune) tuneExpression.reset();
    const auto refreshCorrection = [&](bool fresh = false) noexcept
    {
        const auto full = tuneEnabled ? transform::tune::correctionForFrequency(tuneDetector.getFrequency(),
            tuneKey, tuneScale, 100.0f, reference, customMask, &tuneHeldNotes) : transform::tune::Correction {};
        tuneTarget.store(full.targetMidi, std::memory_order_relaxed);
        const float shift = tuneExpression.apply(full, tuneAmount, tolerance, vibrato, fresh);
        moduleChain.setTuneCorrection(shift, tuneSpeed);
        if (harmonyTracks)
        {
            auto resolved = harmonyValues;
            for (std::size_t i = 0; i < resolved.size(); ++i)
                if (!transform::harmony::resolveInterval(harmonyMode, i, tuneDetector.getFrequency(), reference,
                        tuneKey, tuneScale, customMask, tuneHeldNotes, moduleChain.harmonyLeadShift(), shift, resolved[i].interval))
                    resolved[i].level = 0.0f;
            moduleChain.setHarmonyControls(resolved, true);
        }
    };
    refreshCorrection();
    // Analyse whole blocks when correction is not required, preserving the
    // efficient tuner-only path. With correction active, consume each input
    // sample before it is overwritten; apply new estimates causally.
    const bool correctTune = analyseTune && tuneAmount > 0.0f;
    if (analyseTune && !correctTune && !midiTune && !harmonyTracks
        && tuneDetector.process(buffer.getReadPointer(0),
            activeChannels > 1 ? buffer.getReadPointer(1) : nullptr, sampleCount))
    {
        publishTuneFrequency(tuneDetector.getFrequency());
        refreshCorrection(true);
    }

    std::array<transform::sequencer::GateStepMode,
               transform::sequencer::stepCount> gateSteps {};
    for (int step = 0; step < transform::sequencer::stepCount; ++step)
        gateSteps[static_cast<size_t>(step)] =
            transform::sequencer::gateStepModeFromChoice(
                load(raw.sequencerGateSteps[static_cast<size_t>(step)]));
    const float shortLength = juce::jlimit(0.10f, 0.60f, sequenced(
        transform::sequencer::sequencerGateShortTargetChoice,
        raw.sequencerGateShortLength));
    const float longLength = juce::jlimit(0.65f, 0.95f, sequenced(
        transform::sequencer::sequencerGateLongTargetChoice,
        raw.sequencerGateLongLength));
    const int gateMode = juce::jlimit(
        0, 3, juce::roundToInt(load(raw.sequencerGateMode)));
    const float gateSmoothing = 1.0f - std::exp(
        -1.0f / static_cast<float>(0.002 * currentSampleRate));

    auto* left = buffer.getWritePointer(0);
    auto* right = activeChannels > 1 ? buffer.getWritePointer(1) : nullptr;
    for (int sample = 0; sample < sampleCount; ++sample)
    {
        if (freezeMidi != freezeMidiEnd)
            while (freezeMidi != freezeMidiEnd && (*freezeMidi).samplePosition <= sample)
            {
                const auto event = *freezeMidi;
                if (freezeMidiActive) moduleChain.freezeMidiEvent(event.data, event.numBytes);
                if (vocoderMidiActive) moduleChain.vocoderMidiEvent(event.data, event.numBytes);
                if (tuneHeldNotes.event(event.data, event.numBytes) && analyseTune && (midiTune || harmonyMode == 2)) refreshCorrection();
                ++freezeMidi;
            }
        float dryL = left[sample];
        float dryR = right != nullptr ? right[sample] : dryL;
        if ((correctTune || harmonyTracks || (analyseTune && midiTune)) && tuneDetector.process(&dryL, right != nullptr ? &dryR : nullptr, 1))
        {
            publishTuneFrequency(tuneDetector.getFrequency());
            refreshCorrection(true);
        }
        float gateTarget = 1.0f;
        if (sequenceEnabled && gateMode != 3)
        {
            const auto position = transform::sequencer::positionForPhase(
                gatePhaseAtBlockStart
                    + static_cast<double>(sample) * phaseIncrement,
                lastStep - firstStep + 1, direction);
            gateTarget = transform::sequencer::gateStepTarget(
                gateSteps[static_cast<size_t>(firstStep + position.current)],
                position.fraction, shortLength, longLength);
        }
        sequencerGateGain += gateSmoothing
            * (gateTarget - sequencerGateGain);
        sequencerGateGain = juce::jlimit(
            0.0f, 1.0f, sequencerGateGain);

        float effectInputL = gateMode == 0
            ? dryL * sequencerGateGain : dryL;
        float effectInputR = gateMode == 0
            ? dryR * sequencerGateGain : dryR;
        float wetL = effectInputL;
        float wetR = effectInputR;
        moduleChain.processFrame(wetL, wetR, dryL, dryR,
            sideL != nullptr ? sideL[sample] : 0.0f, sideR != nullptr ? sideR[sample] : 0.0f, sideL != nullptr);
        if (gateMode == 1)
        {
            wetL *= sequencerGateGain;
            wetR *= sequencerGateGain;
        }
        const float mix = mixValue.getNextValue();
        const float gain = outputGainValue.getNextValue();
        float processedL = gain * (dryL + (wetL - dryL) * mix);
        float processedR = gain * (dryR + (wetR - dryR) * mix);
        if (gateMode == 2)
        {
            processedL *= sequencerGateGain;
            processedR *= sequencerGateGain;
        }
        const float bypass = bypassValue.getNextValue();
        left[sample] = sanitiseSample(
            processedL + (dryL - processedL) * bypass, 64.0f);
        if (right != nullptr)
            right[sample] = sanitiseSample(
                processedR + (dryR - processedR) * bypass, 64.0f);
    }
    freezeStatus.store(moduleChain.freezeStatus(), std::memory_order_relaxed);
    deEssReductionDb.store(moduleChain.deEssReductionDb(), std::memory_order_relaxed);
    channelReductionDb.store(moduleChain.channelReductionDb(), std::memory_order_relaxed);
}

// Hosts which invoke their own bypass callback still receive the same delay
// advertised to the host. No FFT runs once the short disable fade is over.
void TransformAudioProcessor::processBlockBypassed(
    juce::AudioBuffer<float>& hostBuffer, juce::MidiBuffer& midi)
{
    auto buffer = getBusBuffer(hostBuffer, false, 0);
    captureMidiCC(midi);
    juce::ScopedNoDenormals noDenormals;
    moduleChain.setPitchControls(0.0f, 0.0f, 0.0f, false);
    moduleChain.setTuneCorrection(0.0f, 0.0f);
    tuneHeldNotes.reset();
    tuneExpression.reset();
    tuneTarget.store(-1, std::memory_order_relaxed);
    moduleChain.disableHarmony();
    moduleChain.disableFreq();
    freqTailSeconds.store(15.0f, std::memory_order_relaxed);
    moduleChain.disableFreeze();
    moduleChain.disableMorph();
    moduleChain.disableVocoder();
    moduleChain.disableDeEss();
    deEssReductionDb.store(0.0f, std::memory_order_relaxed);
    channelReductionDb.store(0.0f, std::memory_order_relaxed);
    moduleChain.disableChannel();
    freezeStatus.store(transform::freeze::Status::bypassed, std::memory_order_relaxed);
    if (tuneWasEnabled)
    {
        tuneDetector.reset();
        tuneWasEnabled = false;
        publishTuneFrequency(0.0f);
    }
    if (buffer.getNumChannels() == 0) return;
    auto* left = buffer.getWritePointer(0);
    auto* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        float dryL = left[i], dryR = right != nullptr ? right[i] : dryL;
        float wetL = dryL, wetR = dryR;
        moduleChain.processFrame(wetL, wetR, dryL, dryR);
        left[i] = sanitiseSample(dryL, 64.0f);
        if (right != nullptr) right[i] = sanitiseSample(dryR, 64.0f);
    }
    for (int ch = channelCount; ch < buffer.getNumChannels(); ++ch)
        buffer.clear(ch, 0, buffer.getNumSamples());
}

void TransformAudioProcessor::getStateInformation(juce::MemoryBlock& destination)
{
    auto state = parameters.copyState();
    state.setProperty("editorModule", getEditorModule(), nullptr);
    state.setProperty("editorZoomPercent", getEditorZoomPercent(), nullptr);
    (void)isSequencerColourLocked();
    state.setProperty("colourLockSuppressed", colourLockSuppressed.load(), nullptr);
    state.setProperty("schemaVersion", stateSchemaVersion, nullptr);
    if (const auto xml = state.createXml())
        copyXmlToBinary(*xml, destination);
}

void TransformAudioProcessor::setStateInformation(const void* data, int size)
{
    const auto xml = getXmlFromBinary(data, size);
    if (xml == nullptr || ! xml->hasTagName("TransformParameters"))
        return;
    auto state = juce::ValueTree::fromXml(*xml);
    if (static_cast<int>(state.getProperty("schemaVersion", 0))
        != stateSchemaVersion)
        return;
    if (state.hasProperty("editorModule"))
        setEditorModule(static_cast<int>(state.getProperty("editorModule")));
    state.removeProperty("editorModule", nullptr);
    if (state.hasProperty("editorZoomPercent")) setEditorZoomPercent(static_cast<int>(state.getProperty("editorZoomPercent")));
    state.removeProperty("editorZoomPercent", nullptr);
    const bool suppressColourLock = static_cast<bool>(state.getProperty("colourLockSuppressed", false));
    state.removeProperty("colourLockSuppressed", nullptr);
    setSavedParameterState(state);
    parameters.replaceState(state);
    colourLockSuppressed.store(suppressColourLock);
    resetSequenceSelection.store(true);
}

juce::AudioProcessorEditor* TransformAudioProcessor::createEditor()
{
    return new TransformAudioProcessorEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TransformAudioProcessor();
}
