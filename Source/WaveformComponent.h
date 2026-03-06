#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"

/**
 * Displays the loaded waveform and manages slice marker interaction.
 *
 * Slice markers are WaveFiletProcessor::Slice objects whose `position` field
 * is a normalised value in [0.0, 1.0].  The `id` field carries a stable MIDI-
 * note identity that is unaffected by drag-reordering (see PluginProcessor.h).
 *
 * Mouse interaction contract
 * --------------------------
 *   Ctrl + Left-click  (not near a marker)  →  add slice
 *   Left-click + drag  (on a marker)        →  move slice
 *   Right-click        (on a marker)        →  remove slice
 *
 * Hit-test model  (Issue #4)
 * --------------------------
 * A click is "on a marker" if the cursor is within hitTolerance pixels of the
 * marker's vertical line anywhere along its full height, OR within a circle of
 * radius hitTolerance centred on the drag handle at the top.  This makes it
 * easy to right-click anywhere along a thin line to delete it, not just at the
 * tiny handle at the top.
 */
class WaveformComponent final : public juce::Component,
                                public juce::FileDragAndDropTarget,
                                public juce::ChangeListener
{
public:
    WaveformComponent (WaveFiletProcessor& processor,
                       juce::AudioFormatManager& formatManager,
                       juce::AudioThumbnailCache& thumbnailCache);

    ~WaveformComponent() override;

    // =========================================================================
    // Component overrides

    void paint   (juce::Graphics&) override;
    void resized () override;

    // Mouse
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp   (const juce::MouseEvent&) override;

    // =========================================================================
    // FileDragAndDropTarget overrides

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped  (const juce::StringArray& files, int x, int y) override;
    void fileDragEnter (const juce::StringArray&, int, int) override;
    void fileDragExit  (const juce::StringArray&) override;

    // =========================================================================
    // ChangeListener override
    // Receives notifications from both the AudioThumbnail (waveform data ready)
    // and from WaveFiletProcessor (new file loaded via background thread).

    void changeListenerCallback (juce::ChangeBroadcaster* source) override;

    // =========================================================================
    // Public API

    /** Starts loading `file` into both the processor and the AudioThumbnail.
     *  The processor load happens on the background FileLoadingThread; the
     *  thumbnail loads asynchronously via JUCE's built-in thumbnail thread. */
    void setSource (const juce::File& file);

    /** Rebuilds the AudioThumbnail from the processor's already-loaded file.
     *  Call this after session restore, when the processor has audio but
     *  setSource() was never invoked through the UI path. */
    void restoreFromProcessor();

private:
    // =========================================================================
    // Hit-testing

    /**
     * Returns the sort-index of the slice marker that contains `pos`, or -1.
     *
     * A marker is "hit" if `pos` is:
     *   (a) within a circle of radius hitTolerance around the drag handle, OR
     *   (b) within hitTolerance pixels horizontally of the marker's vertical
     *       line body AND within the component's vertical bounds.
     *
     * The two-part test (Issue #4) allows right-clicking anywhere along a
     * thin line to delete it, not just on the small handle circle.  It also
     * gives more forgiving drag-start affordance on the line body.
     */
    int getSliceHandleAt (juce::Point<int> pos) const;

    // =========================================================================
    // Coordinate helpers

    /** Maps a pixel x-coordinate to a normalised position in [0.0, 1.0]. */
    double xToNormalized  (int x)      const;

    /** Maps a normalised position in [0.0, 1.0] to a pixel x-coordinate. */
    int    normalizedToX  (double norm) const;

    // =========================================================================
    // Paint helpers

    void drawNoFileMessage (juce::Graphics& g) const;
    void drawSliceMarkers  (juce::Graphics& g) const;

    // =========================================================================
    // Members

    WaveFiletProcessor& processor;
    juce::AudioThumbnail thumbnail;

    bool isDragOver       = false;  ///< true while a file is being dragged over this component
    int  draggedSliceIndex = -1;    ///< sort-index of the slice being dragged, or -1

    /** Pixel radius of the circular drag handle drawn at the top of each marker. */
    static constexpr int handleRadius = 6;

    /** Pixel radius of the hit-test region for both handle circle and line body. */
    static constexpr int hitTolerance = 8;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveformComponent)
};
