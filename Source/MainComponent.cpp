#include "MainComponent.h"

namespace te = tracktion_engine;
using tracktion::TimePosition;

MainComponent::MainComponent (te::Engine& /*engineRef*/)
{
    playbackEngine.initialiseAudio (0, 32);

    addAndMakeVisible (transport);
    addAndMakeVisible (setlist);
    addAndMakeVisible (timeline);
    addAndMakeVisible (inspector);

    addChildComponent (showPage);   // hidden until F2

    setSize (1440, 900);

    // Key listener is registered on the DocumentWindow in Main.cpp so
    // shortcuts fire regardless of which child has focus.

    setlist.addSong ("Song 1");
}

MainComponent::~MainComponent() = default;

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff111111));
}

void MainComponent::resized()
{
    // Show Page covers the full window when visible.
    showPage.setBounds (getLocalBounds());

    auto area = getLocalBounds();
    transport.setBounds (area.removeFromTop  (headerH));
    setlist.setBounds   (area.removeFromLeft (sidebarW));
    inspector.setBounds (area.removeFromRight (inspectorW));
    timeline.setBounds  (area);
}

void MainComponent::toggleShowPage()
{
    showPageVisible = ! showPageVisible;
    showPage.setVisible (showPageVisible);

    // When Show Page is up, hide the normal edit UI so they don't paint behind it.
    transport.setVisible (! showPageVisible);
    setlist  .setVisible (! showPageVisible);
    timeline .setVisible (! showPageVisible);
    inspector.setVisible (! showPageVisible);
}

// ── Keyboard shortcuts (Studio One layout) ────────────────────────────────────

bool MainComponent::keyPressed (const juce::KeyPress& k, juce::Component*)
{
    // Don't steal keys while a text editor is focused — let the user type.
    if (auto* focused = juce::Component::getCurrentlyFocusedComponent())
        if (dynamic_cast<juce::TextEditor*> (focused))
            return false;

    const auto cmd   = k.getModifiers().isCommandDown();
    const auto shift = k.getModifiers().isShiftDown();
    const int  code  = k.getKeyCode();

    // ── Show Page toggle (F2 / Escape) ────────────────────────────────────
    if (code == juce::KeyPress::F2Key)
    {
        toggleShowPage();
        return true;
    }

    if (code == juce::KeyPress::escapeKey && showPageVisible)
    {
        toggleShowPage();
        return true;
    }

    // ── Transport ──────────────────────────────────────────────────────────
    if (code == juce::KeyPress::spaceKey)
    {
        if (shift)
        {
            // Shift+Space: play from start, or if already playing return to 0 and keep playing.
            playbackEngine.stop();
            playbackEngine.setPositionSeconds (0.0);
            playbackEngine.play();
        }
        else
        {
            playbackEngine.isPlaying() ? playbackEngine.stop() : playbackEngine.play();
        }
        return true;
    }

    if (code == juce::KeyPress::returnKey || code == juce::KeyPress::numberPad0)
    {
        playbackEngine.stop();
        playbackEngine.setPositionSeconds (0.0);
        return true;
    }

    if (code == 'L' && ! cmd)
    {
        playbackEngine.setLooping (! playbackEngine.isLooping());
        return true;
    }

    if (code == 'T' && ! cmd)
    {
        handleTapTempo();
        return true;
    }

    if (code == 'M' && ! cmd)
    {
        const double pos = playbackEngine.getCurrentPositionSeconds();
        playbackEngine.addMarker ("Marker " + juce::String (playbackEngine.getMarkers().size() + 1), pos);
        return true;
    }

    // ── Setlist navigation (Page Up / Page Down) ──────────────────────────
    if (code == juce::KeyPress::pageUpKey)
    {
        playbackEngine.switchToSong (playbackEngine.getCurrentSongIndex() - 1);
        return true;
    }
    if (code == juce::KeyPress::pageDownKey)
    {
        playbackEngine.switchToSong (playbackEngine.getCurrentSongIndex() + 1);
        return true;
    }

    // ── Playhead nudge (Left / Right arrow  or  , / .) ────────────────────
    if (code == juce::KeyPress::leftKey  || code == ',')  { nudgeByBars (-1); return true; }
    if (code == juce::KeyPress::rightKey || code == '.')  { nudgeByBars (+1); return true; }

    // ── Loop points ────────────────────────────────────────────────────────
    if (code == '[')
    {
        if (auto* edit = playbackEngine.getCurrentEdit())
            edit->getTransport().setLoopIn (
                TimePosition::fromSeconds (playbackEngine.getCurrentPositionSeconds()));
        return true;
    }

    if (code == ']')
    {
        if (auto* edit = playbackEngine.getCurrentEdit())
            edit->getTransport().setLoopOut (
                TimePosition::fromSeconds (playbackEngine.getCurrentPositionSeconds()));
        return true;
    }

    // ── Undo / Redo ────────────────────────────────────────────────────────
    if (cmd && ! shift && code == 'Z')
    {
        if (auto* edit = playbackEngine.getCurrentEdit())
            edit->getUndoManager().undo();
        return true;
    }

    if (cmd && shift && code == 'Z')
    {
        if (auto* edit = playbackEngine.getCurrentEdit())
            edit->getUndoManager().redo();
        return true;
    }

    // ── Timeline zoom  (Cmd + +/-)  ───────────────────────────────────────
    if (cmd && (code == '=' || code == '+'))
    {
        timeline.setPixelsPerSecond (timeline.getPixelsPerSecond() * 1.3);
        return true;
    }

    if (cmd && code == '-')
    {
        timeline.setPixelsPerSecond (timeline.getPixelsPerSecond() / 1.3);
        return true;
    }

    // ── Delete selected clip ──────────────────────────────────────────────
    if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey)
    {
        playbackEngine.deleteSelectedClip();
        return true;
    }

    // ── Zoom to fit (Cmd+F) ───────────────────────────────────────────────
    if (cmd && ! shift && code == 'F')
    {
        // Find the extent of all clips in the current edit.
        double maxEnd = 0.0;
        if (auto* edit = playbackEngine.getCurrentEdit())
        {
            edit->getTrackList().visitAllTopLevel ([&] (te::Track& t) -> bool
            {
                if (! t.isAudioTrack()) return true;
                auto* ct = dynamic_cast<te::ClipTrack*> (&t);
                if (! ct) return true;
                const int n = ct->getNumTrackItems();
                for (int i = 0; i < n; ++i)
                {
                    auto* c = dynamic_cast<te::Clip*> (ct->getTrackItem (i));
                    if (c) maxEnd = std::max (maxEnd, c->getPosition().getEnd().inSeconds());
                }
                return true;
            });
        }

        if (maxEnd > 0.0)
        {
            const double availW = static_cast<double> (timeline.getWidth() - 160);   // trackHeaderW
            timeline.setPixelsPerSecond (availW / (maxEnd * 1.1));
        }
        return true;
    }

    // ── Open song (Cmd+O) ─────────────────────────────────────────────────
    if (cmd && ! shift && code == 'O')
    {
        fileChooser = std::make_unique<juce::FileChooser> (
            "Open Song",
            juce::File::getSpecialLocation (juce::File::userDocumentsDirectory),
            "*.edit");

        fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                                      | juce::FileBrowserComponent::canSelectFiles,
            [this] (const juce::FileChooser& fc)
            {
                const auto result = fc.getResult();
                if (result != juce::File{})
                    playbackEngine.loadSongFromFile (result);
            });
        return true;
    }

    // ── Save (Cmd+S) ──────────────────────────────────────────────────────
    if (cmd && ! shift && code == 'S')
    {
        fileChooser = std::make_unique<juce::FileChooser> (
            "Save Song",
            juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                .getChildFile (playbackEngine.getSongName (playbackEngine.getCurrentSongIndex())
                               + ".edit"),
            "*.edit");

        fileChooser->launchAsync (juce::FileBrowserComponent::saveMode
                                      | juce::FileBrowserComponent::canSelectFiles,
            [this] (const juce::FileChooser& fc)
            {
                const auto result = fc.getResult();
                if (result != juce::File{})
                    playbackEngine.saveCurrentSong (result);
            });
        return true;
    }

    // ── Keyboard shortcut reference (Cmd+/) ──────────────────────────────
    if (cmd && code == '/')
    {
        const juce::String ref =
            "TRANSPORT\n"
            "  Space          Play / Stop\n"
            "  Shift+Space    Play from start\n"
            "  Return         Stop + go to 0\n"
            "  L              Loop on/off\n"
            "  T              Tap tempo\n"
            "\n"
            "NAVIGATION\n"
            "  Left / ,       Previous bar\n"
            "  Right / .      Next bar\n"
            "  [  ]           Set loop in / out\n"
            "  Cmd+F          Zoom to fit all clips\n"
            "  Cmd++          Zoom in\n"
            "  Cmd+-          Zoom out\n"
            "\n"
            "EDITING\n"
            "  Delete         Delete selected clip\n"
            "  Cmd+Z          Undo\n"
            "  Cmd+Shift+Z    Redo\n"
            "  Cmd+S          Save song\n"
            "  Cmd+O          Open song\n"
            "\n"
            "CLIP\n"
            "  Drag           Move (snaps to bar)\n"
            "  Shift+Drag     Move freely (no snap)\n"
            "  Right-click    Rename / Duplicate / Delete\n"
            "  Double-click   Rename inline\n"
            "\n"
            "SETLIST\n"
            "  Click          Switch to song\n"
            "  Double-click   Rename song\n"
            "\n"
            "SHOW PAGE\n"
            "  F2 / Escape    Toggle full-screen performance view";

        juce::AlertWindow::showMessageBoxAsync (
            juce::MessageBoxIconType::InfoIcon,
            "Keyboard Shortcuts  (Cmd+/)", ref);
        return true;
    }

    return false;
}

// ── Nudge playhead by whole bars ─────────────────────────────────────────────

void MainComponent::handleTapTempo()
{
    const juce::int64 nowMs = juce::Time::currentTimeMillis();
    const juce::int64 gap   = nowMs - lastTapMs;

    if (gap > 3000)   // gap too large — start fresh
    {
        tapAccumMs = 0.0;
        tapCount   = 0;
    }
    else if (gap > 100)   // ignore accidental double-fire
    {
        tapAccumMs += static_cast<double> (gap);
        ++tapCount;

        if (tapCount >= 2)
        {
            const double avgGapSec = (tapAccumMs / tapCount) / 1000.0;
            const double newBpm    = juce::jlimit (20.0, 300.0, 60.0 / avgGapSec);

            if (auto* edit = playbackEngine.getCurrentEdit())
            {
                auto& tempos = edit->tempoSequence;
                if (tempos.getTempos().size() > 0)
                    tempos.getTempos()[0]->setBpm (newBpm);
            }
        }
    }

    lastTapMs = nowMs;
}

void MainComponent::nudgeByBars (int bars)
{
    auto* edit = playbackEngine.getCurrentEdit();
    if (! edit) return;

    const auto curPos = TimePosition::fromSeconds (playbackEngine.getCurrentPositionSeconds());
    auto& tempos      = edit->tempoSequence;

    // Find current bar, offset by `bars`, convert back to seconds.
    auto bb   = tempos.toBarsAndBeats (curPos);
    bb.bars  += bars;
    bb.bars   = std::max (0, bb.bars);
    bb.beats  = tracktion::BeatDuration::fromBeats (0.0);   // snap to bar start

    const auto newPos = tempos.toTime (bb);
    playbackEngine.setPositionSeconds (juce::jmax (0.0, newPos.inSeconds()));
}
