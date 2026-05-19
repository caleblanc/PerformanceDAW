#pragma once

#include <JuceHeader.h>
#include "../Engine/PlaybackEngine.h"

// Sidebar: a vertical list of songs in the setlist.
// Click a row to instantly switch to that song.
// The active song is highlighted in green.
class SetlistComponent : public juce::Component,
                         public PlaybackEngine::Listener
{
public:
    explicit SetlistComponent (PlaybackEngine& engine);
    ~SetlistComponent() override;

    void paint            (juce::Graphics& g) override;
    void resized          () override;
    void mouseDown        (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;

    void addSong (const juce::String& name);   // convenience; wraps engine call

private:
    PlaybackEngine& engine;

    juce::TextButton addSongBtn { "+ New Song" };

    static constexpr int rowH = 44;

    // Inline rename state
    std::unique_ptr<juce::TextEditor> renameEditor;
    int editingRow = -1;
    void commitRename();

    void activeSongChanged (int newIndex) override;
    void paintRow (juce::Graphics& g, int index, juce::Rectangle<int> rowBounds);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SetlistComponent)
};
