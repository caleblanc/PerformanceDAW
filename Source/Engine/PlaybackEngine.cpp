#include "PlaybackEngine.h"

namespace te = tracktion_engine;

// Time types live in tracktion::core, not tracktion::engine.
using tracktion::TimePosition;
using tracktion::TimeDuration;

PlaybackEngine::PlaybackEngine()
{
    engine = std::make_unique<te::Engine> ("PerformanceDAW");
}

PlaybackEngine::~PlaybackEngine()
{
    if (currentEdit)
        currentEdit->getTransport().stop (false, false);
}

// ── Audio setup ───────────────────────────────────────────────────────────────

void PlaybackEngine::initialiseAudio (int numInputs, int numOutputs)
{
    engine->getDeviceManager().initialise (numInputs, numOutputs);
}

// ── Setlist ───────────────────────────────────────────────────────────────────

int PlaybackEngine::createNewSong (const juce::String& name)
{
    auto& slot = setlist.emplace_back();
    slot.name  = name;

    // Two-argument constructor: engine ref + role.
    slot.edit  = std::make_unique<te::Edit> (*engine, te::Edit::forEditing);

    // 120 BPM default.
    auto& tempos = slot.edit->tempoSequence;
    if (tempos.getTempos().size() > 0)
        tempos.getTempos()[0]->setBpm (120.0);

    // One audio track is sufficient — MIDI clips live on audio tracks in TE9.
    auto tip = te::TrackInsertPoint::getEndOfTracks (*slot.edit);
    slot.edit->insertNewAudioTrack (tip, nullptr);

    if (currentEdit == nullptr)
        switchToSong (static_cast<int> (setlist.size()) - 1);

    return static_cast<int> (setlist.size()) - 1;
}

int PlaybackEngine::loadSong (const juce::File& file)
{
    if (! file.existsAsFile()) return -1;

    auto& slot = setlist.emplace_back();
    slot.name  = file.getFileNameWithoutExtension();

    // Load from file using the Options struct so we can provide the state.
    te::Edit::Options opts { *engine,
                             te::loadValueTree (file, true),
                             te::ProjectItemID::createNewID (0) };
    slot.edit = te::Edit::createEdit (opts);

    return static_cast<int> (setlist.size()) - 1;
}

juce::String PlaybackEngine::getSongName (int index) const
{
    if (index < 0 || index >= static_cast<int> (setlist.size())) return {};
    return setlist[index].name;
}

double PlaybackEngine::getSongDuration (int index) const
{
    if (index < 0 || index >= static_cast<int> (setlist.size())) return 0.0;
    auto* edit = setlist[index].edit.get();
    if (! edit) return 0.0;

    double maxEnd = 0.0;
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
    return maxEnd;
}

void PlaybackEngine::renameSong (int index, const juce::String& newName)
{
    if (index < 0 || index >= static_cast<int> (setlist.size())) return;
    setlist[index].name = newName;
    // Notify so the setlist repaints.
    const int idx = index;
    listeners.call ([idx] (Listener& l) { l.activeSongChanged (idx); });
}

void PlaybackEngine::switchToSong (int index)
{
    if (index < 0 || index >= static_cast<int> (setlist.size())) return;
    if (index == currentIndex) return;

    const bool wasPlaying = isPlaying();

    if (currentEdit)
        currentEdit->getTransport().stop (false, false);

    currentIndex = index;
    currentEdit  = setlist[index].edit.get();

    activateEdit (currentEdit);

    currentEdit->getTransport().setPosition (TimePosition::fromSeconds (0.0));

    if (wasPlaying)
        currentEdit->getTransport().play (false);

    const int idx = currentIndex;
    listeners.call ([idx] (Listener& l) { l.activeSongChanged (idx); });
}

void PlaybackEngine::activateEdit (te::Edit* edit)
{
    if (edit == nullptr) return;
    edit->getTransport().ensureContextAllocated (true);
}

// ── Track management ─────────────────────────────────────────────────────────

void PlaybackEngine::addAudioTrack (const juce::String& name)
{
    if (! currentEdit) return;

    auto tip    = te::TrackInsertPoint::getEndOfTracks (*currentEdit);
    auto* track = currentEdit->insertNewAudioTrack (tip, nullptr).get();
    if (track && name.isNotEmpty())
        track->setName (name);

    listeners.call ([] (Listener& l) { l.trackListChanged(); });
}

void PlaybackEngine::removeTrack (te::Track* track)
{
    if (! currentEdit || ! track) return;

    if (selectedTrack == track)
    {
        selectedTrack = nullptr;
        listeners.call ([] (Listener& l) { l.trackSelectionChanged (nullptr); });
    }

    currentEdit->deleteTrack (track);
    listeners.call ([] (Listener& l) { l.trackListChanged(); });
}

void PlaybackEngine::selectTrack (te::Track* track)
{
    if (selectedTrack == track) return;
    selectedTrack = track;
    listeners.call ([track] (Listener& l) { l.trackSelectionChanged (track); });
}

void PlaybackEngine::importAudioFile (te::AudioTrack* track, const juce::File& file, double atSeconds)
{
    if (! track || ! file.existsAsFile()) return;
    auto* clipTrack = dynamic_cast<te::ClipTrack*> (track);
    if (! clipTrack) return;

    // Read duration from the file header.
    double durationSec = 4.0;
    {
        auto& readFM = engine->getAudioFileFormatManager().readFormatManager;
        std::unique_ptr<juce::AudioFormatReader> reader (readFM.createReaderFor (file));
        if (reader && reader->sampleRate > 0.0)
            durationSec = static_cast<double> (reader->lengthInSamples) / reader->sampleRate;
    }

    const tracktion::TimeRange range { TimePosition::fromSeconds (atSeconds),
                                       TimePosition::fromSeconds (atSeconds + durationSec) };
    clipTrack->insertWaveClip (file.getFileNameWithoutExtension(),
                               file,
                               tracktion::ClipPosition { range },
                               false);

    listeners.call ([] (Listener& l) { l.trackListChanged(); });
}

void PlaybackEngine::selectClip (te::Clip* clip)
{
    if (selectedClip == clip) return;
    selectedClip = clip;
    listeners.call ([clip] (Listener& l) { l.clipSelectionChanged (clip); });
}

void PlaybackEngine::deleteSelectedClip()
{
    if (! selectedClip || ! currentEdit) return;

    auto* owner = selectedClip->getParent();
    if (! owner) return;

    const auto range = selectedClip->getPosition().time;
    selectedClip = nullptr;
    listeners.call ([] (Listener& l) { l.clipSelectionChanged (nullptr); });

    te::deleteRegion (*owner, range);
    listeners.call ([] (Listener& l) { l.trackListChanged(); });
}

bool PlaybackEngine::saveCurrentSong (const juce::File& file)
{
    if (! currentEdit) return false;
    currentEdit->flushState();
    if (auto xml = currentEdit->state.createXml())
        return xml->writeTo (file);
    return false;
}

bool PlaybackEngine::loadSongFromFile (const juce::File& file)
{
    const int idx = loadSong (file);
    if (idx < 0) return false;
    switchToSong (idx);
    return true;
}

void PlaybackEngine::setTrackOutputDevice (te::AudioTrack* track, const juce::String& deviceID)
{
    if (! track) return;
    // Routes the track to a named output device — e.g. a specific stereo pair
    // on the X32 Rack such as "X32 RACK Bus Out 1+2".
    track->getOutput().setOutputToDeviceID (deviceID);
}

// ── Transport ─────────────────────────────────────────────────────────────────

void PlaybackEngine::play()
{
    if (currentEdit) currentEdit->getTransport().play (false);
}

void PlaybackEngine::stop()
{
    if (currentEdit) currentEdit->getTransport().stop (false, false);
}

void PlaybackEngine::setLooping (bool loop)
{
    if (currentEdit) currentEdit->getTransport().looping = loop;
}

bool PlaybackEngine::isPlaying() const
{
    return currentEdit && currentEdit->getTransport().isPlaying();
}

bool PlaybackEngine::isLooping() const
{
    return currentEdit && currentEdit->getTransport().looping;
}

double PlaybackEngine::getCurrentPositionSeconds() const
{
    if (currentEdit)
        return currentEdit->getTransport().getPosition().inSeconds();
    return 0.0;
}

void PlaybackEngine::setPositionSeconds (double t)
{
    if (currentEdit)
        currentEdit->getTransport().setPosition (TimePosition::fromSeconds (t));
}

juce::String PlaybackEngine::getPositionAsBBT() const
{
    if (! currentEdit) return "1 | 1 | 0000";

    const auto pos    = TimePosition::fromSeconds (getCurrentPositionSeconds());
    const auto bb     = currentEdit->tempoSequence.toBarsAndBeats (pos);

    const int bar  = bb.bars + 1;
    const int beat = bb.getWholeBeats() + 1;
    const int tick = static_cast<int> (bb.getFractionalBeats().inBeats() * 960.0);

    return juce::String (bar)  + " | "
         + juce::String (beat) + " | "
         + juce::String (tick).paddedLeft ('0', 4);
}
