#pragma once

#include "PluginProcessor.h"
#include <cmath>
#include <functional>

class TransformAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                       public juce::DragAndDropContainer,
                                       private juce::Timer,
                                       private juce::ListBoxModel
{
public:
    explicit TransformAudioProcessorEditor(TransformAudioProcessor&);
    ~TransformAudioProcessorEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseEnter(const juce::MouseEvent&) override;
    bool keyPressed(const juce::KeyPress&) override;

private:
    using SliderAttachment =
        juce::AudioProcessorValueTreeState::SliderAttachment;

    class DetentedSlider final : public juce::Slider
    {
    public:
        std::function<bool()> onSavedStateReset;
        std::function<void()> onInitReset;
        void setCentreDetent(double value, double radius)
        {
            centreDetent = value;
            detentRadius = juce::jmax(0.0, radius);
            centreDetentEnabled = true;
            getProperties().set("hasCentreDetent", true);
            getProperties().set("centreDetentValue", value);
        }
        double snapValue(double attemptedValue, DragMode dragMode) override
        {
            if (centreDetentEnabled && dragMode != notDragging
                && std::abs(attemptedValue - centreDetent) <= detentRadius)
                return centreDetent;
            return attemptedValue;
        }
        void mouseDown(const juce::MouseEvent& event) override
        {
            middleResetGesture = event.mods.isMiddleButtonDown();
            if (middleResetGesture)
            {
                if (isEnabled() && onInitReset != nullptr)
                    onInitReset();
                return;
            }
            juce::Slider::mouseDown(event);
        }
        void mouseDrag(const juce::MouseEvent& event) override
        {
            if (! middleResetGesture)
                juce::Slider::mouseDrag(event);
        }
        void mouseUp(const juce::MouseEvent& event) override
        {
            if (middleResetGesture)
            {
                middleResetGesture = false;
                return;
            }
            juce::Slider::mouseUp(event);
        }
        void mouseDoubleClick(const juce::MouseEvent& event) override
        {
            if (!event.mods.isLeftButtonDown() || middleResetGesture || event.mods.isMiddleButtonDown() || event.mods.isRightButtonDown())
                return;
            if (onSavedStateReset != nullptr)
            {
                if (onSavedStateReset())
                    return;
                if (onInitReset != nullptr)
                {
                    onInitReset();
                    return;
                }
            }
            juce::Slider::mouseDoubleClick(event);
        }
    private:
        double centreDetent = 0.0;
        double detentRadius = 0.0;
        bool centreDetentEnabled = false;
        bool middleResetGesture = false;
    };

    class SteppedComboBox final : public juce::ComboBox
    {
    public:
        void mouseWheelMove(const juce::MouseEvent&,
                            const juce::MouseWheelDetails& wheel) override
        {
            if (! isEnabled() || isPopupActive() || getNumItems() <= 0
                || wheel.isInertial || std::abs(wheel.deltaY) < 0.0001f)
                return;
            const int direction = wheel.deltaY > 0.0f ? -1 : 1;
            const auto now = juce::Time::getMillisecondCounter();
            if (direction == lastWheelDirection
                && now - lastWheelTimeMs < 100u)
                return;
            lastWheelDirection = direction;
            lastWheelTimeMs = now;
            const int current = getSelectedItemIndex();
            const int next = current < 0
                ? (direction > 0 ? 0 : getNumItems() - 1)
                : juce::jlimit(0, getNumItems() - 1, current + direction);
            if (next != current)
                setSelectedItemIndex(next, juce::sendNotificationSync);
        }
    private:
        juce::uint32 lastWheelTimeMs = 0;
        int lastWheelDirection = 0;
    };

    // A real button with no painted chrome: only the existing logo is visible.
    class ZoomHitButton final : public juce::Button
    {
    public:
        ZoomHitButton() : juce::Button("Zoom") {}
    private:
        void paintButton(juce::Graphics&, bool, bool) override {}
    };

    class PresetDisplayLabel final : public juce::Label
    {
    public:
        std::function<void(int)> onStep;
        std::function<void()> onShowMenu;
        void paint(juce::Graphics&) override;
        void mouseDown(const juce::MouseEvent&) override;
        void mouseWheelMove(const juce::MouseEvent&,
                            const juce::MouseWheelDetails&) override;
    private:
        juce::uint32 lastWheelTimeMs = 0;
        int lastWheelDirection = 0;
    };

    class PresetListBox final : public juce::ListBox
    {
    public:
        using juce::ListBox::ListBox;
        std::function<bool(int)> onHorizontalStep;
        bool keyPressed(const juce::KeyPress& key) override
        {
            int direction = 0;
            if (key.getKeyCode() == juce::KeyPress::leftKey)
                direction = -1;
            else if (key.getKeyCode() == juce::KeyPress::rightKey)
                direction = 1;
            if (direction != 0 && onHorizontalStep != nullptr
                && onHorizontalStep(direction))
                return true;
            return juce::ListBox::keyPressed(key);
        }
    };

    class ParameterDragLabel final : public juce::Label
    {
    public:
        std::function<juce::String()> parameterIdProvider;
        bool dragHandleOnly = false;
        bool dragHandleRight = false;
        void setTargetAppearance(bool assigned, juce::Colour accent);
        void paint(juce::Graphics&) override;
        void mouseDrag(const juce::MouseEvent&) override;
    private:
        bool targetAssigned = false;
        juce::Colour targetAccent { 0xff34d6c6 };
    };

    struct Dial
    {
        DetentedSlider slider;
        ParameterDragLabel label;
        std::unique_ptr<SliderAttachment> attachment;
        juce::String help;
        juce::String parameterId;
    };

    class MidiLearnOverlay;
    class SequencerPanel;
    class FolderTreeItem;
    class LookAndFeel final : public juce::LookAndFeel_V4
    {
    public:
        explicit LookAndFeel(bool compactPopupFont = false);
        juce::Label* createSliderTextBox(juce::Slider&) override;
        juce::Font getPopupMenuFont() override;
        juce::Font getComboBoxFont(juce::ComboBox&) override;
        void drawRotarySlider(juce::Graphics&, int, int, int, int,
                              float, float, float, juce::Slider&) override;
        void drawLinearSlider(juce::Graphics&, int, int, int, int,
                              float, float, float,
                              juce::Slider::SliderStyle,
                              juce::Slider&) override;
        void drawToggleButton(juce::Graphics&, juce::ToggleButton&,
                              bool, bool) override;
        void drawPopupMenuItem(juce::Graphics&,
                               const juce::Rectangle<int>&,
                               bool, bool, bool, bool, bool,
                               const juce::String&, const juce::String&,
                               const juce::Drawable*,
                               const juce::Colour*) override;
        void drawPopupMenuBackground(juce::Graphics&, int, int) override;
        juce::PopupMenu::Options getOptionsForComboBoxPopupMenu(
            juce::ComboBox&, juce::Label&) override;
        void setPopupBorderColour(juce::Colour colour)
        {
            popupBorderColour = colour;
        }
        juce::Colour getPopupBorderColour() const noexcept
        {
            return popupBorderColour;
        }
    private:
        juce::Colour popupBorderColour;
        bool useCompactPopupFont = false;
    };

    TransformAudioProcessor& processor;
    // Declared before all child controls: the sequencer borrows this image
    // and is destroyed before the editor releases it.
    const juce::Image backgroundImage;
    LookAndFeel lookAndFeel;
    juce::Component content;
    std::vector<std::unique_ptr<Dial>> dials;
    Dial* mixDial = nullptr;
    Dial* outputDial = nullptr;
    Dial* themeDial = nullptr;
    Dial* browserMixDial = nullptr;
    Dial* pitchSemitonesDial = nullptr;
    Dial* pitchFineDial = nullptr;
    Dial* pitchFormantDial = nullptr;
    std::array<Dial*, 6> pitchExtraDials {};
    juce::Label pitchAlgorithmLabel;
    std::array<juce::ToggleButton, 7> pitchAlgorithmButtons;
    std::unique_ptr<juce::ParameterAttachment> pitchAlgorithmAttachment;
    std::array<std::array<Dial*, transform::harmony::controlsPerVoice>,
               transform::harmony::voiceCount> harmonyDials {};
    std::array<juce::ToggleButton, transform::harmony::voiceCount> harmonyVoicePower;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>, transform::harmony::voiceCount> harmonyVoiceAttachments;
    juce::Label harmonyModeLabel;
    std::array<juce::ToggleButton, 3> harmonyModeButtons;
    std::unique_ptr<juce::ParameterAttachment> harmonyModeAttachment;
    std::array<Dial*, 9> freqDials {};
    juce::Label freqModeLabel;
    std::array<juce::ToggleButton, 4> freqModeButtons;
    juce::ToggleButton freqSyncButton, freqRelativeButton;
    Dial* freqRelativeDial = nullptr;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> freqRelativeAttachment;
    Dial* freqDivisionDial = nullptr;
    std::unique_ptr<juce::ParameterAttachment> freqModeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> freqSyncAttachment;
    std::array<Dial*, 12> freezeDials {};
    juce::Label freezeVoiceModeLabel;
    std::array<juce::ToggleButton, transform::freeze::voiceModeNames.size()> freezeVoiceModeButtons;
    std::unique_ptr<juce::ParameterAttachment> freezeVoiceModeAttachment;
    juce::Label freezeAlgorithmLabel;
    std::array<juce::ToggleButton, transform::freeze::algorithmNames.size()> freezeAlgorithmButtons;
    std::unique_ptr<juce::ParameterAttachment> freezeAlgorithmAttachment;
    juce::Label freezeModeLabel, freezeDirectionLabel;
    std::array<juce::ToggleButton, transform::freeze::modeNames.size()> freezeModeButtons;
    std::array<juce::ToggleButton, transform::freeze::directionNames.size()> freezeDirectionButtons;
    std::unique_ptr<juce::ParameterAttachment> freezeModeAttachment, freezeDirectionAttachment;
    std::array<Dial*, 9> morphDials {};
    juce::Label morphModeLabel;
    std::array<juce::ToggleButton, 3> morphModeButtons;
    std::unique_ptr<juce::ParameterAttachment> morphModeAttachment;
    std::array<Dial*, 16> vocoderDials {};
    Dial* vocoderBandsDial = nullptr;
    juce::Label vocoderCarrierLabel;
    std::array<juce::ToggleButton, 4> vocoderCarrierButtons;
    std::unique_ptr<juce::ParameterAttachment> vocoderCarrierAttachment;
    std::array<Dial*, 7> deEssDials {};
    std::array<Dial*, 12> channelDials {};
    juce::Label deEssModeLabel, deEssMonitorLabel;
    std::array<juce::ToggleButton, transform::deess::modeNames.size()> deEssModeButtons;
    std::array<juce::ToggleButton, transform::deess::monitorNames.size()> deEssMonitorButtons;
    std::unique_ptr<juce::ParameterAttachment> deEssModeAttachment, deEssMonitorAttachment;
    class ReductionMeter final : public juce::Component
    {
    public:
        explicit ReductionMeter(float maximum = 24.0f) : maxReduction(maximum) {}
        void setReduction(float db);
        void paint(juce::Graphics&) override;
    private:
        float reduction = 0.0f;
        const float maxReduction;
    };
    ReductionMeter deEssMeter;
    ReductionMeter channelMeter { 48.0f };
    Dial* tuneKeyDial = nullptr;
    Dial* tuneAmountDial = nullptr;
    Dial* tuneSpeedDial = nullptr;
    Dial* tuneToleranceDial = nullptr;
    Dial* tuneVibratoDial = nullptr;
    Dial* tuneReferenceDial = nullptr;
    std::array<juce::ToggleButton, 12> tuneCustomButtons;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>, 12> tuneCustomAttachments;
    juce::Label tuneScaleLabel;
    SteppedComboBox tuneScaleBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> tuneScaleAttachment;
    std::unique_ptr<juce::Drawable> logoDrawable;
    juce::Label versionLabel;
    ZoomHitButton logoHitArea;
    bool zoomMenuOpen = false;
    std::array<juce::ToggleButton, transform::modules::count> moduleTabs;
    std::array<juce::ToggleButton, transform::modules::count> modulePower;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>,
               transform::modules::count> moduleAttachments;
    class TunerDisplay final : public juce::Component
    {
    public:
        void setReading(float hz, bool enabled, bool masterPower, int targetNote, float referenceHz);
        void paint(juce::Graphics&) override;
    private:
        float frequency = 0.0f;
        float reference = 440.0f;
        int targetMidi = -1;
        bool tuningEnabled = false;
        bool masterEnabled = true;
    } tunerDisplay;
    std::uint32_t lastTuneRevision = 0;
    int staleTuneTicks = 10;
    void updateModuleEditorVisibility();
    void updateTunerDisplay();
    int selectedModule = 0;
    juce::ToggleButton bypassButton;
    std::unique_ptr<juce::ParameterAttachment> bypassAttachment;
    juce::ToggleButton mixLockButton;
    juce::ToggleButton presetBrowserButton;
    juce::ToggleButton previousPresetButton;
    juce::ToggleButton nextPresetButton;
    PresetDisplayLabel currentPresetLabel;
    juce::TreeView presetFolderTree;
    PresetListBox presetList { "Preset list", this };
    juce::Label presetFolderLabel;
    juce::Label presetListLabel;
    juce::ToggleButton presetSaveButton;
    juce::ToggleButton presetSaveAsButton;
    juce::ToggleButton presetOpenFolderButton;
    juce::ToggleButton midiLearnButton;
    juce::ToggleButton randomButton { "!!!" };
    juce::ToggleButton sequencerButton { "SEQ" };
    std::unique_ptr<SequencerPanel> sequencerPanel;
    std::unique_ptr<FolderTreeItem> presetFolderRoot;
    std::unique_ptr<juce::FileChooser> presetFileChooser;
    std::unique_ptr<juce::AlertWindow> presetFolderNameWindow;
    juce::Array<juce::File> visiblePresetFiles;
    juce::File selectedPresetFolder;
    juce::File currentPresetFile;
    juce::File copiedPresetFile;
    juce::StringArray favouritePresetPaths;
    juce::Label information;
    std::unique_ptr<MidiLearnOverlay> midiLearnOverlay;
    void setMidiLearnOpen(bool);

    static constexpr int designWidth = 926;
    static constexpr int collapsedDesignHeight = 276;
    static constexpr int expandedDesignHeight = 530;
    int activeDesignHeight() const noexcept
    {
        return sequencerOpen ? expandedDesignHeight : collapsedDesignHeight;
    }

    Dial* addDial(const char*, const char*, const char*,
                  bool attachToParameter = true);
    bool restoreDialFromSavedState(Dial&);
    void selectModule(int);
    void randomiseSelectedModule();
    void updateModuleStatus();
    juce::Colour filterAccent(int filterIndex) const;
    float themeAmount() const;
    void setPresetBrowserOpen(bool);
    void updatePresetBrowserVisibility();
    void rebuildPresetFolderTree();
    void selectPresetFolder(const juce::File&);
    void selectFavouritePresets();
    void refreshPresetList();
    void updatePresetNavigationDisplay();
    void setSequencerOpen(bool);
    void showZoomMenu();
    void setZoomPercent(int);
    void updateZoomLimits();
    void updateSequencerTargetHighlights();
    void updateAnimatedParameterDisplays();
    bool handlePresetCursorKey(int direction);
    void stepPreset(int direction);
    void showPresetQuickMenu();
    void loadPresetFile(const juce::File&);
    void loadPresetAtBrowserCell(int row, int column);
    void showPresetContextMenu(const juce::File&, juce::Point<int>);
    void showPresetFolderContextMenu(const juce::File&, bool,
                                     juce::Point<int>);
    void copyPreset(const juce::File&);
    void pastePresetIntoFolder(const juce::File&);
    void showCreatePresetFolderDialog(const juce::File&);
    void confirmDeletePresetFolder(const juce::File&);
    juce::Array<juce::File> favouritePresetFiles() const;
    juce::Array<juce::File> navigationPresetFiles() const;
    bool isPresetFavourite(const juce::File&) const;
    void togglePresetFavourite(const juce::File&);
    void loadFavouritePresets();
    void saveFavouritePresets() const;
    void saveCurrentPreset();
    void showPresetSaveAs();
    int getNumRows() override;
    void paintListBoxItem(int, juce::Graphics&, int, int, bool) override;
    void listBoxItemClicked(int, const juce::MouseEvent&) override;
    void returnKeyPressed(int) override;
    void selectedRowsChanged(int) override;
    void timerCallback() override;
    void saveInterfaceSettings(bool setZoomDefault = false) const;
    bool zoomTrackingReady = false;
    int observedEditorWidth = designWidth;

    double observedThemeValue = 100.0;
    int stableSettingsTicks = 0;
    bool settingsDirty = false;
    bool restoringLockedMix = false;
    double lockedMixValue = 35.0;
    bool presetBrowserOpen = false;
    bool favouritesViewSelected = false;
    bool sequencerOpen = false;
    bool previousPresetKeyDown = false;
    bool nextPresetKeyDown = false;
    int selectedPresetBrowserColumn = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TransformAudioProcessorEditor)
};
