#pragma once

#include <JuceHeader.h>
#include "../Engine/PlaybackEngine.h"

namespace te = tracktion_engine;

// Full-screen live-performance view.
//
//   Top half   — current song name, position clock, Play/Stop
//   Mid strip  — progress bar + "NEXT:" cue
//   Bottom     — scrollable setlist (click to switch)
//
// Toggle with F2.  Press Escape or F2 again to return to edit view.
class ShowPageComponent : public juce::Component,
                          public juce::Timer,
                          public PlaybackEngine::Listener
{
public:
    explicit ShowPageComponent (PlaybackEngine& engine);
    ~ShowPageComponent() override;

    void paint   (juce::Graphics& g) override;
    void resized () override;
    void mouseDown (const juce::MouseEvent& e) override;

private:
    PlaybackEngine& engine;

    juce::TextButton playBtn { "▶  PLAY" };
    juce::TextButton stopBtn { "■  STOP" };

    static constexpr int rowH = 56;

    // ── Painting helpers ──────────────────────────────────────────────────────
    void paintNowPlaying (juce::Graphics& g, juce::Rectangle<int> area);
    void paintProgressBar (juce::Graphics& g, juce::Rectangle<int> area);
    void paintSetlist     (juce::Graphics& g, juce::Rectangle<int> area);
    void paintMarkerCues  (juce::Graphics& g, juce::Rectangle<int> area);

    juce::String formatTime (double sec) const;

    // ── Timer / Listener ─────────────────────────────────────────────────────
    void timerCallback     ()    override { repaint(); }
    void activeSongChanged (int) override { repaint(); }
    void trackListChanged  ()    override { repaint(); }
    void markersChanged    ()    override { repaint(); }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ShowPageComponent)
};
