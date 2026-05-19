#pragma once

#include <JuceHeader.h>
#include "../Engine/PlaybackEngine.h"

namespace te = tracktion_engine;

class TimelineComponent : public juce::Component,
                           public juce::Timer,
                           public juce::FileDragAndDropTarget,
                           public juce::ChangeListener,
                           public PlaybackEngine::Listener
{
public:
    explicit TimelineComponent (PlaybackEngine& engine);
    ~TimelineComponent() override;

    void paint   (juce::Graphics& g) override;
    void resized() override;

    void mouseDown      (const juce::MouseEvent& e) override;
    void mouseDrag      (const juce::MouseEvent& e) override;
    void mouseUp        (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent& e,
                         const juce::MouseWheelDetails& w) override;

    // FileDragAndDropTarget
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter  (const juce::StringArray& files, int x, int y) override;
    void fileDragExit   (const juce::StringArray& files) override;
    void filesDropped   (const juce::StringArray& files, int x, int y) override;

    void setPixelsPerSecond (double pps);
    double getPixelsPerSecond() const { return pixelsPerSecond; }

private:
    PlaybackEngine& engine;

    double pixelsPerSecond  = 120.0;
    double viewStartSeconds = 0.0;

    static constexpr int trackHeaderW = 160;
    static constexpr int rulerH       = 28;
    static constexpr int trackH       = 72;

    // Persistent thumbnail management.
    // AudioFormatManager pointer is borrowed from the TE engine — no ownership.
    juce::AudioFormatManager*  formatManager = nullptr;
    juce::AudioThumbnailCache  thumbnailCache { 128 };
    // Keyed by canonical file path.
    std::map<juce::String, std::unique_ptr<juce::AudioThumbnail>> thumbnails;

    juce::AudioThumbnail* getThumbnail (const juce::File& file);
    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }

    // Drag-and-drop state
    bool   isDragOver       = false;
    double dragOverSeconds  = 0.0;
    int    dragOverTrackIdx = -1;

    // ── Painting ──────────────────────────────────────────────────────────────
    void paintRuler       (juce::Graphics& g, juce::Rectangle<int> area);
    void paintTrackHeader (juce::Graphics& g, te::Track* track,
                           juce::Rectangle<int> area, bool isSelected, int trackIdx);
    void paintAudioClip   (juce::Graphics& g, te::Clip* clip, juce::Rectangle<int> area, int trackIdx);
    void paintMidiClip    (juce::Graphics& g, te::Clip* clip, juce::Rectangle<int> area, int trackIdx);
    void paintPlayhead    (juce::Graphics& g);
    void paintDropTarget  (juce::Graphics& g);

    // ── Coordinate helpers ────────────────────────────────────────────────────
    float  secondsToX     (double t) const;
    double xToSeconds     (float x)  const;
    int    trackIndexAtY  (int y)    const;
    te::ClipTrack* clipTrackAtY (int y) const;

    // ── Clip hit-testing ─────────────────────────────────────────────────────
    te::Clip* clipAtPoint (int x, int y) const;

    // ── Inline rename editors ─────────────────────────────────────────────────
    std::unique_ptr<juce::TextEditor> clipRenameEditor;
    te::Clip*                         clipBeingRenamed = nullptr;
    void startClipRename (te::Clip* clip);
    void commitClipRename();

    std::unique_ptr<juce::TextEditor> trackRenameEditor;
    te::Track*                        trackBeingRenamed = nullptr;
    void startTrackRename (te::Track* track, int trackY);
    void commitTrackRename();

    // ── Grid / snap ───────────────────────────────────────────────────────────
    // Snap clip positions to the nearest bar boundary unless Shift is held.
    double snapToBar (double seconds, bool shift) const;
    void   paintGrid (juce::Graphics& g, juce::Rectangle<int> clipArea);

    // ── Clip drag state ───────────────────────────────────────────────────────
    enum class DragMode { None, Move, ResizeLeft, ResizeRight };
    DragMode   clipDragMode  = DragMode::None;
    te::Clip*  dragClip      = nullptr;
    double     dragOffsetSec = 0.0;   // cursor distance from clip start at drag begin

    // ── Per-track header controls ─────────────────────────────────────────────
    struct TrackControls
    {
        te::AudioTrack*                    track;
        std::unique_ptr<juce::Slider>      volSlider;
        std::unique_ptr<juce::TextButton>  muteBtn;
        std::unique_ptr<juce::TextButton>  soloBtn;
    };
    std::vector<std::unique_ptr<TrackControls>> trackControls;
    void rebuildTrackControls();
    void layoutTrackControls();
    void syncTrackControls();   // keep sliders in step with engine (called from timer)

    // ── Timer / Listener ──────────────────────────────────────────────────────
    void timerCallback         ()             override;
    void activeSongChanged     (int)          override;
    void trackListChanged      ()             override;
    void trackSelectionChanged (te::Track*)   override { repaint(); }
    void clipSelectionChanged  (te::Clip*)    override { repaint(); }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TimelineComponent)
};
