#pragma once

#include <JuceHeader.h>
#include "Engine/PlaybackEngine.h"
#include "UI/TransportComponent.h"
#include "UI/SetlistComponent.h"
#include "UI/TimelineComponent.h"
#include "UI/InspectorComponent.h"

// Root layout + global key handler (Studio One shortcut map).
//
//  ┌──────────────────────────────────────────────────────────────────┐
//  │  TransportComponent  (header, 64 px)                             │
//  ├─────────────┬──────────────────────────────────┬─────────────────┤
//  │ SetlistComp │  TimelineComponent                │ InspectorComp  │
//  │  (220 px)   │  (fills remainder)                │  (240 px)      │
//  └─────────────┴──────────────────────────────────┴─────────────────┘
//
// Keyboard shortcuts (Studio One layout):
//   Space           Play / Stop toggle
//   Return          Stop + return to 0
//   Numpad 0        Stop + return to 0
//   L               Loop on/off
//   Left / Right    Move playhead ±1 bar
//   ,  .            Move playhead ±1 bar (numpad-style)
//   [  ]            Set loop-in / loop-out at current position
//   Cmd+Z           Undo
//   Cmd+Shift+Z     Redo
//   Cmd++ / Cmd+=   Zoom in  (timeline)
//   Cmd+-           Zoom out (timeline)
class MainComponent : public juce::Component,
                       public juce::KeyListener
{
public:
    explicit MainComponent (tracktion_engine::Engine& engineRef);
    ~MainComponent() override;

    void paint   (juce::Graphics& g) override;
    void resized() override;

    // KeyListener — registered globally so focus position doesn't matter.
    bool keyPressed (const juce::KeyPress& key, juce::Component* origin) override;

private:
    PlaybackEngine playbackEngine;

    TransportComponent transport  { playbackEngine };
    SetlistComponent   setlist    { playbackEngine };
    TimelineComponent  timeline   { playbackEngine };
    InspectorComponent inspector  { playbackEngine };

    static constexpr int headerH    = 64;
    static constexpr int sidebarW   = 220;
    static constexpr int inspectorW = 240;

    // Move the playhead by a number of bars (uses current tempo).
    void nudgeByBars (int bars);

    // Tap tempo state.
    juce::int64 lastTapMs      = 0;
    double      tapAccumMs     = 0.0;
    int         tapCount       = 0;
    void        handleTapTempo();

    // Kept alive across async file dialog.
    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
