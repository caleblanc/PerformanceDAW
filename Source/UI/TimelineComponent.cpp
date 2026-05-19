#include "TimelineComponent.h"

namespace te = tracktion_engine;
using tracktion::TimePosition;
using tracktion::TimeDuration;

// ── Track colour palette ──────────────────────────────────────────────────────

static juce::Colour trackColour (int trackIndex)
{
    static constexpr juce::uint32 palette[] = {
        0xff004d50,   // teal
        0xff4a0072,   // purple
        0xff1a3a00,   // forest green
        0xff3e1a00,   // burnt orange
        0xff002060,   // navy
        0xff5c2200,   // brown
        0xff004020,   // dark green
        0xff3a0040,   // dark violet
    };
    return juce::Colour (palette[static_cast<size_t> (trackIndex) % std::size (palette)]);
}

static juce::Colour trackColourBright (int trackIndex)
{
    return trackColour (trackIndex).brighter (0.5f);
}

TimelineComponent::TimelineComponent (PlaybackEngine& e) : engine (e)
{
    engine.addListener (this);

    // Borrow the format manager from the TE engine so thumbnails share the
    // same codec registrations and don't duplicate format objects.
    formatManager = &engine.getEngine().getAudioFileFormatManager().readFormatManager;

    startTimerHz (30);
}

TimelineComponent::~TimelineComponent()
{
    engine.removeListener (this);
    stopTimer();

    // Detach all change listeners before the thumbnails are destroyed.
    for (auto& [path, thumb] : thumbnails)
        thumb->removeChangeListener (this);
}

// ── Thumbnail management ──────────────────────────────────────────────────────

juce::AudioThumbnail* TimelineComponent::getThumbnail (const juce::File& file)
{
    if (! file.existsAsFile() || formatManager == nullptr)
        return nullptr;

    const juce::String key = file.getFullPathName();
    auto it = thumbnails.find (key);

    if (it != thumbnails.end())
        return it->second.get();

    // First time we've seen this file — create and start async loading.
    auto thumb = std::make_unique<juce::AudioThumbnail> (256, *formatManager, thumbnailCache);
    thumb->addChangeListener (this);   // repaint() when loading completes
    thumb->setSource (new juce::FileInputSource (file));

    auto* ptr = thumb.get();
    thumbnails.emplace (key, std::move (thumb));
    return ptr;
}

// ── Coordinate helpers ────────────────────────────────────────────────────────

float TimelineComponent::secondsToX (double t) const
{
    return static_cast<float> ((t - viewStartSeconds) * pixelsPerSecond)
           + static_cast<float> (trackHeaderW);
}

double TimelineComponent::xToSeconds (float x) const
{
    return (static_cast<double> (x) - trackHeaderW) / pixelsPerSecond + viewStartSeconds;
}

void TimelineComponent::setPixelsPerSecond (double pps)
{
    pixelsPerSecond = juce::jmax (10.0, juce::jmin (pps, 2000.0));
    repaint();
}

int TimelineComponent::getTrackY (int idx) const
{
    int y = rulerH;
    for (int i = 0; i < idx && i < (int) trackHeights.size(); ++i)
        y += trackHeights[(size_t) i];
    return y;
}

int TimelineComponent::getTrackH (int idx) const
{
    if (idx < 0 || idx >= (int) trackHeights.size()) return defaultTrackH;
    return trackHeights[(size_t) idx];
}

int TimelineComponent::trackIndexAtY (int y) const
{
    int trackY = rulerH;
    for (int i = 0; i < (int) trackHeights.size(); ++i)
    {
        if (y < trackY + trackHeights[(size_t) i]) return i;
        trackY += trackHeights[(size_t) i];
    }
    return static_cast<int> (trackHeights.size());  // below all tracks
}

te::ClipTrack* TimelineComponent::clipTrackAtY (int y) const
{
    if (! engine.getCurrentEdit()) return nullptr;

    const int idx = trackIndexAtY (y);
    if (idx < 0) return nullptr;

    int count = 0;
    te::ClipTrack* result = nullptr;

    engine.getCurrentEdit()->getTrackList().visitAllTopLevel ([&] (te::Track& t) -> bool
    {
        if (! t.isAudioTrack()) return true;
        if (count == idx) { result = dynamic_cast<te::ClipTrack*> (&t); return false; }
        ++count;
        return true;
    });

    return result;
}

// ── Main paint ────────────────────────────────────────────────────────────────

void TimelineComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff181818));
    paintRuler (g, getLocalBounds().removeFromTop (rulerH));

    te::Edit* edit = engine.getCurrentEdit();
    if (edit == nullptr)
    {
        g.setColour (juce::Colour (0xff444444));
        g.setFont (16.0f);
        g.drawText ("Add a song in the Setlist to get started.",
                    getLocalBounds(), juce::Justification::centred);
        paintPlayhead (g);
        return;
    }

    int audioTrackCount = 0;

    edit->getTrackList().visitAllTopLevel ([&] (te::Track& track) -> bool
    {
        if (! track.isAudioTrack()) return true;

        const int tIdx   = audioTrackCount;
        const int tY     = getTrackY (tIdx);
        const int tH     = getTrackH (tIdx);
        ++audioTrackCount;

        auto* clipTrack  = dynamic_cast<te::ClipTrack*> (&track);
        const bool isSel = (&track == engine.getSelectedTrack());

        const auto headerBounds = juce::Rectangle<int> (0, tY, trackHeaderW, tH);
        const auto clipArea     = juce::Rectangle<int> (trackHeaderW, tY,
                                                         getWidth() - trackHeaderW, tH);

        paintTrackHeader (g, &track, headerBounds, isSel, tIdx);

        g.setColour (juce::Colour (isSel ? 0xff1e1e2e
                                         : (audioTrackCount % 2 == 0 ? 0xff1a1a1a : 0xff1e1e1e)));
        g.fillRect (clipArea);

        paintGrid (g, clipArea);

        if (clipTrack != nullptr)
        {
            const int numClips = clipTrack->getNumTrackItems();

            if (numClips == 0)
            {
                g.setColour (juce::Colour (0xff3a3a3a));
                g.setFont (12.0f);
                g.drawText ("Drop audio files here, or use Import in the Inspector",
                            clipArea.reduced (12, 0), juce::Justification::centredLeft);
            }

            for (int i = 0; i < numClips; ++i)
            {
                auto* clip = dynamic_cast<te::Clip*> (clipTrack->getTrackItem (i));
                if (! clip) continue;

                const double startSec = clip->getPosition().getStart().inSeconds();
                const double endSec   = clip->getPosition().getEnd().inSeconds();
                const float  clipX    = secondsToX (startSec);
                const float  clipW    = static_cast<float> ((endSec - startSec) * pixelsPerSecond);

                if (clipX + clipW < trackHeaderW || clipX > getWidth()) continue;

                const auto clipRect = juce::Rectangle<float> (clipX, (float) tY + 2.f,
                                                               clipW, (float) tH - 4.f)
                                          .toNearestInt();

                if (dynamic_cast<te::MidiClip*> (clip))
                    paintMidiClip  (g, clip, clipRect, tIdx);
                else
                    paintAudioClip (g, clip, clipRect, tIdx);
            }
        }

        // Track resize handle (bottom 4px of header, full-width line)
        g.setColour (isSel ? juce::Colour (0xff3a3a6a) : juce::Colour (0xff303030));
        g.drawHorizontalLine (tY + tH - 1, 0.f, (float) getWidth());
        g.setColour (juce::Colour (0xff404040));
        g.fillRect (0, tY + tH - 3, trackHeaderW, 3);

        return true;
    });

    // Loop region shade behind all tracks
    if (edit != nullptr)
    {
        auto& transport = edit->getTransport();
        if (transport.looping)
        {
            const double loopIn  = transport.getLoopRange().getStart().inSeconds();
            const double loopOut = transport.getLoopRange().getEnd().inSeconds();
            if (loopOut > loopIn)
            {
                const float lx = secondsToX (loopIn);
                const float rx = secondsToX (loopOut);
                g.setColour (juce::Colour (0x181565c0));
                g.fillRect  (juce::Rectangle<float> (lx, (float) rulerH,
                                                      rx - lx, (float) (getHeight() - rulerH)));
            }
        }
    }

    if (isDragOver) paintDropTarget (g);
    paintPlayhead (g);
}

// ── Ruler ─────────────────────────────────────────────────────────────────────

void TimelineComponent::paintRuler (juce::Graphics& g, juce::Rectangle<int> area)
{
    g.setColour (juce::Colour (0xff252525));
    g.fillRect (area);

    auto* edit = engine.getCurrentEdit();

    if (edit == nullptr)
    {
        // No edit: fall back to plain seconds.
        const double visibleSec = static_cast<double> (getWidth() - trackHeaderW) / pixelsPerSecond;
        const double endS = viewStartSeconds + visibleSec + 1.0;
        g.setFont (10.f);
        for (double t = std::floor (viewStartSeconds); t <= endS; t += 1.0)
        {
            const float x     = secondsToX (t);
            const bool  major = (static_cast<int> (t) % 5 == 0);
            const float tickH = major ? area.getHeight() * 0.6f : area.getHeight() * 0.3f;
            g.setColour (major ? juce::Colour (0xff888888) : juce::Colour (0xff444444));
            g.drawVerticalLine ((int) x, (float) area.getBottom() - tickH, (float) area.getBottom());
            if (major)
            {
                const int s = (int) t;
                g.setColour (juce::Colour (0xff888888));
                g.drawText (juce::String (s / 60) + ":" + juce::String (s % 60).paddedLeft ('0', 2),
                            (int) x + 2, area.getY(), 50, area.getHeight(),
                            juce::Justification::centredLeft);
            }
        }
    }
    else
    {
        // Bar/beat ruler.
        auto& tempos = edit->tempoSequence;
        const double visibleSec = static_cast<double> (getWidth() - trackHeaderW) / pixelsPerSecond;
        const double endSec     = viewStartSeconds + visibleSec + 4.0;   // a little past the edge

        // Determine how many beats we can fit before labels collide (~40 px per label min).
        const double beatWidth = tempos.getTempos().size() > 0
            ? (60.0 / tempos.getTempos()[0]->getBpm()) * pixelsPerSecond
            : pixelsPerSecond * 0.5;

        const int beatsPerBar = 4;   // assume 4/4 — could read from time sig later
        const double barWidth = beatWidth * beatsPerBar;

        // Decide granularity: if a bar is wider than 20 px show beats too.
        const bool showBeats = (barWidth > 24.0);

        // Walk bar by bar from the visible start.
        auto startPos = tracktion::TimePosition::fromSeconds (juce::jmax (0.0, viewStartSeconds - 1.0));
        auto startBB  = tempos.toBarsAndBeats (startPos);
        int  bar      = juce::jmax (0, startBB.bars);

        g.setFont (10.f);

        for (;;)
        {
            tracktion::tempo::BarsAndBeats bb {};
            bb.bars  = bar;
            bb.beats = tracktion::BeatDuration::fromBeats (0.0);
            const double barSec = tempos.toTime (bb).inSeconds();
            if (barSec > endSec) break;

            const float barX = secondsToX (barSec);

            // Bar line + label
            g.setColour (juce::Colour (0xff666666));
            g.drawVerticalLine ((int) barX, (float) area.getY(), (float) area.getBottom());
            g.setColour (juce::Colour (0xff888888));
            g.drawText (juce::String (bar + 1),
                        (int) barX + 3, area.getY(), 40, area.getHeight(),
                        juce::Justification::centredLeft);

            // Beat lines within the bar
            if (showBeats)
            {
                for (int beat = 1; beat < beatsPerBar; ++beat)
                {
                    tracktion::tempo::BarsAndBeats bbb {};
                    bbb.bars  = bar;
                    bbb.beats = tracktion::BeatDuration::fromBeats ((double) beat);
                    const float beatX = secondsToX (tempos.toTime (bbb).inSeconds());
                    g.setColour (juce::Colour (0xff3a3a3a));
                    g.drawVerticalLine ((int) beatX,
                                       (float) area.getBottom() - area.getHeight() * 0.35f,
                                       (float) area.getBottom());
                }
            }

            ++bar;
        }
    }

    // Draw loop region highlight on the ruler.
    if (edit != nullptr)
    {
        auto& transport = edit->getTransport();
        if (transport.looping)
        {
            const double loopIn  = transport.getLoopRange().getStart().inSeconds();
            const double loopOut = transport.getLoopRange().getEnd().inSeconds();
            if (loopOut > loopIn)
            {
                const float lx = secondsToX (loopIn);
                const float rx = secondsToX (loopOut);
                g.setColour (juce::Colour (0x441565c0));
                g.fillRect  (juce::Rectangle<float> (lx, (float) area.getY(),
                                                      rx - lx, (float) area.getHeight()));
                g.setColour (juce::Colour (0xff1565c0));
                g.drawVerticalLine ((int) lx, (float) area.getY(), (float) area.getBottom());
                g.drawVerticalLine ((int) rx, (float) area.getY(), (float) area.getBottom());
            }
        }
    }

    paintMarkers (g, area);

    g.setColour (juce::Colour (0xff202020));
    g.fillRect  (0, area.getY(), trackHeaderW, area.getHeight());
    g.setColour (juce::Colour (0xff303030));
    g.drawVerticalLine (trackHeaderW, (float) area.getY(), (float) area.getBottom());
}

// ── Markers ───────────────────────────────────────────────────────────────────

void TimelineComponent::paintMarkers (juce::Graphics& g, juce::Rectangle<int> rulerArea)
{
    const auto& markers = engine.getMarkers();
    if (markers.empty()) return;

    for (int i = 0; i < (int) markers.size(); ++i)
    {
        const float mx = secondsToX (markers[(size_t) i].positionSeconds);
        if (mx < (float) trackHeaderW || mx > (float) getWidth()) continue;

        // Marker line extends from ruler through all tracks.
        g.setColour (juce::Colour (0xffffd54f).withAlpha (0.9f));
        g.drawVerticalLine ((int) mx, (float) rulerArea.getY(), (float) getHeight());

        // Triangle flag at top.
        const float flagTop = (float) rulerArea.getY();
        juce::Path flag;
        flag.addTriangle (mx, flagTop, mx + 8.f, flagTop, mx, flagTop + 8.f);
        g.fillPath (flag);

        // Label inside the flag area.
        g.setColour (juce::Colours::black);
        g.setFont (9.f);
        g.drawText (markers[(size_t) i].name,
                    (int) mx + 2, (int) flagTop, 80, 10,
                    juce::Justification::centredLeft);
    }
}

// ── Track header ──────────────────────────────────────────────────────────────

void TimelineComponent::paintTrackHeader (juce::Graphics& g, te::Track* track,
                                           juce::Rectangle<int> b, bool isSel, int trackIdx)
{
    g.setColour (isSel ? juce::Colour (0xff2a2a44) : juce::Colour (0xff222222));
    g.fillRect (b);

    const auto stripCol = isSel ? juce::Colour (0xff5c6bc0) : trackColourBright (trackIdx);
    g.setColour (stripCol);
    g.fillRect (b.getX(), b.getY(), 4, b.getHeight());

    g.setColour (juce::Colour (0xff303030));
    g.drawVerticalLine (b.getRight() - 1, (float) b.getY(), (float) b.getBottom());

    g.setColour (juce::Colours::white);
    g.setFont (13.f);
    g.drawText (track->getName(), b.withTrimmedLeft (10).withTrimmedRight (4),
                juce::Justification::centredLeft);

    // Mute state is reflected by the real TextButton child component — no paint needed here.
}

// ── Audio clip ────────────────────────────────────────────────────────────────

void TimelineComponent::paintAudioClip (juce::Graphics& g, te::Clip* clip, juce::Rectangle<int> b, int trackIdx)
{
    const bool isSel = (clip == engine.getSelectedClip());
    const auto fill  = trackColour (trackIdx);
    g.setColour (isSel ? fill.brighter (0.25f) : fill);
    g.fillRoundedRectangle (b.toFloat(), 3.f);

    if (auto* waveClip = dynamic_cast<te::WaveAudioClip*> (clip))
    {
        const juce::File srcFile = waveClip->getAudioFile().getFile();
        auto* thumb = getThumbnail (srcFile);

        if (thumb != nullptr && thumb->getTotalLength() > 0.0)
        {
            const double startSec = clip->getPosition().getStart().inSeconds();
            const double endSec   = clip->getPosition().getEnd().inSeconds();

            g.setColour (trackColourBright (trackIdx).withAlpha (0.85f));
            thumb->drawChannels (g, b.reduced (2, 8), startSec, endSec, 1.0f);
        }
        else
        {
            const int midY = b.getCentreY();
            g.setColour (trackColourBright (trackIdx).withAlpha (0.5f));
            for (int x = b.getX() + 2; x < b.getRight() - 2; x += 3)
            {
                const int h = 4 + ((x * 7 + (int) (juce::Time::getMillisecondCounter() / 200)) % 10);
                g.drawVerticalLine (x, (float) (midY - h), (float) (midY + h));
            }
        }
    }

    g.setColour (isSel ? juce::Colours::yellow : trackColourBright (trackIdx));
    g.drawRoundedRectangle (b.toFloat().reduced (0.5f), 3.f, isSel ? 2.f : 1.f);

    // Gain handle strip at top of clip.
    if (auto* acb = dynamic_cast<te::AudioClipBase*> (clip))
    {
        const float db       = acb->getGainDB();
        const float normGain = juce::jlimit (0.f, 1.f, (db + 60.f) / 72.f);  // -60..+12 dB
        const int   barW     = static_cast<int> (normGain * (float) b.getWidth());
        const auto  gainBar  = juce::Rectangle<int> (b.getX(), b.getY(), barW, 6);
        g.setColour (juce::Colour (0x88ffffff));
        g.fillRect  (gainBar);
        if (std::abs (db) > 0.5f)
        {
            g.setColour (juce::Colours::white.withAlpha (0.7f));
            g.setFont (9.f);
            g.drawText (juce::String (db, 1) + " dB",
                        b.getX() + 2, b.getY(), 48, 8, juce::Justification::centredLeft);
        }
    }

    g.setColour (juce::Colours::white);
    g.setFont (11.f);
    g.drawText (clip->getName(), b.reduced (6, 2), juce::Justification::bottomLeft);
}

// ── MIDI clip ─────────────────────────────────────────────────────────────────

void TimelineComponent::paintMidiClip (juce::Graphics& g, te::Clip* clip, juce::Rectangle<int> b, int trackIdx)
{
    const bool isSel = (clip == engine.getSelectedClip());
    g.setColour (trackColour (trackIdx));
    g.fillRoundedRectangle (b.toFloat(), 3.f);
    g.setColour (isSel ? juce::Colours::yellow : trackColourBright (trackIdx));
    g.drawRoundedRectangle (b.toFloat().reduced (0.5f), 3.f, isSel ? 2.f : 1.f);

    if (auto* mc = dynamic_cast<te::MidiClip*> (clip))
    {
        const double lenSec = mc->getPosition().getLength().inSeconds();
        if (lenSec > 0.0)
        {
            g.setColour (juce::Colour (0xffce93d8).withAlpha (0.85f));
            for (auto* note : mc->getSequence().getNotes())
            {
                const double noteSec = note->getStartBeat().inBeats() / 2.0;   // ~120 BPM
                const double lenBeat = note->getLengthBeats().inBeats() / 2.0;
                const float nx = (float) b.getX() + (float) (noteSec / lenSec * b.getWidth());
                const float nw = juce::jmax (2.f, (float) (lenBeat / lenSec * b.getWidth()));
                const float ny = (float) b.getY() + (1.f - note->getNoteNumber() / 127.f) * b.getHeight();
                g.fillRect (nx, ny, nw, 2.f);
            }
        }
    }

    g.setColour (juce::Colours::white);
    g.setFont (11.f);
    g.drawText (clip->getName(), b.reduced (6, 2), juce::Justification::bottomLeft);
}

// ── Playhead ──────────────────────────────────────────────────────────────────

void TimelineComponent::paintPlayhead (juce::Graphics& g)
{
    const float x = secondsToX (engine.getCurrentPositionSeconds());
    if (x < trackHeaderW || x > getWidth()) return;

    g.setColour (juce::Colours::red.withAlpha (0.9f));
    g.drawVerticalLine ((int) x, (float) rulerH, (float) getHeight());

    juce::Path head;
    head.addTriangle (x - 6.f, 0.f, x + 6.f, 0.f, x, (float) rulerH);
    g.setColour (juce::Colours::red);
    g.fillPath (head);
}

void TimelineComponent::paintDropTarget (juce::Graphics& g)
{
    const float x = secondsToX (dragOverSeconds);
    if (x >= trackHeaderW)
    {
        g.setColour (juce::Colours::yellow.withAlpha (0.7f));
        g.drawVerticalLine ((int) x, (float) rulerH, (float) getHeight());
    }

    if (dragOverTrackIdx >= 0)
    {
        const int y = getTrackY (dragOverTrackIdx);
        const int h = getTrackH (dragOverTrackIdx);
        g.setColour (juce::Colours::yellow.withAlpha (0.12f));
        g.fillRect (trackHeaderW, y, getWidth() - trackHeaderW, h);
    }
}

// ── Grid / snap ───────────────────────────────────────────────────────────────

double TimelineComponent::snapToBar (double seconds, bool shiftHeld) const
{
    if (shiftHeld) return seconds;   // Shift = free movement

    auto* edit = engine.getCurrentEdit();
    if (! edit) return seconds;

    auto& tempos = edit->tempoSequence;
    const auto pos = TimePosition::fromSeconds (seconds);
    const auto bb  = tempos.toBarsAndBeats (pos);
    const int  bar = bb.bars;

    tracktion::tempo::BarsAndBeats thisBar {};
    thisBar.bars  = bar;
    thisBar.beats = tracktion::BeatDuration::fromBeats (0.0);

    tracktion::tempo::BarsAndBeats nextBar {};
    nextBar.bars  = bar + 1;
    nextBar.beats = tracktion::BeatDuration::fromBeats (0.0);

    const double thisBarSec = tempos.toTime (thisBar).inSeconds();
    const double nextBarSec = tempos.toTime (nextBar).inSeconds();

    double best = (std::abs (seconds - thisBarSec) <= std::abs (seconds - nextBarSec))
                      ? thisBarSec
                      : nextBarSec;

    // Also snap to clip edges within half a beat.
    const double snapRadius = (60.0 / (tempos.getTempos().size() > 0
                                           ? tempos.getTempos()[0]->getBpm()
                                           : 120.0))
                              * 0.5;

    edit->getTrackList().visitAllTopLevel ([&] (te::Track& t) -> bool
    {
        if (! t.isAudioTrack()) return true;
        auto* ct = dynamic_cast<te::ClipTrack*> (&t);
        if (! ct) return true;
        const int n = ct->getNumTrackItems();
        for (int i = 0; i < n; ++i)
        {
            auto* c = dynamic_cast<te::Clip*> (ct->getTrackItem (i));
            if (! c || c == dragClip) continue;
            for (double edge : { c->getPosition().getStart().inSeconds(),
                                 c->getPosition().getEnd().inSeconds() })
            {
                if (std::abs (seconds - edge) < snapRadius
                    && std::abs (seconds - edge) < std::abs (seconds - best))
                    best = edge;
            }
        }
        return true;
    });

    return best;
}

void TimelineComponent::paintGrid (juce::Graphics& g, juce::Rectangle<int> clipArea)
{
    auto* edit = engine.getCurrentEdit();
    if (! edit) return;

    auto& tempos = edit->tempoSequence;
    const int beatsPerBar = 4;
    const double visibleSec = static_cast<double> (getWidth() - trackHeaderW) / pixelsPerSecond;
    const double endSec     = viewStartSeconds + visibleSec + 4.0;

    const double beatWidth = tempos.getTempos().size() > 0
        ? (60.0 / tempos.getTempos()[0]->getBpm()) * pixelsPerSecond
        : pixelsPerSecond * 0.5;
    const double barWidth  = beatWidth * beatsPerBar;
    const bool   showBeats = (barWidth > 40.0);

    auto startPos = TimePosition::fromSeconds (juce::jmax (0.0, viewStartSeconds - 1.0));
    auto startBB  = tempos.toBarsAndBeats (startPos);
    int  bar      = juce::jmax (0, startBB.bars);

    for (;;)
    {
        tracktion::tempo::BarsAndBeats bb {};
        bb.bars  = bar;
        bb.beats = tracktion::BeatDuration::fromBeats (0.0);
        const double barSec = tempos.toTime (bb).inSeconds();
        if (barSec > endSec) break;

        const float barX = secondsToX (barSec);
        if (barX >= (float) trackHeaderW)
        {
            g.setColour (juce::Colour (0xff2a2a2a));
            g.drawVerticalLine ((int) barX, (float) clipArea.getY(), (float) clipArea.getBottom());
        }

        if (showBeats)
        {
            for (int beat = 1; beat < beatsPerBar; ++beat)
            {
                tracktion::tempo::BarsAndBeats bbb {};
                bbb.bars  = bar;
                bbb.beats = tracktion::BeatDuration::fromBeats ((double) beat);
                const float beatX = secondsToX (tempos.toTime (bbb).inSeconds());
                if (beatX >= (float) trackHeaderW)
                {
                    g.setColour (juce::Colour (0xff222222));
                    g.drawVerticalLine ((int) beatX, (float) clipArea.getY(), (float) clipArea.getBottom());
                }
            }
        }
        ++bar;
    }
}

// ── Clip hit-testing ──────────────────────────────────────────────────────────

te::Clip* TimelineComponent::clipAtPoint (int x, int y) const
{
    auto* clipTrack = clipTrackAtY (y);
    if (! clipTrack) return nullptr;

    const int n = clipTrack->getNumTrackItems();
    for (int i = 0; i < n; ++i)
    {
        auto* clip = dynamic_cast<te::Clip*> (clipTrack->getTrackItem (i));
        if (! clip) continue;

        const float startX = secondsToX (clip->getPosition().getStart().inSeconds());
        const float endX   = secondsToX (clip->getPosition().getEnd().inSeconds());

        if ((float) x >= startX && (float) x <= endX)
            return clip;
    }
    return nullptr;
}

// ── Interaction ───────────────────────────────────────────────────────────────

void TimelineComponent::mouseDown (const juce::MouseEvent& e)
{
    // Ruler right-click → marker context menu
    if (e.y < rulerH && e.x > trackHeaderW && e.mods.isRightButtonDown())
    {
        const double clickSec = xToSeconds ((float) e.x);
        const auto& markers   = engine.getMarkers();
        constexpr double hitRadius = 0.5;   // seconds
        int hitIdx = -1;
        for (int i = 0; i < (int) markers.size(); ++i)
            if (std::abs (markers[(size_t) i].positionSeconds - clickSec) < hitRadius)
                { hitIdx = i; break; }

        if (hitIdx >= 0)
        {
            juce::PopupMenu m;
            m.addItem (1, "Rename Marker");
            m.addItem (2, "Delete Marker");
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                [this, hitIdx] (int r)
                {
                    if (r == 2)
                    {
                        engine.removeMarker (hitIdx);
                    }
                    else if (r == 1)
                    {
                        // Rename: cycle through a simple generated name for now.
                        const auto& ms = engine.getMarkers();
                        if (hitIdx < (int) ms.size())
                            engine.renameMarker (hitIdx, ms[(size_t) hitIdx].name + "+");
                    }
                });
        }
        return;
    }

    // Ruler click → set playhead
    if (e.y < rulerH && e.x > trackHeaderW)
    {
        engine.setPositionSeconds (juce::jmax (0.0, xToSeconds ((float) e.x)));
        repaint();
        return;
    }

    // Track header click → select track, deselect clip
    if (e.x < trackHeaderW && e.y >= rulerH)
    {
        // Detect resize handle: bottom 3px of any track header.
        for (int i = 0; i < (int) trackHeights.size(); ++i)
        {
            const int bottom = getTrackY (i) + getTrackH (i);
            if (std::abs (e.y - bottom) <= 4)
            {
                resizingTrackIdx = i;
                resizeDragStartY = e.y;
                resizeDragStartH = getTrackH (i);
                return;
            }
        }

        engine.selectClip (nullptr);
        auto* track = clipTrackAtY (e.y);
        engine.selectTrack (track);

        // Right-click on header → rename
        if (e.mods.isRightButtonDown() && track != nullptr)
        {
            const int tIdx = trackIndexAtY (e.y);
            startTrackRename (track, getTrackY (tIdx));
        }

        repaint();
        return;
    }

    // Clip area click (right-click → context menu)
    if (e.x >= trackHeaderW && e.y >= rulerH && e.mods.isRightButtonDown())
    {
        auto* clip = clipAtPoint (e.x, e.y);
        if (clip != nullptr)
        {
            engine.selectClip (clip);
            // Synthesize a double-click event to show the context menu.
            mouseDoubleClick (e);
        }
        return;
    }

    // Clip area click
    if (e.x >= trackHeaderW && e.y >= rulerH)
    {
        auto* clip = clipAtPoint (e.x, e.y);
        engine.selectClip (clip);

        clipDragMode = DragMode::None;
        dragClip     = nullptr;

        if (clip != nullptr)
        {
            const int    tIdx      = trackIndexAtY (e.y);
            const int    tY        = getTrackY (tIdx);
            const float  clipStartX = secondsToX (clip->getPosition().getStart().inSeconds());
            const float  clipEndX   = secondsToX (clip->getPosition().getEnd().inSeconds());
            constexpr float edgeZone    = 8.f;
            constexpr int   gainZoneH   = 10;   // top pixels of clip = gain drag area

            if (e.y < tY + 2 + gainZoneH
                && dynamic_cast<te::AudioClipBase*> (clip) != nullptr)
            {
                // Gain handle
                clipDragMode     = DragMode::GainHandle;
                gainDragStartDb  = dynamic_cast<te::AudioClipBase*> (clip)->getGainDB();
                gainDragStartY   = e.y;
            }
            else if ((float) e.x - clipStartX < edgeZone)
                clipDragMode = DragMode::ResizeLeft;
            else if (clipEndX - (float) e.x < edgeZone)
                clipDragMode = DragMode::ResizeRight;
            else
            {
                clipDragMode  = DragMode::Move;
                dragOffsetSec = xToSeconds ((float) e.x)
                                - clip->getPosition().getStart().inSeconds();
            }

            dragClip = clip;
        }

        repaint();
    }
}

void TimelineComponent::mouseDrag (const juce::MouseEvent& e)
{
    // Track height resize
    if (resizingTrackIdx >= 0)
    {
        const int newH = juce::jmax (minTrackH, resizeDragStartH + (e.y - resizeDragStartY));
        trackHeights[(size_t) resizingTrackIdx] = newH;
        layoutTrackControls();
        repaint();
        return;
    }

    if (dragClip == nullptr || clipDragMode == DragMode::None) return;

    // Clip gain handle
    if (clipDragMode == DragMode::GainHandle)
    {
        if (auto* acb = dynamic_cast<te::AudioClipBase*> (dragClip))
        {
            const float newDb = juce::jlimit (-60.f, 12.f,
                gainDragStartDb + (float) (gainDragStartY - e.y) * 0.3f);
            acb->setGainDB (newDb);
            repaint();
        }
        return;
    }

    const bool   shiftHeld = e.mods.isShiftDown();
    const double curSec    = xToSeconds ((float) e.x);
    auto         curPos    = dragClip->getPosition();

    if (clipDragMode == DragMode::Move)
    {
        const double rawStart = juce::jmax (0.0, curSec - dragOffsetSec);
        const double newStart = snapToBar (rawStart, shiftHeld);
        const double dur      = curPos.getLength().inSeconds();
        curPos.time = tracktion::TimeRange { TimePosition::fromSeconds (newStart),
                                            TimePosition::fromSeconds (newStart + dur) };
    }
    else if (clipDragMode == DragMode::ResizeRight)
    {
        const double clipStart = curPos.getStart().inSeconds();
        const double rawEnd    = juce::jmax (clipStart + 0.05, curSec);
        const double newEnd    = snapToBar (rawEnd, shiftHeld);
        curPos.time = tracktion::TimeRange { TimePosition::fromSeconds (clipStart),
                                            TimePosition::fromSeconds (juce::jmax (clipStart + 0.05, newEnd)) };
    }
    else if (clipDragMode == DragMode::ResizeLeft)
    {
        const double clipEnd  = curPos.getEnd().inSeconds();
        const double rawStart = juce::jmin (clipEnd - 0.05, juce::jmax (0.0, curSec));
        const double newStart = snapToBar (rawStart, shiftHeld);
        curPos.time = tracktion::TimeRange { TimePosition::fromSeconds (juce::jmin (clipEnd - 0.05, newStart)),
                                            TimePosition::fromSeconds (clipEnd) };
    }

    dragClip->setPosition (curPos);
    repaint();
}

void TimelineComponent::mouseUp (const juce::MouseEvent&)
{
    clipDragMode     = DragMode::None;
    dragClip         = nullptr;
    resizingTrackIdx = -1;
}

void TimelineComponent::startTrackRename (te::Track* track, int ty)
{
    commitTrackRename();

    trackBeingRenamed = track;
    trackRenameEditor = std::make_unique<juce::TextEditor>();
    trackRenameEditor->setText (track->getName(), false);
    trackRenameEditor->setSelectAllWhenFocused (true);
    trackRenameEditor->setFont (juce::Font (13.f));
    trackRenameEditor->setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff2a2a44));
    trackRenameEditor->setColour (juce::TextEditor::textColourId,       juce::Colours::white);
    trackRenameEditor->setColour (juce::TextEditor::outlineColourId,    juce::Colour (0xff5c6bc0));
    trackRenameEditor->setBounds (10, ty + 8, trackHeaderW - 20, 22);
    trackRenameEditor->onReturnKey = [this] { commitTrackRename(); };
    trackRenameEditor->onEscapeKey = [this] { trackRenameEditor.reset(); trackBeingRenamed = nullptr; repaint(); };
    trackRenameEditor->onFocusLost = [this] { commitTrackRename(); };
    addAndMakeVisible (*trackRenameEditor);
    trackRenameEditor->grabKeyboardFocus();
}

void TimelineComponent::commitTrackRename()
{
    if (! trackRenameEditor || ! trackBeingRenamed) return;
    const juce::String newName = trackRenameEditor->getText();
    trackRenameEditor.reset();
    if (newName.isNotEmpty())
        trackBeingRenamed->setName (newName);
    trackBeingRenamed = nullptr;
    repaint();
}

void TimelineComponent::startClipRename (te::Clip* clip)
{
    commitClipRename();   // finish any in-progress rename first

    clipBeingRenamed = clip;

    // Find the clip's pixel rect.
    const float clipX = secondsToX (clip->getPosition().getStart().inSeconds());
    const float clipW = static_cast<float> ((clip->getPosition().getEnd().inSeconds()
                                             - clip->getPosition().getStart().inSeconds())
                                            * pixelsPerSecond);

    // Find which track index this clip is on.
    int foundTrackIdx = 0;
    int tIdx = 0;
    if (auto* edit = engine.getCurrentEdit())
    {
        edit->getTrackList().visitAllTopLevel ([&] (te::Track& t) -> bool
        {
            if (! t.isAudioTrack()) return true;
            auto* ct = dynamic_cast<te::ClipTrack*> (&t);
            if (ct)
            {
                const int n = ct->getNumTrackItems();
                for (int i = 0; i < n; ++i)
                    if (dynamic_cast<te::Clip*> (ct->getTrackItem (i)) == clip)
                    {
                        foundTrackIdx = tIdx;
                        return false;
                    }
            }
            ++tIdx;
            return true;
        });
    }

    const int trackY = getTrackY (foundTrackIdx);
    const int tH     = getTrackH (foundTrackIdx);

    clipRenameEditor = std::make_unique<juce::TextEditor>();
    clipRenameEditor->setText (clip->getName(), false);
    clipRenameEditor->setSelectAllWhenFocused (true);
    clipRenameEditor->setFont (juce::Font (11.f));
    clipRenameEditor->setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff003840));
    clipRenameEditor->setColour (juce::TextEditor::textColourId,       juce::Colours::white);
    clipRenameEditor->setColour (juce::TextEditor::outlineColourId,    juce::Colour (0xff00acc1));

    const int edX = juce::jmax ((int) trackHeaderW, (int) clipX + 2);
    const int edW = juce::jmin ((int) clipW - 4, 180);
    clipRenameEditor->setBounds (edX, trackY + tH - 24, juce::jmax (60, edW), 20);

    clipRenameEditor->onReturnKey = [this] { commitClipRename(); };
    clipRenameEditor->onEscapeKey = [this] { clipRenameEditor.reset(); clipBeingRenamed = nullptr; repaint(); };
    clipRenameEditor->onFocusLost = [this] { commitClipRename(); };

    addAndMakeVisible (*clipRenameEditor);
    clipRenameEditor->grabKeyboardFocus();
}

void TimelineComponent::commitClipRename()
{
    if (! clipRenameEditor || ! clipBeingRenamed) return;
    const juce::String newName = clipRenameEditor->getText();
    clipRenameEditor.reset();
    if (newName.isNotEmpty())
        clipBeingRenamed->setName (newName);
    clipBeingRenamed = nullptr;
    repaint();
}

void TimelineComponent::mouseDoubleClick (const juce::MouseEvent& e)
{
    // Double-click on ruler → add marker
    if (e.y < rulerH && e.x > trackHeaderW)
    {
        const double posSec = xToSeconds ((float) e.x);
        engine.addMarker ("Marker " + juce::String (engine.getMarkers().size() + 1), posSec);
        return;
    }

    if (e.x < trackHeaderW || e.y < rulerH) return;

    auto* clip = clipAtPoint (e.x, e.y);
    if (clip == nullptr) return;

    engine.selectClip (clip);

    // Right-click / Ctrl+click also shows a context menu via PopupMenu.
    juce::PopupMenu menu;
    menu.addItem (1, "Rename Clip");
    menu.addItem (2, "Duplicate Clip");
    menu.addSeparator();
    menu.addItem (3, "Delete Clip");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
        [this, clip] (int result)
        {
            if (result == 1)
            {
                startClipRename (clip);
            }
            else if (result == 2)
            {
                // Duplicate: insert an identical clip immediately after.
                auto* owner = dynamic_cast<te::ClipTrack*> (clip->getParent());
                if (! owner) return;

                const double endSec  = clip->getPosition().getEnd().inSeconds();
                const double lenSec  = clip->getPosition().getLength().inSeconds();
                const tracktion::TimeRange newRange {
                    TimePosition::fromSeconds (endSec),
                    TimePosition::fromSeconds (endSec + lenSec)
                };

                if (auto* wc = dynamic_cast<te::WaveAudioClip*> (clip))
                    owner->insertWaveClip (wc->getName(), wc->getAudioFile().getFile(),
                                          tracktion::ClipPosition { newRange }, false);
                repaint();
            }
            else if (result == 3)
            {
                engine.selectClip (clip);
                engine.deleteSelectedClip();
            }
        });
}

void TimelineComponent::mouseWheelMove (const juce::MouseEvent& e,
                                         const juce::MouseWheelDetails& w)
{
    // Studio One scroll model:
    //   Ctrl  + vertical wheel  → zoom in / out (anchored at cursor)
    //   plain   vertical wheel  → horizontal pan  (same direction as horizontal scroll)
    //   horizontal trackpad swipe → horizontal pan

    const bool ctrlDown = e.mods.isCtrlDown() || e.mods.isCommandDown();

    if (ctrlDown)
    {
        // Zoom, keeping the time under the cursor stationary.
        const double timeBefore = xToSeconds ((float) e.x);
        setPixelsPerSecond (pixelsPerSecond * (1.0 + (double) w.deltaY * 0.18));
        viewStartSeconds = juce::jmax (0.0, viewStartSeconds + timeBefore - xToSeconds ((float) e.x));
    }
    else
    {
        // Pan horizontally.  Use whichever axis has more movement, so both
        // a vertical scroll wheel and a horizontal trackpad swipe work.
        const double delta = std::abs (w.deltaX) >= std::abs (w.deltaY) ? w.deltaX : -w.deltaY;
        viewStartSeconds   = juce::jmax (0.0,
            viewStartSeconds - delta * 80.0 / pixelsPerSecond);
    }

    repaint();
}

// ── Drag and drop ─────────────────────────────────────────────────────────────

bool TimelineComponent::isInterestedInFileDrag (const juce::StringArray& files)
{
    static const juce::StringArray exts { ".wav", ".aif", ".aiff", ".mp3",
                                           ".ogg", ".flac", ".caf", ".m4a" };
    for (auto& f : files)
        for (auto& e : exts)
            if (f.endsWithIgnoreCase (e)) return true;
    return false;
}

void TimelineComponent::fileDragEnter (const juce::StringArray&, int x, int y)
{
    isDragOver       = true;
    dragOverSeconds  = juce::jmax (0.0, xToSeconds ((float) x));
    dragOverTrackIdx = trackIndexAtY (y);
    repaint();
}

void TimelineComponent::fileDragExit (const juce::StringArray&)
{
    isDragOver = false;
    repaint();
}

void TimelineComponent::filesDropped (const juce::StringArray& files, int x, int y)
{
    isDragOver = false;

    te::Edit* edit = engine.getCurrentEdit();
    if (! edit) { repaint(); return; }

    auto* dropTrack = clipTrackAtY (y);
    if (dropTrack == nullptr)
    {
        engine.addAudioTrack ("Audio Track");
        dropTrack = clipTrackAtY (y);
        if (! dropTrack) { repaint(); return; }
    }

    const double insertSec = juce::jmax (0.0, xToSeconds ((float) x));
    double offset = 0.0;

    for (auto& filePath : files)
    {
        juce::File file (filePath);
        if (! file.existsAsFile()) continue;

        double dur = 4.0;
        if (formatManager)
        {
            std::unique_ptr<juce::AudioFormatReader> reader (formatManager->createReaderFor (file));
            if (reader && reader->sampleRate > 0.0)
                dur = (double) reader->lengthInSamples / reader->sampleRate;
        }

        const tracktion::TimeRange range { TimePosition::fromSeconds (insertSec + offset),
                                           TimePosition::fromSeconds (insertSec + offset + dur) };
        dropTrack->insertWaveClip (file.getFileNameWithoutExtension(), file,
                                   tracktion::ClipPosition { range }, false);
        offset += dur;
    }

    repaint();
}

// ── Per-track header controls ─────────────────────────────────────────────────

void TimelineComponent::rebuildTrackControls()
{
    // Remove all old controls.
    for (auto& tc : trackControls)
    {
        removeChildComponent (tc->volSlider.get());
        removeChildComponent (tc->muteBtn.get());
        removeChildComponent (tc->soloBtn.get());
    }
    trackControls.clear();

    // Preserve existing heights across rebuild.
    const auto oldHeights = trackHeights;
    trackHeights.clear();

    auto* edit = engine.getCurrentEdit();
    if (! edit) { repaint(); return; }

    edit->getTrackList().visitAllTopLevel ([this, &oldHeights] (te::Track& t) -> bool
    {
        if (! t.isAudioTrack()) return true;
        auto* at = dynamic_cast<te::AudioTrack*> (&t);
        if (! at) return true;

        // Preserve previous height for this track index, or use default.
        const int idx = static_cast<int> (trackHeights.size());
        trackHeights.push_back (idx < (int) oldHeights.size() ? oldHeights[(size_t) idx] : defaultTrackH);

        auto tc = std::make_unique<TrackControls>();
        tc->track = at;

        // Volume slider
        tc->volSlider = std::make_unique<juce::Slider> (juce::Slider::LinearHorizontal,
                                                         juce::Slider::NoTextBox);
        tc->volSlider->setRange (0.0, 1.0);
        tc->volSlider->setColour (juce::Slider::trackColourId,      juce::Colour (0xff004d50));
        tc->volSlider->setColour (juce::Slider::thumbColourId,       juce::Colour (0xff00acc1));
        tc->volSlider->setColour (juce::Slider::backgroundColourId,  juce::Colour (0xff222222));

        if (auto* vp = at->getVolumePlugin())
            tc->volSlider->setValue ((double) vp->getSliderPos(), juce::dontSendNotification);

        tc->volSlider->onValueChange = [atPtr = at, slPtr = tc->volSlider.get()]
        {
            if (auto* vp = atPtr->getVolumePlugin())
                vp->setSliderPos ((float) slPtr->getValue());
        };

        // Mute button
        tc->muteBtn = std::make_unique<juce::TextButton> ("M");
        tc->muteBtn->setColour (juce::TextButton::buttonColourId,   juce::Colour (0xff333333));
        tc->muteBtn->setColour (juce::TextButton::buttonOnColourId,  juce::Colour (0xffc62828));
        tc->muteBtn->setColour (juce::TextButton::textColourOffId,   juce::Colour (0xff888888));
        tc->muteBtn->setColour (juce::TextButton::textColourOnId,    juce::Colours::white);
        tc->muteBtn->setClickingTogglesState (true);
        tc->muteBtn->setToggleState (at->isMuted (false), juce::dontSendNotification);
        tc->muteBtn->onClick = [atPtr = at, btnPtr = tc->muteBtn.get()]
        {
            atPtr->setMute (btnPtr->getToggleState());
        };

        // Solo button
        tc->soloBtn = std::make_unique<juce::TextButton> ("S");
        tc->soloBtn->setColour (juce::TextButton::buttonColourId,   juce::Colour (0xff333333));
        tc->soloBtn->setColour (juce::TextButton::buttonOnColourId,  juce::Colour (0xffe65100));
        tc->soloBtn->setColour (juce::TextButton::textColourOffId,   juce::Colour (0xff888888));
        tc->soloBtn->setColour (juce::TextButton::textColourOnId,    juce::Colours::white);
        tc->soloBtn->setClickingTogglesState (true);
        tc->soloBtn->setToggleState (at->isSolo (false), juce::dontSendNotification);
        tc->soloBtn->onClick = [atPtr = at, btnPtr = tc->soloBtn.get()]
        {
            atPtr->setSolo (btnPtr->getToggleState());
        };

        addAndMakeVisible (*tc->volSlider);
        addAndMakeVisible (*tc->muteBtn);
        addAndMakeVisible (*tc->soloBtn);
        trackControls.push_back (std::move (tc));
        return true;
    });

    layoutTrackControls();
    repaint();
}

void TimelineComponent::layoutTrackControls()
{
    for (int i = 0; i < (int) trackControls.size(); ++i)
    {
        auto& tc  = *trackControls[(size_t) i];
        const int y = getTrackY (i);
        const int h = getTrackH (i);

        // Mute (M) and Solo (S) buttons: top-right corner, side by side.
        tc.muteBtn->setBounds (trackHeaderW - 54, y + 6, 24, 18);
        tc.soloBtn->setBounds (trackHeaderW - 28, y + 6, 24, 18);

        // Volume slider: lower portion of header (above the resize handle).
        const int sliderY = y + h - 22;
        tc.volSlider->setBounds (6, sliderY, trackHeaderW - 14, 16);
    }
}

void TimelineComponent::syncTrackControls()
{
    for (auto& tc : trackControls)
    {
        if (tc->volSlider->isMouseButtonDown()) continue;   // don't fight the user

        if (auto* vp = tc->track->getVolumePlugin())
        {
            const double engineVal = (double) vp->getSliderPos();
            if (std::abs (tc->volSlider->getValue() - engineVal) > 0.001)
                tc->volSlider->setValue (engineVal, juce::dontSendNotification);
        }

        tc->muteBtn->setToggleState (tc->track->isMuted (false), juce::dontSendNotification);
        tc->soloBtn->setToggleState (tc->track->isSolo  (false), juce::dontSendNotification);
    }
}

// ── Timer / listener ──────────────────────────────────────────────────────────

void TimelineComponent::timerCallback()
{
    if (engine.isPlaying())
    {
        const double pos     = engine.getCurrentPositionSeconds();
        const double visible = static_cast<double> (getWidth() - trackHeaderW) / pixelsPerSecond;
        if (pos > viewStartSeconds + visible * 0.8)
            viewStartSeconds = pos - visible * 0.6;
        repaint();
    }

    syncTrackControls();
}

void TimelineComponent::trackListChanged()
{
    rebuildTrackControls();
    repaint();
}

void TimelineComponent::activeSongChanged (int)
{
    thumbnails.clear();   // clips are gone; release old thumbnails
    viewStartSeconds = 0.0;
    rebuildTrackControls();
    repaint();
}

void TimelineComponent::resized()
{
    layoutTrackControls();
}
