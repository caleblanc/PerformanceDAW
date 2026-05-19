#pragma once

#include <JuceHeader.h>
#include <tracktion_engine/tracktion_engine.h>

namespace te = tracktion_engine;

class PlaybackEngine
{
public:
    // ── Listener ─────────────────────────────────────────────────────────────
    struct Listener
    {
        virtual ~Listener() = default;
        virtual void activeSongChanged     (int newIndex) {}
        virtual void transportStateChanged () {}
        virtual void trackListChanged      () {}
        virtual void trackSelectionChanged (te::Track* newSelection) {}
        virtual void clipSelectionChanged  (te::Clip*  newSelection) {}
    };

    // ── Lifecycle ─────────────────────────────────────────────────────────────
    PlaybackEngine();
    ~PlaybackEngine();

    void initialiseAudio (int numInputs = 0, int numOutputs = 32);
    te::Engine& getEngine() { return *engine; }

    // ── Setlist ───────────────────────────────────────────────────────────────
    int  createNewSong (const juce::String& name);
    int  loadSong      (const juce::File& file);

    int          getSetlistSize()          const { return static_cast<int> (setlist.size()); }
    juce::String getSongName     (int index) const;
    double       getSongDuration (int index) const;   // seconds; 0 if no clips
    void         renameSong  (int index, const juce::String& newName);
    void         switchToSong (int index);
    int          getCurrentSongIndex()     const { return currentIndex; }
    te::Edit*    getCurrentEdit()          const { return currentEdit; }

    // ── Track management (operates on the current edit) ───────────────────────
    void addAudioTrack  (const juce::String& name = "Audio Track");
    void removeTrack    (te::Track* track);

    // Track selection — drives the Inspector panel.
    void       selectTrack     (te::Track* track);
    te::Track* getSelectedTrack() const { return selectedTrack; }

    // Clip selection — drives clip operations.
    void      selectClip        (te::Clip* clip);
    te::Clip* getSelectedClip   () const { return selectedClip; }
    void      deleteSelectedClip();

    // Persist the current edit to disk.
    bool saveCurrentSong  (const juce::File& file);
    bool loadSongFromFile (const juce::File& file);   // adds to setlist & switches

    // Route a track's output to a TE output device by its ID string.
    // deviceID comes from WaveOutputDevice::getName() or getDeviceID().
    void setTrackOutputDevice (te::AudioTrack* track, const juce::String& deviceID);

    // Import an audio file onto a track at a given time position.
    // Notifies trackListChanged so the timeline repaints.
    void importAudioFile (te::AudioTrack* track, const juce::File& file, double atSeconds = 0.0);

    // ── Auto-advance ──────────────────────────────────────────────────────────
    bool getAutoAdvance()       const { return autoAdvance; }
    void setAutoAdvance (bool b)      { autoAdvance = b; }

    // ── Transport ─────────────────────────────────────────────────────────────
    void play();
    void stop();
    void setLooping (bool loop);
    bool isPlaying()  const;
    bool isLooping()  const;

    double       getCurrentPositionSeconds() const;
    void         setPositionSeconds (double t);
    juce::String getPositionAsBBT()  const;

    // ── Listeners ─────────────────────────────────────────────────────────────
    void addListener    (Listener* l) { listeners.add (l); }
    void removeListener (Listener* l) { listeners.remove (l); }

private:
    std::unique_ptr<te::Engine> engine;

    struct SongSlot
    {
        juce::String              name;
        std::unique_ptr<te::Edit> edit;
    };

    std::vector<SongSlot>       setlist;
    te::Edit*                   currentEdit   = nullptr;
    int                         currentIndex  = -1;
    te::Track*                  selectedTrack = nullptr;
    te::Clip*                   selectedClip  = nullptr;

    bool autoAdvance = false;

    juce::ListenerList<Listener> listeners;

    void activateEdit (te::Edit* edit);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlaybackEngine)
};
