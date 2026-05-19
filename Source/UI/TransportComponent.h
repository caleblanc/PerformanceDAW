#pragma once

#include <JuceHeader.h>
#include "../Engine/PlaybackEngine.h"

// Header bar: Play / Stop / Loop buttons + Big Clock (BBT and MM:SS).
// Repaints at 30 Hz while playing via a timer.
class TransportComponent : public juce::Component,
                           public juce::Timer,
                           public PlaybackEngine::Listener
{
public:
    explicit TransportComponent (PlaybackEngine& engine);
    ~TransportComponent() override;

    void paint  (juce::Graphics& g) override;
    void resized() override;

private:
    PlaybackEngine& engine;

    juce::TextButton   playBtn     { "PLAY" };
    juce::TextButton   stopBtn     { "STOP" };
    juce::ToggleButton loopBtn     { "LOOP" };
    juce::ToggleButton advanceBtn  { "AUTO" };

    // Editable BPM label — double-click to type a new tempo.
    juce::Label bpmLabel;

    void timerCallback() override;
    void transportStateChanged() override;
    void activeSongChanged (int) override { updateBpmLabel(); repaint(); }

    void updateBpmLabel();
    void drawBigClock (juce::Graphics& g, juce::Rectangle<int> area);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TransportComponent)
};
