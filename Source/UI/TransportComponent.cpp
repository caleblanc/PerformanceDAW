#include "TransportComponent.h"

TransportComponent::TransportComponent (PlaybackEngine& e) : engine (e)
{
    engine.addListener (this);

    auto configBtn = [this] (juce::Button& b, juce::Colour bg)
    {
        b.setColour (juce::TextButton::buttonColourId,   bg);
        b.setColour (juce::TextButton::buttonOnColourId, bg.brighter (0.3f));
        b.setColour (juce::TextButton::textColourOffId,  juce::Colours::white);
        addAndMakeVisible (b);
    };

    configBtn (playBtn,    juce::Colour (0xff2e7d32));   // green
    configBtn (stopBtn,    juce::Colour (0xffc62828));   // red
    configBtn (loopBtn,    juce::Colour (0xff1565c0));   // blue
    configBtn (advanceBtn, juce::Colour (0xff6a1b9a));   // purple

    playBtn.onClick = [this] { engine.play(); };
    stopBtn.onClick = [this] { engine.stop(); };
    loopBtn.onStateChange    = [this] { engine.setLooping    (loopBtn.getToggleState()); };
    advanceBtn.onStateChange = [this] { engine.setAutoAdvance (advanceBtn.getToggleState()); };
    addAndMakeVisible (advanceBtn);

    // BPM label — double-click to edit tempo.
    bpmLabel.setJustificationType (juce::Justification::centred);
    bpmLabel.setFont (juce::Font (14.f, juce::Font::bold));
    bpmLabel.setColour (juce::Label::textColourId,            juce::Colour (0xffdddddd));
    bpmLabel.setColour (juce::Label::backgroundColourId,      juce::Colour (0xff282828));
    bpmLabel.setColour (juce::Label::outlineColourId,         juce::Colour (0xff404040));
    bpmLabel.setColour (juce::Label::textWhenEditingColourId, juce::Colours::white);
    bpmLabel.setEditable (false, true, false);   // double-click to edit
    bpmLabel.onEditorHide = [this]
    {
        const double val = bpmLabel.getText().retainCharacters ("0123456789.")
                               .getDoubleValue();
        if (val >= 20.0 && val <= 400.0)
        {
            if (auto* edit = engine.getCurrentEdit())
            {
                auto& tempos = edit->tempoSequence;
                if (tempos.getTempos().size() > 0)
                    tempos.getTempos()[0]->setBpm (val);
            }
        }
        updateBpmLabel();
    };
    addAndMakeVisible (bpmLabel);
    updateBpmLabel();

    startTimerHz (30);
}

TransportComponent::~TransportComponent()
{
    engine.removeListener (this);
    stopTimer();
}

void TransportComponent::resized()
{
    auto area = getLocalBounds().reduced (6);

    const int btnW = 72, btnH = 36;
    const int btnY = (area.getHeight() - btnH) / 2;

    playBtn.setBounds    (area.getX(),                  area.getY() + btnY, btnW, btnH);
    stopBtn.setBounds    (area.getX() + btnW + 4,       area.getY() + btnY, btnW, btnH);
    loopBtn.setBounds    (area.getX() + btnW * 2 + 12,  area.getY() + btnY, btnW, btnH);
    advanceBtn.setBounds (area.getX() + btnW * 3 + 16,  area.getY() + btnY, btnW, btnH);

    // BPM / time-sig label sits to the right of the advance button, before the clock.
    const int bpmX = area.getX() + btnW * 4 + 28;
    const int bpmH = 34;
    bpmLabel.setBounds (bpmX, (getHeight() - bpmH) / 2, 140, bpmH);
}

void TransportComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1a1a1a));

    // Reserve right 2/3 for the clock display.
    auto clockArea = getLocalBounds()
                         .withTrimmedLeft (getWidth() / 3)
                         .reduced (8, 6);
    drawBigClock (g, clockArea);
}

void TransportComponent::drawBigClock (juce::Graphics& g, juce::Rectangle<int> area)
{
    const double pos = engine.getCurrentPositionSeconds();

    // BBT
    const juce::String bbt = engine.getPositionAsBBT();

    // MM:SS.ms
    const int totalMs  = static_cast<int> (pos * 1000.0);
    const int minutes  = totalMs / 60000;
    const int seconds  = (totalMs % 60000) / 1000;
    const int ms       = totalMs % 1000;
    const juce::String clock = juce::String (minutes).paddedLeft ('0', 2)
                             + ":"
                             + juce::String (seconds).paddedLeft ('0', 2)
                             + "."
                             + juce::String (ms).paddedLeft ('0', 3);

    const auto topHalf = area.removeFromTop (area.getHeight() / 2);

    g.setColour (juce::Colours::white);
    g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), (float) topHalf.getHeight() * 0.75f, juce::Font::bold));
    g.drawFittedText (bbt,   topHalf, juce::Justification::centredRight, 1);

    g.setColour (juce::Colour (0xffaaaaaa));
    g.setFont   (juce::Font (juce::Font::getDefaultMonospacedFontName(), (float) area.getHeight() * 0.65f, juce::Font::plain));
    g.drawFittedText (clock, area,    juce::Justification::centredRight, 1);
}

void TransportComponent::updateBpmLabel()
{
    double bpm    = 120.0;
    int    num    = 4;
    int    denom  = 4;

    if (auto* edit = engine.getCurrentEdit())
    {
        auto& tempos = edit->tempoSequence;
        if (tempos.getTempos().size() > 0)
            bpm = tempos.getTempos()[0]->getBpm();
        if (tempos.getTimeSigs().size() > 0)
        {
            num   = tempos.getTimeSigs()[0]->numerator.get();
            denom = tempos.getTimeSigs()[0]->denominator.get();
        }
    }

    bpmLabel.setText (juce::String (num) + "/" + juce::String (denom)
                      + "   " + juce::String (bpm, 1) + " BPM",
                      juce::dontSendNotification);
}

void TransportComponent::timerCallback()
{
    // Update play button brightness to reflect playback state.
    const juce::Colour playActive   (0xff43a047);   // bright green
    const juce::Colour playInactive (0xff2e7d32);   // dim green
    playBtn.setColour (juce::TextButton::buttonColourId,
                       engine.isPlaying() ? playActive : playInactive);

    // Auto-advance: if playing finished and auto-advance is on, go to next song.
    if (engine.getAutoAdvance() && ! engine.isPlaying())
    {
        const double pos = engine.getCurrentPositionSeconds();
        const double dur = engine.getSongDuration (engine.getCurrentSongIndex());
        if (dur > 0.0 && pos >= dur - 0.1 && pos > 0.1)
        {
            const int next = engine.getCurrentSongIndex() + 1;
            if (next < engine.getSetlistSize())
            {
                engine.switchToSong (next);
                engine.play();
            }
        }
    }

    if (engine.isPlaying())
        repaint();

    updateBpmLabel();
}

void TransportComponent::transportStateChanged()
{
    repaint();
}
