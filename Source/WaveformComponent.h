#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"

/**
 * Displays the loaded waveform and manages slice marker interaction.
 *
 * Slice markers are normalized positions [0.0, 1.0] stored in WaveFiletProcessor.
 *
 * Mouse interaction:
 *   Ctrl + Left-click  (away from a marker)  →  add slice
 *   Left-click + drag  (on a marker handle)  →  move slice
 *   Right-click        (on a marker handle)  →  remove slice
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

    //==============================================================================
    // Component
    void paint (juce::Graphics&) override;
    void resized() override;

    // Mouse
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp   (const juce::MouseEvent&) override;

    //==============================================================================
    // FileDragAndDropTarget
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;
    void fileDragEnter (const juce::StringArray&, int, int) override;
    void fileDragExit  (const juce::StringArray&) override;

    //==============================================================================
    // ChangeListener (AudioThumbnail notifies us when it finishes loading)
    void changeListenerCallback (juce::ChangeBroadcaster* source) override;

    //==============================================================================
    void setSource (const juce::File& file);

    // Rebuilds the AudioThumbnail from the processor's already-loaded file.
    // Call this after session restore, when the processor has audio but
    // setSource() was never invoked through the UI path.
    void restoreFromProcessor();

private:
    //==============================================================================
    // Returns the index of the slice marker whose handle contains `pos`, or -1.
    int getSliceHandleAt (juce::Point<int> pos) const;

    // Coordinate conversion helpers
    double xToNormalized (int x) const;
    int    normalizedToX (double norm) const;

    void drawNoFileMessage (juce::Graphics& g) const;
    void drawSliceMarkers  (juce::Graphics& g) const;

    //==============================================================================
    WaveFiletProcessor& processor;
    juce::AudioThumbnail thumbnail;

    bool isDragOver = false;    // file drag hover highlight

    int draggedSliceIndex = -1; // index being dragged, or -1

    static constexpr int handleRadius  = 6;  // px radius of the grab handle circle
    static constexpr int hitTolerance  = 8;  // px hit-test radius

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveformComponent)
};
