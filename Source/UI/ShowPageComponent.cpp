#include "ShowPageComponent.h"

namespace te = tracktion_engine;

ShowPageComponent::ShowPageComponent (PlaybackEngine& e)
    : engine (e)
{
    engine.addListener (this);

    auto styleBtn = [&] (juce::TextButton& btn, juce::Colour bg)
    {
        btn.setColour (juce::TextButton::buttonColourId,   bg);
        btn.setColour (juce::TextButton::buttonOnColourId, bg.brighter (0.3f));
        btn.setColour (juce::TextButton::textColourOffId,  juce::Colours::white);
        addAndMakeVisible (btn);
    };

    styleBtn (playBtn, juce::Colour (0xff2a7a2a));
    styleBtn (stopBtn, juce::Colour (0xff7a2a2a));

    playBtn.onClick = [this] { engine.play(); };
    stopBtn.onClick = [this]
    {
        engine.stop();
        engine.setPositionSeconds (0.0);
    };

    startTimerHz (30);
}

ShowPageComponent::~ShowPageComponent()
{
    stopTimer();
    engine.removeListener (this);
}

// ── Layout ────────────────────────────────────────────────────────────────────

void ShowPageComponent::resized()
{
    auto area = getLocalBounds();

    // Button row at the bottom of the top strip.
    auto topStrip = area.removeFromTop (getHeight() / 3);
    auto btnRow   = topStrip.removeFromBottom (52).reduced (8, 6);
    const int btnW = btnRow.getWidth() / 2 - 4;
    playBtn.setBounds (btnRow.removeFromLeft (btnW));
    btnRow.removeFromLeft (8);
    stopBtn.setBounds (btnRow.removeFromLeft (btnW));
}

// ── Paint ─────────────────────────────────────────────────────────────────────

void ShowPageComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff0a0a0a));

    auto area = getLocalBounds();
    const int h = area.getHeight();

    auto topStrip   = area.removeFromTop (h / 3);
    auto midStrip   = area.removeFromTop (60);
    auto markerStrip = area.removeFromTop (50);
    // rest = setlist

    paintNowPlaying  (g, topStrip);
    paintProgressBar (g, midStrip);
    paintMarkerCues  (g, markerStrip);
    paintSetlist     (g, area);
}

// ── Now Playing ───────────────────────────────────────────────────────────────

void ShowPageComponent::paintNowPlaying (juce::Graphics& g, juce::Rectangle<int> area)
{
    const int idx  = engine.getCurrentSongIndex();
    const auto songName = (idx >= 0) ? engine.getSongName (idx) : juce::String ("— No Song —");

    // Divider line at bottom.
    g.setColour (juce::Colour (0xff333333));
    g.drawHorizontalLine (area.getBottom() - 1, (float) area.getX(), (float) area.getRight());

    // Large song name (top 55% of strip, minus button row at bottom).
    auto nameArea = area.removeFromTop (static_cast<int> (area.getHeight() * 0.55f));
    g.setFont (juce::Font (juce::FontOptions{}.withHeight (static_cast<float> (nameArea.getHeight()) * 0.55f)
                                              .withStyle ("Bold")));
    g.setColour (juce::Colours::white);
    g.drawFittedText (songName, nameArea.reduced (24, 0),
                      juce::Justification::centredLeft, 1);

    // Position: BBT + MM:SS — right side of name area.
    const auto bbt  = engine.getPositionAsBBT();
    const auto mmss = formatTime (engine.getCurrentPositionSeconds());
    const auto posText = bbt + "     " + mmss;

    g.setFont (juce::Font (juce::FontOptions{}.withHeight (28.0f)
                                              .withStyle ("Regular")));
    g.setColour (juce::Colour (0xffa0a0a0));
    g.drawFittedText (posText, nameArea.reduced (24, 0),
                      juce::Justification::centredRight, 1);

    // Playing indicator dot.
    if (engine.isPlaying())
    {
        g.setColour (juce::Colour (0xff40e040));
        g.fillEllipse (area.getRight() - 36.0f, area.getY() + 10.0f, 16.0f, 16.0f);
    }
}

// ── Progress bar ──────────────────────────────────────────────────────────────

void ShowPageComponent::paintProgressBar (juce::Graphics& g, juce::Rectangle<int> area)
{
    const int idx  = engine.getCurrentSongIndex();
    const double dur = (idx >= 0) ? engine.getSongDuration (idx) : 0.0;
    const double pos = engine.getCurrentPositionSeconds();
    const float progress = (dur > 0.0) ? static_cast<float> (juce::jlimit (0.0, 1.0, pos / dur)) : 0.0f;

    auto bar = area.reduced (16, 14);

    // Track.
    g.setColour (juce::Colour (0xff2a2a2a));
    g.fillRoundedRectangle (bar.toFloat(), 5.0f);

    // Fill.
    if (progress > 0.0f)
    {
        auto fill = bar.toFloat().withWidth (bar.getWidth() * progress);
        g.setColour (juce::Colour (0xff3a7abf));
        g.fillRoundedRectangle (fill, 5.0f);
    }

    // "NEXT:" label.
    const int nextIdx = idx + 1;
    if (nextIdx < engine.getSetlistSize())
    {
        const juce::String nextLabel = "NEXT:  " + engine.getSongName (nextIdx);
        g.setFont (juce::Font (juce::FontOptions{}.withHeight (14.0f)));
        g.setColour (juce::Colour (0xff707070));
        g.drawFittedText (nextLabel, bar.removeFromRight (280),
                          juce::Justification::centredRight, 1);
    }
}

// ── Marker cues ───────────────────────────────────────────────────────────────

void ShowPageComponent::paintMarkerCues (juce::Graphics& g, juce::Rectangle<int> area)
{
    g.setColour (juce::Colour (0xff1a1a1a));
    g.fillRect (area);
    g.setColour (juce::Colour (0xff333333));
    g.drawHorizontalLine (area.getBottom() - 1, (float) area.getX(), (float) area.getRight());

    const auto& markers = engine.getMarkers();
    if (markers.empty()) return;

    const double pos = engine.getCurrentPositionSeconds();

    // Find the next upcoming marker.
    int nextMarkerIdx = -1;
    for (int i = 0; i < static_cast<int> (markers.size()); ++i)
        if (markers[i].positionSeconds > pos + 0.1)
        {
            nextMarkerIdx = i;
            break;
        }

    g.setFont (juce::Font (juce::FontOptions{}.withHeight (15.0f)));

    auto row = area.reduced (16, 8);
    const int cellW = 200;
    int drawn = 0;

    // Draw up to 4 upcoming markers.
    for (int i = (nextMarkerIdx >= 0 ? nextMarkerIdx : 0);
         i < static_cast<int> (markers.size()) && drawn < 4; ++i)
    {
        const auto& m = markers[i];
        const double timeLeft = m.positionSeconds - pos;
        const bool isCurrent  = (nextMarkerIdx >= 0 && i == nextMarkerIdx);

        auto cell = row.removeFromLeft (cellW);

        g.setColour (isCurrent ? juce::Colour (0xffffcc00) : juce::Colour (0xff707070));
        g.drawFittedText (m.name + "\n" + formatTime (m.positionSeconds)
                          + (timeLeft > 0 ? "  (-" + formatTime (timeLeft) + ")" : ""),
                          cell, juce::Justification::centredLeft, 2);

        row.removeFromLeft (8);
        ++drawn;
    }
}

// ── Setlist ───────────────────────────────────────────────────────────────────

void ShowPageComponent::paintSetlist (juce::Graphics& g, juce::Rectangle<int> area)
{
    const int currentIdx = engine.getCurrentSongIndex();
    const int numSongs   = engine.getSetlistSize();

    g.setFont (juce::Font (juce::FontOptions{}.withHeight (20.0f)));

    for (int i = 0; i < numSongs; ++i)
    {
        const auto row = area.removeFromTop (rowH);

        const bool isActive = (i == currentIdx);
        const bool isNext   = (i == currentIdx + 1);

        // Row background.
        if (isActive)
            g.setColour (juce::Colour (0xff1e4080));
        else if (isNext)
            g.setColour (juce::Colour (0xff1a2a1a));
        else
            g.setColour (juce::Colour (i % 2 == 0 ? 0xff111111 : 0xff151515));

        g.fillRect (row);

        // Number.
        g.setColour (isActive ? juce::Colours::white : juce::Colour (0xff606060));
        g.setFont (juce::Font (juce::FontOptions{}.withHeight (14.0f)));
        g.drawFittedText (juce::String (i + 1), row.withWidth (36),
                          juce::Justification::centred, 1);

        // Name.
        g.setColour (isActive ? juce::Colours::white
                              : isNext ? juce::Colour (0xffcccccc)
                                       : juce::Colour (0xff808080));
        g.setFont (juce::Font (juce::FontOptions{}.withHeight (isActive ? 22.0f : 18.0f)
                                                  .withStyle (isActive ? "Bold" : "Regular")));
        g.drawFittedText (engine.getSongName (i),
                          row.withTrimmedLeft (44).withTrimmedRight (100),
                          juce::Justification::centredLeft, 1);

        // Duration.
        const double dur = engine.getSongDuration (i);
        if (dur > 0.0)
        {
            g.setFont (juce::Font (juce::FontOptions{}.withHeight (14.0f)));
            g.setColour (juce::Colour (0xff505050));
            g.drawFittedText (formatTime (dur),
                              row.withTrimmedLeft (row.getWidth() - 90),
                              juce::Justification::centredRight, 1);
        }

        // Active indicator bar.
        if (isActive)
        {
            g.setColour (juce::Colour (0xff3a7abf));
            g.fillRect (row.getX(), row.getY(), 4, row.getHeight());
        }

        // Divider.
        g.setColour (juce::Colour (0xff222222));
        g.drawHorizontalLine (row.getBottom() - 1, (float) row.getX(), (float) row.getRight());
    }
}

// ── Mouse ─────────────────────────────────────────────────────────────────────

void ShowPageComponent::mouseDown (const juce::MouseEvent& e)
{
    const int h = getHeight();
    const int setlistTop = h / 3 + 60 + 50;   // topStrip + midStrip + markerStrip

    if (e.y < setlistTop) return;

    const int row = (e.y - setlistTop) / rowH;
    if (row >= 0 && row < engine.getSetlistSize())
        engine.switchToSong (row);
}

// ── Helpers ───────────────────────────────────────────────────────────────────

juce::String ShowPageComponent::formatTime (double sec) const
{
    const int totalSec = static_cast<int> (sec);
    const int m  = totalSec / 60;
    const int s  = totalSec % 60;
    return juce::String (m) + ":" + juce::String (s).paddedLeft ('0', 2);
}
