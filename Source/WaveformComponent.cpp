#include "WaveformComponent.h"
#include "Theme.h"

WaveformComponent::WaveformComponent (WaveFiletProcessor& proc,
                                      juce::AudioFormatManager& formatManager,
                                      juce::AudioThumbnailCache& thumbnailCache)
    : processor (proc),
      thumbnail (512, formatManager, thumbnailCache)
{
    thumbnail.addChangeListener (this);
    processor.addChangeListener (this);
}

WaveformComponent::~WaveformComponent()
{
    processor.removeChangeListener (this);
    thumbnail.removeChangeListener (this);
}

//==============================================================================
void WaveformComponent::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    g.fillAll (Theme::waveformBg);

    if (! processor.hasAudio())
    {
        drawNoFileMessage (g);
        if (isDragOver)
        {
            g.setColour (Theme::dragOverFill);
            g.fillRect (bounds);
            g.setColour (Theme::dragOverBorder);
            g.drawRect (bounds, 2.0f);
        }
        return;
    }

    // Waveform
    g.setColour (Theme::waveform);
    thumbnail.drawChannels (g,
                            getLocalBounds(),
                            0.0,
                            thumbnail.getTotalLength(),
                            1.0f);

    drawSliceMarkers (g);

    if (isDragOver)
    {
        g.setColour (Theme::dragOverFill);
        g.fillRect (bounds);
        g.setColour (Theme::dragOverBorder);
        g.drawRect (bounds, 2.0f);
    }
}

void WaveformComponent::resized() {}

//==============================================================================
void WaveformComponent::drawNoFileMessage (juce::Graphics& g) const
{
    g.setColour (Theme::noFileText);
    g.setFont (juce::Font (juce::FontOptions{}.withHeight (16.0f)));
    g.drawFittedText ("Drop a .wav file here\nor click \u201cLoad File\u201d",
                      getLocalBounds(), juce::Justification::centred, 2);
}

void WaveformComponent::drawSliceMarkers (juce::Graphics& g) const
{
    const auto& slices = processor.getSlices();
    const int   h      = getHeight();

    for (int i = 0; i < static_cast<int> (slices.size()); ++i)
    {
        const auto& s  = slices[i];
        const int   x  = normalizedToX (s.position);
        const int   mx = normalizedToX (s.position + s.startOffset);

        // Offset marker (shown only if offset != 0)
        if (s.startOffset > 0.001)
        {
            g.setColour (Theme::offsetMarker.withAlpha (0.6f));
            g.drawLine (static_cast<float> (mx), 0.0f,
                        static_cast<float> (mx), static_cast<float> (h), 1.0f);
        }

        // Vertical slice line
        const juce::Colour lineCol = s.customFile.existsAsFile()
                                         ? Theme::customSliceFlag
                                         : Theme::sliceLine;
        g.setColour (lineCol);
        g.drawLine (static_cast<float> (x), 0.0f,
                    static_cast<float> (x), static_cast<float> (h), 1.5f);

        // Drag handle
        g.setColour (Theme::sliceHandle);
        g.fillEllipse (static_cast<float> (x - handleRadius),
                       static_cast<float> (handleRadius / 2),
                       static_cast<float> (handleRadius * 2),
                       static_cast<float> (handleRadius * 2));
        g.setColour (Theme::sliceHandleRing);
        g.drawEllipse (static_cast<float> (x - handleRadius),
                       static_cast<float> (handleRadius / 2),
                       static_cast<float> (handleRadius * 2),
                       static_cast<float> (handleRadius * 2), 1.0f);

        // MIDI note label below handle
        g.setColour (Theme::sliceHandle.withAlpha (0.9f));
        g.setFont (juce::Font (juce::FontOptions{}.withHeight (10.0f)));
        const int note = WaveFiletProcessor::kBaseMidiNote + s.id;
        g.drawText (juce::String (note),
                    x - 14, handleRadius * 3, 28, 12,
                    juce::Justification::centred, false);
    }
}

//==============================================================================
void WaveformComponent::mouseDown (const juce::MouseEvent& e)
{
    if (! processor.hasAudio())
        return;

    const int hitIndex = getSliceHandleAt (e.getPosition());
    didDragMove = false;

    if (e.mods.isRightButtonDown())
    {
        if (hitIndex >= 0)
            showSliceContextMenu (hitIndex);
        return;
    }

    if (hitIndex >= 0)
    {
        // Will drag or trigger depending on whether mouse moves
        draggedSliceIndex = hitIndex;
        return;
    }

    // Ctrl+click: add a new slice
    if (e.mods.isCtrlDown())
    {
        processor.addSlice (xToNormalized (e.x));
        repaint();
        return;
    }

    // Plain left-click on empty space: trigger the slice that owns this region
    const double clickNorm = xToNormalized (e.x);
    const auto& slices = processor.getSlices();
    int triggerIdx = -1;
    for (int i = static_cast<int> (slices.size()) - 1; i >= 0; --i)
    {
        if (slices[i].position <= clickNorm)
        {
            triggerIdx = i;
            break;
        }
    }
    if (triggerIdx >= 0)
        processor.triggerSlice (triggerIdx);
}

void WaveformComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (draggedSliceIndex < 0)
        return;

    didDragMove = true;
    processor.moveSlice (draggedSliceIndex, xToNormalized (e.x));

    // Re-acquire index: moveSlice re-sorts, so find the closest position
    const double currentNorm = xToNormalized (e.x);
    const auto&  slices      = processor.getSlices();
    double minDist   = std::numeric_limits<double>::max();
    int    nearest   = draggedSliceIndex;
    for (int i = 0; i < static_cast<int> (slices.size()); ++i)
    {
        const double dist = std::abs (slices[i].position - currentNorm);
        if (dist < minDist) { minDist = dist; nearest = i; }
    }
    draggedSliceIndex = nearest;
    repaint();
}

void WaveformComponent::mouseUp (const juce::MouseEvent&)
{
    // If we pressed on a marker but didn't drag, treat it as a trigger
    if (draggedSliceIndex >= 0 && ! didDragMove)
        processor.triggerSlice (draggedSliceIndex);

    draggedSliceIndex = -1;
    didDragMove       = false;
}

void WaveformComponent::mouseWheel (const juce::MouseEvent& e,
                                    const juce::MouseWheelDetails& wheel)
{
    if (! processor.hasAudio())
        return;

    const int hitIndex = getSliceHandleAt (e.getPosition());
    if (hitIndex < 0)
        return;

    // Scroll adjusts the within-slice start offset
    const double delta = static_cast<double> (wheel.deltaY) * 0.02;
    processor.adjustSliceStartOffset (hitIndex, delta);
    repaint();
}

//==============================================================================
// Hit-test: covers handle circle AND full vertical line body
int WaveformComponent::getSliceHandleAt (juce::Point<int> pos) const
{
    const auto& slices = processor.getSlices();

    for (int i = 0; i < static_cast<int> (slices.size()); ++i)
    {
        const int x = normalizedToX (slices[i].position);

        // Handle circle
        const int handleCentreY = handleRadius;
        const int dx = pos.x - x;
        const int dy = pos.y - handleCentreY;
        if (dx * dx + dy * dy <= hitTolerance * hitTolerance)
            return i;

        // Full vertical line body
        if (std::abs (dx) <= hitTolerance)
            return i;
    }
    return -1;
}

double WaveformComponent::xToNormalized (int x) const
{
    if (getWidth() <= 0) return 0.0;
    return juce::jlimit (0.0, 1.0, static_cast<double> (x) / getWidth());
}

int WaveformComponent::normalizedToX (double norm) const
{
    return static_cast<int> (norm * getWidth());
}

//==============================================================================
void WaveformComponent::showSliceContextMenu (int sliceIndex)
{
    juce::PopupMenu menu;
    menu.addItem (1, "Delete slice");
    menu.addItem (2, "Load custom audio...");

    const auto& slices = processor.getSlices();
    if (sliceIndex < static_cast<int> (slices.size())
        && slices[sliceIndex].customFile.existsAsFile())
    {
        menu.addItem (3, "Clear custom audio");
    }

    if (sliceIndex < static_cast<int> (slices.size())
        && slices[sliceIndex].startOffset > 0.001)
    {
        menu.addItem (4, "Reset start offset");
    }

    menu.showMenuAsync (
        juce::PopupMenu::Options().withTargetComponent (this),
        [this, sliceIndex] (int result)
        {
            if (result == 1)
            {
                processor.removeSlice (sliceIndex);
                repaint();
            }
            else if (result == 2)
            {
                auto chooser = std::make_shared<juce::FileChooser> (
                    "Select audio file for slice",
                    juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                    "*.wav;*.aif;*.aiff");
                chooser->launchAsync (
                    juce::FileBrowserComponent::openMode
                  | juce::FileBrowserComponent::canSelectFiles,
                    [this, sliceIndex, chooser] (const juce::FileChooser& fc)
                    {
                        const auto f = fc.getResult();
                        if (f.existsAsFile())
                        {
                            processor.setSliceCustomFile (sliceIndex, f);
                            repaint();
                        }
                    });
            }
            else if (result == 3)
            {
                processor.clearSliceCustomFile (sliceIndex);
                repaint();
            }
            else if (result == 4)
            {
                processor.adjustSliceStartOffset (sliceIndex,
                    -processor.getSlices()[sliceIndex].startOffset);
                repaint();
            }
        });
}

//==============================================================================
bool WaveformComponent::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (f.endsWithIgnoreCase (".wav")  ||
            f.endsWithIgnoreCase (".aif")  ||
            f.endsWithIgnoreCase (".aiff"))
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
        if (f.hasFileExtension (".wav") ||
            f.hasFileExtension (".aif") ||
            f.hasFileExtension (".aiff"))
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

void WaveformComponent::restoreFromProcessor()
{
    if (processor.hasAudio())
        thumbnail.setSource (new juce::FileInputSource (processor.getLoadedFile()));
}

void WaveformComponent::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source == &processor)
    {
        // New file finished loading — restore thumbnail if it's not already set
        if (processor.hasAudio() && thumbnail.getTotalLength() == 0.0)
            thumbnail.setSource (new juce::FileInputSource (processor.getLoadedFile()));
    }
    repaint();
}
