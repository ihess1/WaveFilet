#include "WaveformComponent.h"

WaveformComponent::WaveformComponent (WaveFiletProcessor& proc,
                                      juce::AudioFormatManager& formatManager,
                                      juce::AudioThumbnailCache& thumbnailCache)
    : processor (proc),
      thumbnail (512, formatManager, thumbnailCache)
{
    thumbnail.addChangeListener (this);
}

WaveformComponent::~WaveformComponent()
{
    thumbnail.removeChangeListener (this);
}

//==============================================================================
void WaveformComponent::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    // Background
    g.fillAll (juce::Colour (0xff1a1a2e));

    if (! processor.hasAudio())
    {
        drawNoFileMessage (g);
        return;
    }

    // Waveform
    g.setColour (juce::Colour (0xff4ecdc4));
    thumbnail.drawChannels (g,
                            getLocalBounds(),
                            0.0,
                            thumbnail.getTotalLength(),
                            1.0f);

    // Slice markers
    drawSliceMarkers (g);

    // Drag-over highlight
    if (isDragOver)
    {
        g.setColour (juce::Colours::white.withAlpha (0.15f));
        g.fillRect (bounds);
        g.setColour (juce::Colours::white.withAlpha (0.8f));
        g.drawRect (bounds, 2.0f);
    }
}

void WaveformComponent::resized() {}

//==============================================================================
void WaveformComponent::drawNoFileMessage (juce::Graphics& g) const
{
    g.setColour (juce::Colours::white.withAlpha (0.4f));
    g.setFont (16.0f);
    g.drawFittedText ("Drop a .wav file here\nor click \u201cLoad File\u201d",
                      getLocalBounds(), juce::Justification::centred, 2);
}

void WaveformComponent::drawSliceMarkers (juce::Graphics& g) const
{
    const auto& slices = processor.getSlicePositions();
    const int h = getHeight();

    for (int i = 0; i < static_cast<int> (slices.size()); ++i)
    {
        const int x = normalizedToX (slices[i]);

        // Vertical line
        g.setColour (juce::Colour (0xffff6b6b));
        g.drawLine (static_cast<float> (x), 0.0f, static_cast<float> (x),
                    static_cast<float> (h), 1.5f);

        // Drag handle at top
        g.setColour (juce::Colour (0xffffd166));
        g.fillEllipse (static_cast<float> (x - handleRadius),
                       static_cast<float> (handleRadius / 2),
                       static_cast<float> (handleRadius * 2),
                       static_cast<float> (handleRadius * 2));
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.drawEllipse (static_cast<float> (x - handleRadius),
                       static_cast<float> (handleRadius / 2),
                       static_cast<float> (handleRadius * 2),
                       static_cast<float> (handleRadius * 2), 1.0f);
    }
}

//==============================================================================
void WaveformComponent::mouseDown (const juce::MouseEvent& e)
{
    if (! processor.hasAudio())
        return;

    const int hitIndex = getSliceHandleAt (e.getPosition());

    if (e.mods.isRightButtonDown())
    {
        // Right-click: delete marker if we hit one
        if (hitIndex >= 0)
        {
            processor.removeSlice (hitIndex);
            repaint();
        }
        return;
    }

    if (hitIndex >= 0)
    {
        // Begin dragging an existing marker
        draggedSliceIndex = hitIndex;
        dragStartNorm     = processor.getSlicePositions()[hitIndex];
        return;
    }

    // Ctrl+click: add a new slice
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
    // After sort, the index may have changed — find it again by proximity
    // (simple approach: keep tracking by searching nearest to current x)
    const double currentNorm = xToNormalized (e.x);
    const auto& slices = processor.getSlicePositions();
    double minDist = std::numeric_limits<double>::max();
    int nearestIndex = draggedSliceIndex;
    for (int i = 0; i < static_cast<int> (slices.size()); ++i)
    {
        const double dist = std::abs (slices[i] - currentNorm);
        if (dist < minDist)
        {
            minDist = dist;
            nearestIndex = i;
        }
    }
    draggedSliceIndex = nearestIndex;
    repaint();
}

void WaveformComponent::mouseUp (const juce::MouseEvent&)
{
    draggedSliceIndex = -1;
}

//==============================================================================
int WaveformComponent::getSliceHandleAt (juce::Point<int> pos) const
{
    const auto& slices = processor.getSlicePositions();
    const int handleY  = handleRadius; // handle centre Y

    for (int i = 0; i < static_cast<int> (slices.size()); ++i)
    {
        const int x    = normalizedToX (slices[i]);
        const int dx   = pos.x - x;
        const int dy   = pos.y - handleY;
        if (dx * dx + dy * dy <= hitTolerance * hitTolerance)
            return i;
    }
    return -1;
}

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

//==============================================================================
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

//==============================================================================
void WaveformComponent::setSource (const juce::File& file)
{
    processor.loadFile (file);
    thumbnail.setSource (new juce::FileInputSource (file));
    repaint();
}

void WaveformComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    repaint();
}
