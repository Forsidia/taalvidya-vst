#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "TaalView.h"

// Wrapping row of buttons, one per bol.
class BolPalette : public juce::Component
{
public:
    explicit BolPalette (std::function<void (const juce::String&)> onPick);
    int getRequiredHeight (int width) const;
    void resized() override;

private:
    juce::OwnedArray<juce::TextButton> buttons;
};

class TablaEditor : public juce::AudioProcessorEditor,
                    private juce::ListBoxModel,
                    private juce::ChangeListener,
                    private juce::Timer
{
public:
    explicit TablaEditor (TablaProcessor&);
    ~TablaEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    struct Entry { Taal taal; juce::File file; };

    // ListBoxModel
    int getNumRows() override { return (int) visible.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void selectedRowsChanged (int row) override;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    // model helpers
    Taal& working() { return library[(size_t) current].taal; }
    void loadUserTaals();
    void rebuildVisible();
    void selectTaal (int libraryIndex, bool applyDefaultTempo);
    void commit();                       // push `working()` to the processor and refresh the UI
    void refreshAll();
    void refreshText (bool force);
    void refreshBolPanel();
    void selectBol (int index);
    void insertBol (const juce::String& bol);
    void applyText();
    void newTaal();
    void openTaal();
    void saveTaal (bool saveAs);

    TablaProcessor& processor;
    juce::LookAndFeel_V4 laf;
    juce::TooltipWindow tooltips { this, 600 };

    std::vector<Entry> library;
    std::vector<int> visible;
    int current = 0;
    int selectedBol = -1;
    bool textDirty = false;
    bool updatingUi = false;

    // top bar
    juce::TextButton newButton { "New" }, openButton { "Open..." }, saveButton { "Save" }, saveAsButton { "Save As..." };
    juce::TextButton playButton { "Play" };
    juce::ToggleButton loopButton { "Loop" }, syncButton { "Sync to host" };
    juce::Label tempoLabel { {}, "Tempo (bpm)" }, pitchLabel { {}, "Pitch (semitones)" }, volumeLabel { {}, "Volume (dB)" };
    juce::Label noteLabel;
    juce::Slider tempoSlider, pitchSlider, volumeSlider;
    juce::TextButton tempoReset { "Reset" }, pitchReset { "Reset" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> tempoAtt, pitchAtt, volumeAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> loopAtt, syncAtt;

    // left column
    juce::Label listTitle { {}, "All Taals" };
    juce::TextEditor filterBox;
    juce::ListBox taalList { "taals", this };

    // bol properties
    juce::Label bolNumberLabel, nameLabel { {}, "Name of Bol" }, durLabel { {}, "Duration (beats)" }, empLabel { {}, "Emphasis (dB)" };
    juce::TextButton playBolButton { "Play Bol" };
    juce::ComboBox bolBox;
    juce::Slider durSlider, empSlider;

    // centre
    TaalView taalView;
    juce::Viewport viewport;
    BolPalette palette;
    juce::TextButton deleteButton { "Delete" }, dupButton { "Duplicate" }, leftButton { "< Move" }, rightButton { "Move >" };
    juce::Label textTitle { {}, "Tabla:" };
    juce::TextEditor textBox;
    juce::Label errorLabel;
    juce::TextEditor nameBox;
    juce::TextButton applyButton { "Apply" }, revertButton { "Revert" };

    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TablaEditor)
};
