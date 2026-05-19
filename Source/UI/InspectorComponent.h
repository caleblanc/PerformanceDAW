#pragma once

#include <JuceHeader.h>
#include "../Engine/PlaybackEngine.h"

namespace te = tracktion_engine;

class InspectorComponent : public juce::Component,
                            public juce::Timer,
                            public PlaybackEngine::Listener
{
public:
    explicit InspectorComponent (PlaybackEngine& engine);
    ~InspectorComponent() override;

    void paint   (juce::Graphics& g) override;
    void resized() override;

private:
    PlaybackEngine& engine;
    te::Track*      watchedTrack = nullptr;

    // ── Track info ────────────────────────────────────────────────────────────
    juce::Label      sectionLabel   { {}, "INSPECTOR" };
    juce::Label      trackNameLabel { {}, "Name" };
    juce::TextEditor trackNameEditor;

    juce::Label      outputLabel { {}, "Output device" };
    juce::ComboBox   outputCombo;

    juce::Label      muteLabel  { {}, "Mute" };
    juce::ToggleButton muteButton;

    juce::Label  volumeLabel { {}, "Volume (dB)" };
    juce::Slider volumeSlider;
    juce::Label  panLabel    { {}, "Pan" };
    juce::Slider panSlider;

    // ── Clip info ─────────────────────────────────────────────────────────────
    juce::Label clipSectionLabel { {}, "CLIP" };
    juce::Label clipNameLabel    { {}, "Name" };
    juce::Label clipNameValue;
    juce::Label clipStartLabel   { {}, "Start" };
    juce::Label clipStartValue;
    juce::Label clipLenLabel     { {}, "Length" };
    juce::Label clipLenValue;

    void updateFromClip (te::Clip* clip);

    // ── Import button ─────────────────────────────────────────────────────────
    juce::TextButton importBtn { "Import Audio..." };
    // Must be kept alive until the async callback fires.
    std::unique_ptr<juce::FileChooser> fileChooser;

    // ── Track management ──────────────────────────────────────────────────────
    juce::TextButton addTrackBtn    { "+ Audio Track" };
    juce::TextButton deleteTrackBtn { "Delete Track" };

    // ── Helpers ───────────────────────────────────────────────────────────────
    void populateOutputCombo();
    void applyOutputSelection();
    void updateFromTrack (te::Track* track);
    void launchImportDialog();

    void trackSelectionChanged (te::Track* t) override { updateFromTrack (t); }
    void clipSelectionChanged  (te::Clip* c)  override { updateFromClip (c); }
    void activeSongChanged     (int)          override { populateOutputCombo(); repaint(); }
    void trackListChanged      ()             override { repaint(); }
    void timerCallback         ()             override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InspectorComponent)
};
