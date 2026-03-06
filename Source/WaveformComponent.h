#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"

/**
 * Displays the loaded waveform and manages all slice marker interactions.
 *
 * Slice positions are normalised doubles [0.0, 1.0] stored in WaveFiletProcessor.
 *
 * Mouse interaction:
 *   Ctrl + Left-click  (away from a marker)        -> add slice
 *   Left-click         (away from a marker, no Ctrl)-> trigger nearest slice before cursor
 *   Left-click + drag  (on any part of a marker)   -> move slice
 *   Left-click release (on marker, no drag)         -> trigger that slice
 *   Right-click        (on a marker)                -> context menu (delete / load custom wav / clear offset)
 *   Mousewheel         (on a marker)                -> adjust slice start offset
 *
 * Hit-test covers both the drag-handle circle and the full vertical line body
 * (within hitTolerance px horizontally).
 *
 * ChangeListener sources:
 *   AudioThumbnail  — repaints when waveform data arrives
 *   WaveFiletProcessor — repaints when a new file finishes loading
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

    //==========================================================================
    // Component
    void paint   (juce::Graphics&) override;
    void resized () override;

    void mouseDown  (const juce::MouseEvent&) override;
    void mouseDrag  (const juce::MouseEvent&) override;
    void mouseUp    (const juce::MouseEvent&) override;
    void mouseWheel (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    //==========================================================================
    // FileDragAndDropTarget
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped  (const juce::StringArray& files, int x, int y) override;
    void fileDragEnter (const juce::StringArray&, int, int) override;
    void fileDragExit  (const juce::StringArray&) override;

    //==========================================================================
    // ChangeListener
    void changeListenerCallback (juce::ChangeBroadcaster* source) override;

    //==========================================================================
    void setSource (const juce::File& file);
    void restoreFromProcessor();

private:
    //==========================================================================
    // Returns sorted index of the slice whose handle/line contains pos, or -1.
    int getSliceHandleAt (juce::Point<int> pos) const;

    double xToNormalized (int x) const;
    int    normalizedToX (double norm) const;

    void drawNoFileMessage (juce::Graphics& g) const;
    void drawSliceMarkers  (juce::Graphics& g) const;

    void showSliceContextMenu (int sliceIndex);

    //==========================================================================
    WaveFiletProcessor& processor;
    juce::AudioThumbnail thumbnail;

    bool isDragOver       = false;
    int  draggedSliceIndex = -1;
    bool didDragMove       = false;   // distinguishes click-vs-drag

    static constexpr int handleRadius = 6;
    static constexpr int hitTolerance = 8;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveformComponent)
};
