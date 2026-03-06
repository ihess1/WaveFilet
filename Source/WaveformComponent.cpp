#include "WaveformComponent.h"
#include "Theme.h"

WaveformComponent::WaveformComponent (WaveFiletProcessor& proc,
                                      juce::AudioFormatManager& formatManager,
                                      juce::AudioThumbnailCache& thumbnailCache)
    : processor (proc),
      thumbnail (512, formatManager, thumbnailCache)
{
    // Listen for thumbnail data-ready events so we repaint as the waveform
    // progressively loads.
    thumbnail.addChangeListener (this);

    // Listen for WaveFiletProcessor's file-loaded notification so we repaint
    // immediately when the background loading thread finishes, even if the
    // thumbnail has already completed (or not yet started).
    processor.addChangeListener (this);
}

WaveformComponent::~WaveformComponent()
{
    // Always remove listeners before destruction to avoid dangling callbacks.
    processor.removeChangeListener (this);
    thumbnail.removeChangeListener (this);
}

// =============================================================================
// Component
// =============================================================================

void WaveformComponent::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    // Background
    g.fillAll (Theme::waveform_background);

    if (! processor.hasAudio())
    {
        drawNoFileMessage (g);
        return;
    }

    // Waveform channels
    g.setColour (Theme::waveform_fill);
    thumbnail.drawChannels (g,
                            getLocalBounds(),
                            0.0,
                            thumbnail.getTotalLength(),
                            1.0f);

    // Slice markers on top of the waveform
    drawSliceMarkers (g);

    // File-drag hover highlight — drawn last so it sits on top of everything
    if (isDragOver)
    {
        g.setColour (juce::Colours::white.withAlpha (0.15f));
        g.fillRect  (bounds);
        g.setColour (juce::Colours::white.withAlpha (0.8f));
        g.drawRect  (bounds, 2.0f);
    }
}

void WaveformComponent::resized() {}

// =============================================================================
// Paint helpers
// =============================================================================

void WaveformComponent::drawNoFileMessage (juce::Graphics& g) const
{
    g.setColour (juce::Colours::white.withAlpha (0.4f));
    // juce::Font(float) is deprecated in JUCE 8; use FontOptions instead (Issue #5).
    g.setFont (juce::Font (juce::FontOptions{}.withHeight (16.0f)));
    g.drawFittedText ("Drop a .wav file here\nor click \u201cLoad File\u201d",
                      getLocalBounds(), juce::Justification::centred, 2);
}

void WaveformComponent::drawSliceMarkers (juce::Graphics& g) const
{
    const auto& sliceList = processor.getSlices();
    const int   h         = getHeight();

    for (int i = 0; i < static_cast<int> (sliceList.size()); ++i)
    {
        const int x = normalizedToX (sliceList[i].position);

        // Vertical line spanning the full component height
        g.setColour (Theme::sliceMarker_line);
        g.drawLine  (static_cast<float> (x), 0.0f,
                     static_cast<float> (x), static_cast<float> (h), 1.5f);

        // Circular drag handle at the top of the line
        g.setColour (Theme::sliceMarker_handle);
        g.fillEllipse (static_cast<float> (x - handleRadius),
                       static_cast<float> (handleRadius / 2),
                       static_cast<float> (handleRadius * 2),
                       static_cast<float> (handleRadius * 2));

        // Subtle dark outline to make the handle pop against any waveform colour
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.drawEllipse (static_cast<float> (x - handleRadius),
                       static_cast<float> (handleRadius / 2),
                       static_cast<float> (handleRadius * 2),
                       static_cast<float> (handleRadius * 2), 1.0f);
    }
}

// =============================================================================
// Mouse interaction
// =============================================================================

void WaveformComponent::mouseDown (const juce::MouseEvent& e)
{
    if (! processor.hasAudio())
        return;

    const int hitIndex = getSliceHandleAt (e.getPosition());

    if (e.mods.isRightButtonDown())
    {
        // Right-click on any part of a marker (handle or line body) → delete
        if (hitIndex >= 0)
        {
            processor.removeSlice (hitIndex);
            repaint();
        }
        return;
    }

    if (hitIndex >= 0)
    {
        // Left-click on a marker → begin drag
        draggedSliceIndex = hitIndex;
        return;
    }

    // Ctrl + Left-click away from all markers → add a new slice
    if (e.mods.isCtrlDown())
    {
        processor.addSlice (xToNormalized (e.x));
        repaint();
    }
}

void WaveformComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (draggedSliceIndex < 0)
        return;

    processor.moveSlice (draggedSliceIndex, xToNormalized (e.x));

    // moveSlice() re-sorts slicePositions, so draggedSliceIndex may now point
    // to a different element after the sort.  Re-acquire the correct index by
    // finding the slice closest to the cursor.  This is always unambiguous:
    // we placed the dragged slice at exactly the cursor position, so it will
    // always be the nearest slice unless another slice is literally at the
    // same pixel — an edge case that resolves itself on the next mouse event.
    const double     currentNorm = xToNormalized (e.x);
    const auto&      sliceList   = processor.getSlices();
    double minDist    = std::numeric_limits<double>::max();
    int    nearestIdx = draggedSliceIndex;

    for (int i = 0; i < static_cast<int> (sliceList.size()); ++i)
    {
        const double dist = std::abs (sliceList[i].position - currentNorm);
        if (dist < minDist)
        {
            minDist    = dist;
            nearestIdx = i;
        }
    }

    draggedSliceIndex = nearestIdx;
    repaint();
}

void WaveformComponent::mouseUp (const juce::MouseEvent&)
{
    draggedSliceIndex = -1;
}

// =============================================================================
// Hit-testing  (Issue #4)
// =============================================================================

int WaveformComponent::getSliceHandleAt (juce::Point<int> pos) const
{
    const auto& sliceList = processor.getSlices();
    const int   handleY   = handleRadius;  // centre of the handle circle

    for (int i = 0; i < static_cast<int> (sliceList.size()); ++i)
    {
        const int x  = normalizedToX (sliceList[i].position);
        const int dx = pos.x - x;
        const int dy = pos.y - handleY;

        // Test 1: handle circle at the top of the marker.
        // Catches precise clicks on the small grab target.
        if (dx * dx + dy * dy <= hitTolerance * hitTolerance)
            return i;

        // Test 2: horizontal proximity to the vertical line body.
        // Allows right-clicking or dragging from anywhere along the full-height
        // line, making thin markers much easier to interact with (Issue #4).
        if (std::abs (dx) <= hitTolerance
            && pos.y >= 0
            && pos.y <= getHeight())
        {
            return i;
        }
    }

    return -1;
}

// =============================================================================
// Coordinate helpers
// =============================================================================

double WaveformComponent::xToNormalized (int x) const
{
    if (getWidth() <= 0)
        return 0.0;

    return juce::jlimit (0.0, 1.0, static_cast<double> (x) / getWidth());
}

int WaveformComponent::normalizedToX (double norm) const
{
    return static_cast<int> (norm * getWidth());
}

// =============================================================================
// FileDragAndDropTarget
// =============================================================================

bool WaveformComponent::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (f.endsWithIgnoreCase (".wav"))
            return true;

    return false;
}

void WaveformComponent::fileDragEnter (const juce::StringArray&, int, int)
{
    isDragOver = true;
    repaint();
}

void WaveformComponent::fileDragExit (const juce::StringArray&)
{
    isDragOver = false;
    repaint();
}

void WaveformComponent::filesDropped (const juce::StringArray& files, int, int)
{
    isDragOver = false;

    for (const auto& path : files)
    {
        const juce::File f (path);
        if (f.hasFileExtension (".wav"))
        {
            setSource (f);
            break;
        }
    }

    repaint();
}

// =============================================================================
// Source management
// =============================================================================

void WaveformComponent::setSource (const juce::File& file)
{
    // Start background audio decoding on the processor's FileLoadingThread.
    processor.loadFile (file);

    // Start async thumbnail generation independently.  The thumbnail renders
    // in JUCE's internal background thread; each partial update fires a
    // changeListenerCallback which triggers a repaint.
    thumbnail.setSource (new juce::FileInputSource (file));

    repaint();
}

void WaveformComponent::restoreFromProcessor()
{
    // After session restore the processor has decoded audio (loaded via
    // setStateInformation → loadFile) but no thumbnail was ever set because
    // setSource() is only reachable through UI gestures.  Rebuild it now so
    // the waveform is visible immediately on re-open.
    if (processor.hasAudio())
        thumbnail.setSource (new juce::FileInputSource (processor.getLoadedFile()));
}

// =============================================================================
// ChangeListener
// =============================================================================

void WaveformComponent::changeListenerCallback (juce::ChangeBroadcaster* /*source*/)
{
    // Called by both the AudioThumbnail (progressive waveform load) and by
    // WaveFiletProcessor (background file decode complete).  In both cases the
    // right response is to repaint so the latest state is reflected on screen.
    repaint();
}
