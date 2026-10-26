#include "PluginEditor.h"

namespace
{
    juce::File userTaalFolder()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("TablaVST").getChildFile ("Taals");
    }

    void styleSlider (juce::Slider& s, double step, int boxW = 70)
    {
        s.setSliderStyle (juce::Slider::IncDecButtons);
        s.setIncDecButtonsMode (juce::Slider::incDecButtonsDraggable_AutoDirection);
        s.setTextBoxStyle (juce::Slider::TextBoxLeft, false, boxW, 24);
        s.setNumDecimalPlacesToDisplay (step < 0.1 ? 2 : 1);
    }
}

// ---------------------------------------------------------------------------------------------
BolPalette::BolPalette (std::function<void (const juce::String&)> onPick)
{
    auto add = [&] (const juce::String& label, const juce::String& bol, const juce::String& tip)
    {
        auto* b = buttons.add (new juce::TextButton (label));
        b->setTooltip (tip);
        b->onClick = [=] { onPick (bol); };
        addAndMakeVisible (b);
    };
    for (auto& d : BolLibrary::defs())
        add (d.name, d.name, d.description + (d.parts.isEmpty() ? "" : "  (compound)"));
    add ("Rest", "Rest", "Silent beat");
}

int BolPalette::getRequiredHeight (int width) const
{
    int x = 0, rows = 1;
    for (auto* b : buttons)
    {
        const int w = juce::jmax (46, juce::GlyphArrangement::getStringWidthInt (juce::Font (juce::FontOptions (14.0f)), b->getButtonText()) + 22);
        if (x + w > width && x > 0) { x = 0; ++rows; }
        x += w + 6;
    }
    return rows * 32;
}

void BolPalette::resized()
{
    int x = 0, y = 0;
    for (auto* b : buttons)
    {
        const int w = juce::jmax (46, juce::GlyphArrangement::getStringWidthInt (juce::Font (juce::FontOptions (14.0f)), b->getButtonText()) + 22);
        if (x + w > getWidth() && x > 0) { x = 0; y += 32; }
        b->setBounds (x, y, w, 26);
        x += w + 6;
    }
}

// ---------------------------------------------------------------------------------------------
TablaEditor::TablaEditor (TablaProcessor& p)
    : AudioProcessorEditor (&p), processor (p),
      taalView (p, [this]() -> const Taal& { return library[(size_t) current].taal; }),
      palette ([this] (const juce::String& bol) { insertBol (bol); })
{
    using namespace juce;

    laf.setColourScheme (LookAndFeel_V4::getLightColourScheme());
    laf.setColour (ResizableWindow::backgroundColourId, Palette::background);
    laf.setColour (TextButton::buttonColourId, Palette::panelLight);
    laf.setColour (TextButton::buttonOnColourId, Palette::accentDeep);
    laf.setColour (Slider::thumbColourId, Palette::accent);
    laf.setColour (ToggleButton::tickColourId, Palette::accent);
    laf.setColour (TextEditor::backgroundColourId, Palette::panel);
    laf.setColour (TextEditor::outlineColourId, Palette::panelLight);
    laf.setColour (TextEditor::focusedOutlineColourId, Palette::accent);
    laf.setColour (ListBox::backgroundColourId, Palette::panel);
    laf.setColour (ComboBox::backgroundColourId, Palette::panel);
    laf.setColour (Slider::textBoxBackgroundColourId, Palette::panel);
    setLookAndFeel (&laf);

    // library = built-ins + user files
    for (auto& t : BuiltinTaals::create()) library.push_back ({ t, {} });
    loadUserTaals();

    // adopt whatever taal the processor currently holds (restored session state)
    {
        const auto& pt = processor.getTaal();
        int found = -1;
        for (int i = 0; i < (int) library.size(); ++i)
            if (library[(size_t) i].taal.name == pt.name) { found = i; break; }
        if (found < 0) { library.push_back ({ pt, {} }); found = (int) library.size() - 1; }
        else library[(size_t) found].taal = pt;
        current = found;
    }

    // ---- top bar ----
    for (auto* c : std::initializer_list<Component*> { &newButton, &openButton, &saveButton, &saveAsButton, &playButton,
                                                      &loopButton, &syncButton,
                                                      &tempoLabel, &pitchLabel, &volumeLabel, &noteLabel,
                                                      &tempoSlider, &pitchSlider, &volumeSlider, &tempoReset, &pitchReset })
        addAndMakeVisible (c);

    playButton.setClickingTogglesState (false);
    playButton.onClick = [this] { processor.setPlaying (! processor.isPlaying()); };
    newButton.onClick = [this] { newTaal(); };
    openButton.onClick = [this] { openTaal(); };
    saveButton.onClick = [this] { saveTaal (false); };
    saveAsButton.onClick = [this] { saveTaal (true); };
    syncButton.setTooltip ("Follow the host transport and tempo (VST). When off, use the Play button.");
    noteLabel.setColour (Label::textColourId, Palette::accent);

    styleSlider (tempoSlider, 0.01, 84);
    styleSlider (pitchSlider, 0.01, 70);
    styleSlider (volumeSlider, 0.1, 60);
    tempoSlider.setNumDecimalPlacesToDisplay (2);
    tempoAtt = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (processor.apvts, "tempo", tempoSlider);
    pitchAtt = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (processor.apvts, "pitch", pitchSlider);
    volumeAtt = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (processor.apvts, "volume", volumeSlider);
    loopAtt = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (processor.apvts, "loop", loopButton);
    syncAtt = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (processor.apvts, "hostSync", syncButton);
    tempoReset.onClick = [this] { tempoSlider.setValue (working().defaultTempo); };
    pitchReset.onClick = [this] { pitchSlider.setValue (0.0); };
    pitchSlider.onValueChange = [this]
    {
        const int note = 61 + (int) std::round (pitchSlider.getValue());   // C#4 is the tabla's natural pitch
        noteLabel.setText (MidiMessage::getMidiNoteName (note, true, false, 4), dontSendNotification);
    };
    pitchSlider.onValueChange();

    // ---- left column ----
    addAndMakeVisible (listTitle);
    addAndMakeVisible (filterBox);
    addAndMakeVisible (taalList);
    filterBox.setTextToShowWhenEmpty ("Search taals...", Palette::dim);
    filterBox.onTextChange = [this] { rebuildVisible(); };
    taalList.setRowHeight (24);

    for (auto* c : std::initializer_list<Component*> { &bolNumberLabel, &nameLabel, &durLabel, &empLabel, &playBolButton,
                                                      &bolBox, &durSlider, &empSlider })
        addAndMakeVisible (c);

    bolBox.addItem ("Rest", 1);
    {
        int id = 2;
        for (auto& n : BolLibrary::playableNames()) bolBox.addItem (n, id++);
    }
    styleSlider (durSlider, 0.01, 70);
    durSlider.setRange (0.0625, 32.0, 0.0625);
    durSlider.setNumDecimalPlacesToDisplay (3);
    styleSlider (empSlider, 0.5, 70);
    empSlider.setRange (-12.0, 12.0, 0.5);

    bolBox.onChange = [this]
    {
        if (updatingUi || selectedBol < 0) return;
        working().events[(size_t) selectedBol].bol = bolBox.getText();
        commit();
        processor.previewBol (bolBox.getText(), working().events[(size_t) selectedBol].emphasisDb);
    };
    durSlider.onValueChange = [this]
    {
        if (updatingUi || selectedBol < 0) return;
        working().events[(size_t) selectedBol].duration = durSlider.getValue();
        commit();
    };
    empSlider.onValueChange = [this]
    {
        if (updatingUi || selectedBol < 0) return;
        working().events[(size_t) selectedBol].emphasisDb = (float) empSlider.getValue();
        commit();
    };
    playBolButton.onClick = [this]
    {
        if (selectedBol >= 0)
        {
            auto& e = working().events[(size_t) selectedBol];
            processor.previewBol (e.bol, e.emphasisDb);
        }
    };

    // ---- centre ----
    addAndMakeVisible (viewport);
    viewport.setViewedComponent (&taalView, false);
    viewport.setScrollBarsShown (true, false);
    taalView.onSelect = [this] (int i) { selectBol (i); };
    taalView.onPlayBol = [this] (int i)
    {
        auto& e = working().events[(size_t) i];
        processor.previewBol (e.bol, e.emphasisDb);
    };
    taalView.onTogglePlay = [this] { playButton.onClick(); };
    taalView.onDelete = [this] { deleteButton.onClick(); };
    taalView.onMove = [this] (int from, int to)
    {
        auto& ev = working().events;
        auto moved = ev[(size_t) from];
        ev.erase (ev.begin() + from);
        ev.insert (ev.begin() + to, moved);
        selectedBol = to;
        commit();
    };

    addAndMakeVisible (palette);
    for (auto* b : { &deleteButton, &dupButton, &leftButton, &rightButton }) addAndMakeVisible (b);
    deleteButton.onClick = [this]
    {
        if (selectedBol < 0) return;
        auto& ev = working().events;
        ev.erase (ev.begin() + selectedBol);
        selectedBol = ev.empty() ? -1 : std::min (selectedBol, (int) ev.size() - 1);
        commit();
    };
    dupButton.onClick = [this]
    {
        if (selectedBol < 0) return;
        auto& ev = working().events;
        ev.insert (ev.begin() + selectedBol + 1, ev[(size_t) selectedBol]);
        ++selectedBol;
        commit();
    };
    leftButton.onClick = [this]
    {
        if (selectedBol <= 0) return;
        std::swap (working().events[(size_t) selectedBol], working().events[(size_t) selectedBol - 1]);
        --selectedBol;
        commit();
    };
    rightButton.onClick = [this]
    {
        if (selectedBol < 0 || selectedBol >= (int) working().events.size() - 1) return;
        std::swap (working().events[(size_t) selectedBol], working().events[(size_t) selectedBol + 1]);
        ++selectedBol;
        commit();
    };

    addAndMakeVisible (textTitle);
    addAndMakeVisible (textBox);
    addAndMakeVisible (errorLabel);
    addAndMakeVisible (nameBox);
    addAndMakeVisible (applyButton);
    addAndMakeVisible (revertButton);
    textBox.setMultiLine (true, true);
    textBox.setReturnKeyStartsNewLine (false);
    textBox.setFont (FontOptions ("Georgia", 18.0f, Font::plain));
    textBox.setTooltip ("Bols separated by commas  -  Bol[duration] sets length in beats, Bol{+3} sets emphasis in dB, - is a rest.");
    textBox.onTextChange = [this] { textDirty = true; };
    textBox.onReturnKey = [this] { applyText(); };
    errorLabel.setColour (Label::textColourId, Palette::error);
    nameBox.setTextToShowWhenEmpty ("Taal name", Palette::dim);
    nameBox.onTextChange = [this]
    {
        if (updatingUi) return;
        working().name = nameBox.getText();
        processor.setTaal (working());
        taalView.repaint();
        taalList.repaint();
    };
    applyButton.onClick = [this] { applyText(); };
    revertButton.onClick = [this] { refreshText (true); errorLabel.setText ({}, dontSendNotification); };

    processor.addChangeListener (this);
    rebuildVisible();
    selectTaal (current, false);

    // Only text boxes and the composer keep keyboard focus; everything else hands keys to the editor
    // so Space (play/stop) and Delete work regardless of what was clicked last.
    std::function<void (juce::Component&)> noFocus = [&] (juce::Component& c)
    {
        for (auto* ch : c.getChildren())
        {
            if (dynamic_cast<juce::TextEditor*> (ch) != nullptr || ch == &taalView) continue;
            ch->setWantsKeyboardFocus (false);
            noFocus (*ch);
        }
    };
    noFocus (*this);
    setWantsKeyboardFocus (true);
    viewport.setWantsKeyboardFocus (false);

    setResizable (true, true);
    setResizeLimits (1000, 640, 2200, 1400);
    setSize (1280, 840);
    startTimerHz (30);
}

TablaEditor::~TablaEditor()
{
    processor.removeChangeListener (this);
    setLookAndFeel (nullptr);
}

// ---------------------------------------------------------------------------------------------
void TablaEditor::loadUserTaals()
{
    auto folder = userTaalFolder();
    folder.createDirectory();
    for (auto& f : folder.findChildFiles (juce::File::findFiles, false, "*.taalv,*.tal"))   // .tal = legacy
    {
        Taal t;
        if (Taal::fromJson (f.loadFileAsString(), t)) library.push_back ({ t, f });
    }
    std::stable_sort (library.begin(), library.end(),
                      [] (const Entry& a, const Entry& b) { return a.taal.name.compareIgnoreCase (b.taal.name) < 0; });
}

void TablaEditor::rebuildVisible()
{
    const auto filter = filterBox.getText().trim();
    visible.clear();
    for (int i = 0; i < (int) library.size(); ++i)
        if (filter.isEmpty() || library[(size_t) i].taal.name.containsIgnoreCase (filter)) visible.push_back (i);
    taalList.updateContent();
    const auto it = std::find (visible.begin(), visible.end(), current);
    if (it != visible.end()) taalList.selectRow ((int) (it - visible.begin()), true, false);
    taalList.repaint();
}

void TablaEditor::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (row < 0 || row >= (int) visible.size()) return;
    if (selected) { g.setColour (Palette::accentDeep.withAlpha (0.6f)); g.fillRect (0, 0, w, h); }
    const auto& e = library[(size_t) visible[(size_t) row]];
    g.setColour (Palette::text);
    g.setFont (juce::FontOptions (15.0f));
    g.drawText (e.taal.name, 10, 0, w - 20, h, juce::Justification::centredLeft);
    if (e.file != juce::File())
    {
        g.setColour (Palette::accent);
        g.fillEllipse ((float) w - 14.0f, (float) h * 0.5f - 3.0f, 6.0f, 6.0f);   // dot = saved user taal
    }
}

void TablaEditor::selectedRowsChanged (int row)
{
    if (row < 0 || row >= (int) visible.size()) return;
    if (visible[(size_t) row] != current) selectTaal (visible[(size_t) row], true);
}

void TablaEditor::selectTaal (int libraryIndex, bool applyDefaultTempo)
{
    current = libraryIndex;
    selectedBol = -1;
    textDirty = false;
    if (applyDefaultTempo) tempoSlider.setValue (working().defaultTempo);
    processor.setTaal (working());
    refreshAll();
}

void TablaEditor::commit()
{
    processor.setTaal (working());
    refreshAll();
}

void TablaEditor::refreshAll()
{
    updatingUi = true;
    nameBox.setText (working().name, false);
    updatingUi = false;
    refreshText (false);
    refreshBolPanel();
    taalView.setSelected (selectedBol);
    taalView.setWidth (viewport.getMaximumVisibleWidth());
    taalView.repaint();
    taalList.repaint();
}

void TablaEditor::refreshText (bool force)
{
    if (textDirty && ! force) return;
    textBox.setText ("Tabla: " + working().toText(), false);
    textDirty = false;
}

void TablaEditor::refreshBolPanel()
{
    const bool has = selectedBol >= 0 && selectedBol < (int) working().events.size();
    updatingUi = true;
    bolNumberLabel.setText (has ? "Bol Number: " + juce::String (selectedBol + 1) + " of " + juce::String (working().events.size())
                                : juce::String ("Bol Number: -"), juce::dontSendNotification);
    for (auto* c : std::initializer_list<juce::Component*> { &playBolButton, &bolBox, &durSlider, &empSlider })
        c->setEnabled (has);
    if (has)
    {
        auto& e = working().events[(size_t) selectedBol];
        bolBox.setText (e.bol, juce::dontSendNotification);
        durSlider.setValue (e.duration, juce::dontSendNotification);
        empSlider.setValue (e.emphasisDb, juce::dontSendNotification);
    }
    else
    {
        bolBox.setSelectedId (0, juce::dontSendNotification);
        durSlider.setValue (0.0, juce::dontSendNotification);
        empSlider.setValue (0.0, juce::dontSendNotification);
    }
    updatingUi = false;
}

void TablaEditor::selectBol (int index)
{
    selectedBol = index;
    refreshBolPanel();
    taalView.setSelected (index);
    if (index >= 0)
    {
        auto& e = working().events[(size_t) index];
        processor.previewBol (e.bol, e.emphasisDb);
    }
}

void TablaEditor::insertBol (const juce::String& bol)
{
    BolEvent e;
    e.bol = bol;
    auto& ev = working().events;
    const int at = selectedBol >= 0 ? selectedBol + 1 : (int) ev.size();
    ev.insert (ev.begin() + at, e);
    selectedBol = at;
    commit();
    processor.previewBol (bol, 0.0f);
}

void TablaEditor::applyText()
{
    std::vector<BolEvent> parsed;
    juce::String err;
    if (! Taal::parseText (textBox.getText(), parsed, err))
    {
        errorLabel.setText (err, juce::dontSendNotification);
        return;
    }
    errorLabel.setText ({}, juce::dontSendNotification);
    working().events = std::move (parsed);
    selectedBol = -1;
    textDirty = false;
    commit();
    refreshText (true);
}

void TablaEditor::newTaal()
{
    Taal t;
    t.name = "New Taal";
    t.defaultTempo = tempoSlider.getValue();
    library.push_back ({ t, {} });
    filterBox.clear();
    rebuildVisible();
    selectTaal ((int) library.size() - 1, false);
    rebuildVisible();
}

void TablaEditor::openTaal()
{
    chooser = std::make_unique<juce::FileChooser> ("Open taal", userTaalFolder(), "*.taalv;*.tal");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
    {
        const auto f = fc.getResult();
        Taal t;
        if (! f.existsAsFile()) return;
        if (! Taal::fromJson (f.loadFileAsString(), t))
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Open taal", "That file is not a valid .taalv file.");
            return;
        }
        library.push_back ({ t, f });
        filterBox.clear();
        rebuildVisible();
        selectTaal ((int) library.size() - 1, true);
        rebuildVisible();
    });
}

void TablaEditor::saveTaal (bool saveAs)
{
    working().defaultTempo = tempoSlider.getValue();

    auto write = [this] (const juce::File& f)
    {
        library[(size_t) current].file = f;
        f.getParentDirectory().createDirectory();
        if (! f.replaceWithText (working().toJson()))
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Save taal", "Could not write " + f.getFullPathName());
        processor.setTaal (working());
        refreshAll();
    };

    if (! saveAs && library[(size_t) current].file != juce::File())
    {
        write (library[(size_t) current].file);
        return;
    }

    auto suggested = userTaalFolder().getChildFile (juce::File::createLegalFileName (working().name) + ".taalv");
    chooser = std::make_unique<juce::FileChooser> ("Save taal", suggested, "*.taalv");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this, write] (const juce::FileChooser& fc)
    {
        auto f = fc.getResult();
        if (f == juce::File()) return;
        write (f.withFileExtension ("taalv"));
    });
}

void TablaEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // the host restored a session: adopt the processor's taal
    const auto& pt = processor.getTaal();
    int found = -1;
    for (int i = 0; i < (int) library.size(); ++i)
        if (library[(size_t) i].taal.name == pt.name) { found = i; break; }
    if (found < 0) { library.push_back ({ pt, {} }); found = (int) library.size() - 1; }
    else library[(size_t) found].taal = pt;
    rebuildVisible();
    selectTaal (found, false);
    rebuildVisible();
}

bool TablaEditor::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::spaceKey)
    {
        if (processor.apvts.getRawParameterValue ("hostSync")->load() < 0.5f)
            processor.setPlaying (! processor.isPlaying());
        return true;
    }
    if (k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey) { deleteButton.onClick(); return true; }
    return false;
}

void TablaEditor::mouseDown (const juce::MouseEvent&) { grabKeyboardFocus(); }

void TablaEditor::timerCallback()
{
    const bool sync = processor.apvts.getRawParameterValue ("hostSync")->load() > 0.5f;
    playButton.setButtonText (processor.isPlaying() ? "Stop" : "Play");
    playButton.setToggleState (processor.isPlaying(), juce::dontSendNotification);
    playButton.setEnabled (! sync);
    taalView.repaint();
}

// ---------------------------------------------------------------------------------------------
void TablaEditor::paint (juce::Graphics& g)
{
    g.fillAll (Palette::background);
    g.setColour (Palette::panel);
    g.fillRect (0, 0, getWidth(), 84);
    g.setColour (Palette::accent);
    g.fillRect (0, 84, getWidth(), 2);
}

void TablaEditor::resized()
{
    auto r = getLocalBounds();

    // top bar: two rows
    auto top = r.removeFromTop (84).reduced (10, 6);
    auto row1 = top.removeFromTop (30);
    for (auto* b : { &newButton, &openButton, &saveButton, &saveAsButton })
    {
        b->setBounds (row1.removeFromLeft (b == &openButton || b == &saveAsButton ? 84 : 64).reduced (0, 0));
        row1.removeFromLeft (6);
    }
    row1.removeFromLeft (16);
    playButton.setBounds (row1.removeFromLeft (80));
    row1.removeFromLeft (14);
    loopButton.setBounds (row1.removeFromLeft (70));
    syncButton.setBounds (row1.removeFromLeft (120));

    top.removeFromTop (8);
    auto row2 = top.removeFromTop (30);
    tempoLabel.setBounds (row2.removeFromLeft (92));
    tempoSlider.setBounds (row2.removeFromLeft (150));
    tempoReset.setBounds (row2.removeFromLeft (52).reduced (0, 2));
    row2.removeFromLeft (24);
    pitchLabel.setBounds (row2.removeFromLeft (112));
    pitchSlider.setBounds (row2.removeFromLeft (140));
    pitchReset.setBounds (row2.removeFromLeft (52).reduced (0, 2));
    noteLabel.setBounds (row2.removeFromLeft (50));
    row2.removeFromLeft (14);
    volumeLabel.setBounds (row2.removeFromLeft (84));
    volumeSlider.setBounds (row2.removeFromLeft (130));

    r.removeFromTop (6);

    // left column
    auto left = r.removeFromLeft (230).reduced (8, 4);
    listTitle.setBounds (left.removeFromTop (22));
    filterBox.setBounds (left.removeFromTop (26));
    left.removeFromTop (4);
    auto props = left.removeFromBottom (232);
    taalList.setBounds (left);

    props.removeFromTop (6);
    auto line = props.removeFromTop (28);
    bolNumberLabel.setBounds (line.removeFromLeft (150));
    playBolButton.setBounds (line);
    props.removeFromTop (8);
    nameLabel.setBounds (props.removeFromTop (20));
    bolBox.setBounds (props.removeFromTop (28));
    props.removeFromTop (8);
    durLabel.setBounds (props.removeFromTop (20));
    durSlider.setBounds (props.removeFromTop (28).removeFromLeft (190));
    props.removeFromTop (8);
    empLabel.setBounds (props.removeFromTop (20));
    empSlider.setBounds (props.removeFromTop (28).removeFromLeft (190));

    // centre column
    auto centre = r.reduced (8, 4);
    auto bottom = centre.removeFromBottom (96);
    {
        auto nameRow = bottom.removeFromBottom (30);
        applyButton.setBounds (nameRow.removeFromRight (90));
        nameRow.removeFromRight (6);
        revertButton.setBounds (nameRow.removeFromRight (90));
        nameRow.removeFromRight (10);
        nameBox.setBounds (nameRow.removeFromLeft (220));
        nameRow.removeFromLeft (10);
        errorLabel.setBounds (nameRow);
        bottom.removeFromBottom (6);
        textTitle.setBounds (bottom.removeFromTop (0));
        textBox.setBounds (bottom);
    }
    centre.removeFromBottom (6);

    const int palH = palette.getRequiredHeight (centre.getWidth());
    palette.setBounds (centre.removeFromBottom (palH));
    centre.removeFromBottom (4);
    auto edit = centre.removeFromBottom (30);
    deleteButton.setBounds (edit.removeFromLeft (80));
    edit.removeFromLeft (6);
    dupButton.setBounds (edit.removeFromLeft (90));
    edit.removeFromLeft (6);
    leftButton.setBounds (edit.removeFromLeft (80));
    edit.removeFromLeft (6);
    rightButton.setBounds (edit.removeFromLeft (80));
    centre.removeFromBottom (4);

    viewport.setBounds (centre);
    taalView.setWidth (viewport.getMaximumVisibleWidth());
}
