#include "InspectorComponent.h"

namespace te = tracktion_engine;

static void styleLabel (juce::Label& l)
{
    l.setFont (juce::Font (11.0f));
    l.setColour (juce::Label::textColourId, juce::Colour (0xff888888));
}

static void styleEditor (juce::TextEditor& e)
{
    e.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff2a2a2a));
    e.setColour (juce::TextEditor::textColourId,       juce::Colours::white);
    e.setColour (juce::TextEditor::outlineColourId,    juce::Colour (0xff444444));
    e.setFont   (juce::Font (14.0f));
}

static void styleCombo (juce::ComboBox& c)
{
    c.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff2a2a2a));
    c.setColour (juce::ComboBox::textColourId,       juce::Colours::white);
    c.setColour (juce::ComboBox::outlineColourId,    juce::Colour (0xff444444));
}

static void styleBtn (juce::TextButton& b, juce::Colour bg)
{
    b.setColour (juce::TextButton::buttonColourId,  bg);
    b.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
}

// ── Constructor ───────────────────────────────────────────────────────────────

InspectorComponent::InspectorComponent (PlaybackEngine& e) : engine (e)
{
    engine.addListener (this);

    styleLabel (sectionLabel);
    addAndMakeVisible (sectionLabel);

    styleLabel (trackNameLabel);
    addAndMakeVisible (trackNameLabel);

    styleEditor (trackNameEditor);
    trackNameEditor.setEnabled (false);
    trackNameEditor.onReturnKey = [this]
    {
        if (watchedTrack)
            watchedTrack->setName (trackNameEditor.getText());
    };
    addAndMakeVisible (trackNameEditor);

    styleLabel (outputLabel);
    addAndMakeVisible (outputLabel);

    styleCombo (outputCombo);
    outputCombo.setEnabled (false);
    outputCombo.onChange = [this] { applyOutputSelection(); };
    addAndMakeVisible (outputCombo);

    styleLabel (muteLabel);
    addAndMakeVisible (muteLabel);

    muteButton.setColour (juce::ToggleButton::textColourId, juce::Colours::white);
    muteButton.setEnabled (false);
    muteButton.onStateChange = [this]
    {
        if (auto* at = dynamic_cast<te::AudioTrack*> (watchedTrack))
            at->setMute (muteButton.getToggleState());
    };
    addAndMakeVisible (muteButton);

    // Volume slider (dB)
    styleLabel (volumeLabel);
    addAndMakeVisible (volumeLabel);

    volumeSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    volumeSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 48, 20);
    volumeSlider.setRange (-60.0, 6.0, 0.1);
    volumeSlider.setValue (0.0, juce::dontSendNotification);
    volumeSlider.setEnabled (false);
    volumeSlider.setColour (juce::Slider::trackColourId,     juce::Colour (0xff004d50));
    volumeSlider.setColour (juce::Slider::thumbColourId,     juce::Colour (0xff00acc1));
    volumeSlider.setColour (juce::Slider::backgroundColourId, juce::Colour (0xff222222));
    volumeSlider.onValueChange = [this]
    {
        if (auto* at = dynamic_cast<te::AudioTrack*> (watchedTrack))
            if (auto* vp = at->getVolumePlugin())
                vp->setVolumeDb ((float) volumeSlider.getValue());
    };
    addAndMakeVisible (volumeSlider);

    // Pan slider (-1 to +1)
    styleLabel (panLabel);
    addAndMakeVisible (panLabel);

    panSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    panSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 48, 20);
    panSlider.setRange (-1.0, 1.0, 0.01);
    panSlider.setValue (0.0, juce::dontSendNotification);
    panSlider.setEnabled (false);
    panSlider.setColour (juce::Slider::trackColourId,     juce::Colour (0xff004d50));
    panSlider.setColour (juce::Slider::thumbColourId,     juce::Colour (0xff00acc1));
    panSlider.setColour (juce::Slider::backgroundColourId, juce::Colour (0xff222222));
    panSlider.onValueChange = [this]
    {
        if (auto* at = dynamic_cast<te::AudioTrack*> (watchedTrack))
            if (auto* vp = at->getVolumePlugin())
                vp->setPan ((float) panSlider.getValue());
    };
    addAndMakeVisible (panSlider);

    // Clip info section
    clipSectionLabel.setColour (juce::Label::textColourId, juce::Colour (0xff888888));
    clipSectionLabel.setFont (juce::Font (11.f));
    addAndMakeVisible (clipSectionLabel);

    auto styleValueLabel = [](juce::Label& l)
    {
        l.setColour (juce::Label::textColourId,       juce::Colours::white);
        l.setColour (juce::Label::backgroundColourId, juce::Colour (0xff2a2a2a));
        l.setFont (juce::Font (12.f));
        l.setJustificationType (juce::Justification::centredLeft);
    };

    styleLabel (clipNameLabel);  styleValueLabel (clipNameValue);
    styleLabel (clipStartLabel); styleValueLabel (clipStartValue);
    styleLabel (clipLenLabel);   styleValueLabel (clipLenValue);

    addAndMakeVisible (clipNameLabel);  addAndMakeVisible (clipNameValue);
    addAndMakeVisible (clipStartLabel); addAndMakeVisible (clipStartValue);
    addAndMakeVisible (clipLenLabel);   addAndMakeVisible (clipLenValue);

    startTimerHz (10);

    // Import Audio button — always visible even when no track selected,
    // so the user knows how to get audio in.
    styleBtn (importBtn, juce::Colour (0xff1b5e20));
    importBtn.setEnabled (false);
    importBtn.onClick = [this] { launchImportDialog(); };
    addAndMakeVisible (importBtn);

    styleBtn (addTrackBtn,    juce::Colour (0xff1565c0));
    styleBtn (deleteTrackBtn, juce::Colour (0xff6a1b9a));
    deleteTrackBtn.setEnabled (false);

    addTrackBtn.onClick = [this]
    {
        int n = 1;
        if (auto* ed = engine.getCurrentEdit())
            ed->getTrackList().visitAllTopLevel ([&] (te::Track& t) { if (t.isAudioTrack()) ++n; return true; });
        engine.addAudioTrack ("Audio " + juce::String (n));
    };

    deleteTrackBtn.onClick = [this]
    {
        if (watchedTrack)
            engine.removeTrack (watchedTrack);
    };

    addAndMakeVisible (addTrackBtn);
    addAndMakeVisible (deleteTrackBtn);

    populateOutputCombo();
}

InspectorComponent::~InspectorComponent()
{
    stopTimer();
    engine.removeListener (this);
}

// ── Layout ────────────────────────────────────────────────────────────────────

void InspectorComponent::resized()
{
    auto area = getLocalBounds().reduced (8);

    sectionLabel.setBounds (area.removeFromTop (22));
    area.removeFromTop (2);

    trackNameLabel.setBounds (area.removeFromTop (16));
    trackNameEditor.setBounds (area.removeFromTop (28));
    area.removeFromTop (8);

    outputLabel.setBounds (area.removeFromTop (16));
    outputCombo.setBounds (area.removeFromTop (28));
    area.removeFromTop (8);

    muteLabel.setBounds  (area.removeFromTop (16));
    muteButton.setBounds (area.removeFromTop (24));
    area.removeFromTop (10);

    volumeLabel.setBounds  (area.removeFromTop (16));
    volumeSlider.setBounds (area.removeFromTop (28));
    area.removeFromTop (6);

    panLabel.setBounds  (area.removeFromTop (16));
    panSlider.setBounds (area.removeFromTop (28));
    area.removeFromTop (10);

    // Clip section
    clipSectionLabel.setBounds (area.removeFromTop (18));

    auto layoutPair = [&] (juce::Label& lbl, juce::Label& val)
    {
        auto row = area.removeFromTop (16);
        lbl.setBounds (row.removeFromLeft (44));
        val.setBounds (row);
        area.removeFromTop (2);
    };
    layoutPair (clipNameLabel,  clipNameValue);
    layoutPair (clipStartLabel, clipStartValue);
    layoutPair (clipLenLabel,   clipLenValue);
    area.removeFromTop (8);

    importBtn.setBounds (area.removeFromTop (32));

    // Bottom management buttons
    auto bot = getLocalBounds().reduced (8).removeFromBottom (76);
    deleteTrackBtn.setBounds (bot.removeFromBottom (32).reduced (0, 2));
    addTrackBtn.setBounds    (bot.removeFromBottom (32).reduced (0, 2));
}

void InspectorComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1c1c1c));
    g.setColour (juce::Colour (0xff303030));
    g.drawVerticalLine (0, 0.0f, (float) getHeight());

    if (watchedTrack == nullptr)
    {
        g.setColour (juce::Colour (0xff555555));
        g.setFont (12.0f);
        g.drawText ("Select a track to\ninspect and route it.",
                    getLocalBounds().withTrimmedTop (200),
                    juce::Justification::centredTop);
    }
}

// ── Import dialog ─────────────────────────────────────────────────────────────

void InspectorComponent::launchImportDialog()
{
    auto* at = dynamic_cast<te::AudioTrack*> (watchedTrack);
    if (! at) return;

    fileChooser = std::make_unique<juce::FileChooser> (
        "Import Audio File",
        juce::File::getSpecialLocation (juce::File::userMusicDirectory),
        "*.wav;*.aif;*.aiff;*.mp3;*.flac;*.caf;*.m4a",
        true);   // true = use native dialog

    fileChooser->launchAsync (
        juce::FileBrowserComponent::openMode |
        juce::FileBrowserComponent::canSelectFiles |
        juce::FileBrowserComponent::canSelectMultipleItems,
        [this, at] (const juce::FileChooser& fc)
        {
            double insertSec = engine.getCurrentPositionSeconds();
            for (auto& f : fc.getResults())
            {
                engine.importAudioFile (at, f, insertSec);
                // Stack multiple files end-to-end (engine returns correct duration).
                insertSec += 1.0;   // placeholder gap; engine sets real duration
            }
        });
}

// ── Output combo ──────────────────────────────────────────────────────────────

void InspectorComponent::populateOutputCombo()
{
    outputCombo.clear (juce::dontSendNotification);

    auto devices = engine.getEngine().getDeviceManager().getWaveOutputDevices();
    int id = 1;
    for (auto* d : devices)
        outputCombo.addItem (d->getName(), id++);

    if (outputCombo.getNumItems() == 0)
        outputCombo.addItem ("(no audio device)", 1);
}

void InspectorComponent::applyOutputSelection()
{
    auto* at = dynamic_cast<te::AudioTrack*> (watchedTrack);
    if (! at) return;

    const int idx = outputCombo.getSelectedItemIndex();
    auto devices = engine.getEngine().getDeviceManager().getWaveOutputDevices();
    if (idx >= 0 && idx < (int) devices.size())
        engine.setTrackOutputDevice (at, devices[idx]->getName());
}

// ── Update from track ─────────────────────────────────────────────────────────

void InspectorComponent::updateFromTrack (te::Track* track)
{
    watchedTrack = track;

    const bool hasTrack   = (track != nullptr);
    const bool isAudio    = hasTrack && (dynamic_cast<te::AudioTrack*> (track) != nullptr);

    trackNameEditor.setEnabled (hasTrack);
    outputCombo.setEnabled     (isAudio);
    muteButton.setEnabled      (isAudio);
    volumeSlider.setEnabled    (isAudio);
    panSlider.setEnabled       (isAudio);
    importBtn.setEnabled       (isAudio);
    deleteTrackBtn.setEnabled  (hasTrack);

    if (! hasTrack)
    {
        trackNameEditor.setText ({}, juce::dontSendNotification);
        outputCombo.setSelectedId (0, juce::dontSendNotification);
        muteButton.setToggleState (false, juce::dontSendNotification);
        volumeSlider.setValue (0.0, juce::dontSendNotification);
        panSlider.setValue    (0.0, juce::dontSendNotification);
        repaint();
        return;
    }

    trackNameEditor.setText (track->getName(), juce::dontSendNotification);

    if (auto* at = dynamic_cast<te::AudioTrack*> (track))
    {
        muteButton.setToggleState (at->isMuted (false), juce::dontSendNotification);

        if (auto* vp = at->getVolumePlugin())
        {
            if (! volumeSlider.isMouseButtonDown())
                volumeSlider.setValue ((double) vp->getVolumeDb(), juce::dontSendNotification);
            if (! panSlider.isMouseButtonDown())
                panSlider.setValue ((double) vp->getPan(), juce::dontSendNotification);
        }

        const juce::String curID = at->getOutput().getOutputDeviceID();
        auto devices = engine.getEngine().getDeviceManager().getWaveOutputDevices();
        for (int i = 0; i < (int) devices.size(); ++i)
        {
            if (devices[i]->getName() == curID)
            {
                outputCombo.setSelectedItemIndex (i, juce::dontSendNotification);
                break;
            }
        }
    }

    repaint();
}

void InspectorComponent::updateFromClip (te::Clip* clip)
{
    if (clip == nullptr)
    {
        clipNameValue.setText  ({}, juce::dontSendNotification);
        clipStartValue.setText ({}, juce::dontSendNotification);
        clipLenValue.setText   ({}, juce::dontSendNotification);
        return;
    }

    const double startSec = clip->getPosition().getStart().inSeconds();
    const double lenSec   = clip->getPosition().getLength().inSeconds();

    clipNameValue.setText (clip->getName(), juce::dontSendNotification);

    auto secToHMS = [] (double s) -> juce::String
    {
        const int m  = (int) s / 60;
        const int sc = (int) s % 60;
        const int ms = (int) ((s - std::floor (s)) * 1000.0);
        return juce::String (m) + ":"
             + juce::String (sc).paddedLeft ('0', 2) + "."
             + juce::String (ms).paddedLeft ('0', 3);
    };

    clipStartValue.setText (secToHMS (startSec), juce::dontSendNotification);
    clipLenValue.setText   (secToHMS (lenSec),   juce::dontSendNotification);
}

void InspectorComponent::timerCallback()
{
    if (auto* at = dynamic_cast<te::AudioTrack*> (watchedTrack))
    {
        muteButton.setToggleState (at->isMuted (false), juce::dontSendNotification);

        if (auto* vp = at->getVolumePlugin())
        {
            if (! volumeSlider.isMouseButtonDown())
                volumeSlider.setValue ((double) vp->getVolumeDb(), juce::dontSendNotification);
            if (! panSlider.isMouseButtonDown())
                panSlider.setValue ((double) vp->getPan(), juce::dontSendNotification);
        }
    }

    // Keep clip position info fresh while dragging.
    updateFromClip (engine.getSelectedClip());
}
