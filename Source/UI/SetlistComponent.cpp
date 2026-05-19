#include "SetlistComponent.h"

SetlistComponent::SetlistComponent (PlaybackEngine& e) : engine (e)
{
    engine.addListener (this);

    addSongBtn.setColour (juce::TextButton::buttonColourId,  juce::Colour (0xff333333));
    addSongBtn.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
    addSongBtn.onClick = [this] { addSong ("New Song " + juce::String (engine.getSetlistSize() + 1)); };
    addAndMakeVisible (addSongBtn);
}

SetlistComponent::~SetlistComponent()
{
    engine.removeListener (this);
}

void SetlistComponent::addSong (const juce::String& name)
{
    engine.createNewSong (name);
    resized();
    repaint();
}

void SetlistComponent::resized()
{
    addSongBtn.setBounds (getLocalBounds().removeFromBottom (36).reduced (4, 4));
}

void SetlistComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff202020));

    // Title
    g.setColour (juce::Colour (0xff888888));
    g.setFont   (12.0f);
    g.drawText  ("SETLIST", getLocalBounds().removeFromTop (22).reduced (8, 0),
                 juce::Justification::centredLeft);

    // Rows
    const int listTop = 22;
    for (int i = 0; i < engine.getSetlistSize(); ++i)
    {
        auto rowBounds = juce::Rectangle<int> (0, listTop + i * rowH, getWidth(), rowH);
        paintRow (g, i, rowBounds);
    }
}

void SetlistComponent::paintRow (juce::Graphics& g, int index, juce::Rectangle<int> b)
{
    const bool isActive = (index == engine.getCurrentSongIndex());

    g.setColour (isActive ? juce::Colour (0xff2e7d32) : juce::Colour (0xff2a2a2a));
    g.fillRect  (b.reduced (2, 1));

    // Song name (top half of row)
    auto nameArea = b.reduced (10, 0).removeFromTop (b.getHeight() / 2 + 4);
    g.setColour (juce::Colours::white);
    g.setFont   (14.0f);
    g.drawText  (juce::String (index + 1) + ".  " + engine.getSongName (index),
                 nameArea, juce::Justification::centredLeft);

    // Duration (bottom half, dimmer)
    const double dur = engine.getSongDuration (index);
    if (dur > 0.0)
    {
        const int totalSec = static_cast<int> (dur);
        const int m  = totalSec / 60;
        const int s  = totalSec % 60;
        const juce::String durStr = juce::String (m) + ":"
                                  + juce::String (s).paddedLeft ('0', 2);
        auto durArea = b.reduced (10, 0).removeFromBottom (b.getHeight() / 2 - 4);
        g.setColour (isActive ? juce::Colour (0xffaaffaa) : juce::Colour (0xff888888));
        g.setFont   (11.0f);
        g.drawText  (durStr, durArea, juce::Justification::centredLeft);
    }

    g.setColour (juce::Colour (0xff3a3a3a));
    g.drawLine  (0.0f, (float) b.getBottom(), (float) getWidth(), (float) b.getBottom(), 1.0f);
}

void SetlistComponent::mouseDown (const juce::MouseEvent& e)
{
    const int listTop = 22;
    const int clickedRow = (e.y - listTop) / rowH;
    if (clickedRow >= 0 && clickedRow < engine.getSetlistSize())
        engine.switchToSong (clickedRow);
}

void SetlistComponent::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int listTop    = 22;
    const int clickedRow = (e.y - listTop) / rowH;
    if (clickedRow < 0 || clickedRow >= engine.getSetlistSize()) return;

    editingRow   = clickedRow;
    renameEditor = std::make_unique<juce::TextEditor>();
    renameEditor->setText (engine.getSongName (clickedRow), false);
    renameEditor->setSelectAllWhenFocused (true);
    renameEditor->setFont (juce::Font (15.f));
    renameEditor->setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff3a5c3a));
    renameEditor->setColour (juce::TextEditor::textColourId,       juce::Colours::white);
    renameEditor->setColour (juce::TextEditor::outlineColourId,    juce::Colours::transparentBlack);

    const auto rowBounds = juce::Rectangle<int> (0, listTop + clickedRow * rowH, getWidth(), rowH)
                               .reduced (2, 1);
    renameEditor->setBounds (rowBounds.reduced (4, 8));

    renameEditor->onReturnKey = [this] { commitRename(); };
    renameEditor->onEscapeKey = [this] { renameEditor.reset(); editingRow = -1; repaint(); };
    renameEditor->onFocusLost = [this] { commitRename(); };

    addAndMakeVisible (*renameEditor);
    renameEditor->grabKeyboardFocus();
}

void SetlistComponent::commitRename()
{
    if (! renameEditor || editingRow < 0) return;
    const juce::String newName = renameEditor->getText();
    renameEditor.reset();
    const int row = editingRow;
    editingRow = -1;
    if (newName.isNotEmpty())
        engine.renameSong (row, newName);
    repaint();
}

void SetlistComponent::activeSongChanged (int)
{
    repaint();
}
