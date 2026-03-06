#include "PluginProcessor.h"
#include "PluginEditor.h"

WaveFiletProcessor::WaveFiletProcessor()
    : AudioProcessor (BusesProperties()
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    formatManager.registerBasicFormats();
}

WaveFiletProcessor::~WaveFiletProcessor() {}

//==============================================================================
const juce::String WaveFiletProcessor::getName() const { return JucePlugin_Name; }
bool WaveFiletProcessor::acceptsMidi() const            { return true; }
bool WaveFiletProcessor::producesMidi() const           { return false; }
bool WaveFiletProcessor::isMidiEffect() const           { return false; }
double WaveFiletProcessor::getTailLengthSeconds() const { return 0.0; }

int WaveFiletProcessor::getNumPrograms()                              { return 1; }
int WaveFiletProcessor::getCurrentProgram()                           { return 0; }
void WaveFiletProcessor::setCurrentProgram (int)                      {}
const juce::String WaveFiletProcessor::getProgramName (int)           { return {}; }
void WaveFiletProcessor::changeProgramName (int, const juce::String&) {}

//==============================================================================
void WaveFiletProcessor::prepareToPlay (double, int) {}
void WaveFiletProcessor::releaseResources()           {}

bool WaveFiletProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
        && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    return true;
}

void WaveFiletProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                       juce::MidiBuffer&)
{
    // Phase 1: no audio output yet — clear the buffer to silence
    buffer.clear();
}

//==============================================================================
bool WaveFiletProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor* WaveFiletProcessor::createEditor()
{
    return new WaveFiletEditor (*this);
}

//==============================================================================
bool WaveFiletProcessor::hasAudio() const
{
    return fileLength > 0;
}

void WaveFiletProcessor::loadFile (const juce::File& file)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
    if (reader == nullptr)
        return;

    // Slice positions from any previously loaded file are not meaningful
    // relative to a new file's timeline, so reset them on every load.
    slicePositions.clear();

    const auto maxSeconds = 600.0; // 10-minute cap
    const auto maxSamples = static_cast<int> (
        juce::jmin (reader->lengthInSamples,
                    static_cast<juce::int64> (maxSeconds * reader->sampleRate)));

    // Extra guard samples beyond fileLength provide headroom for future
    // cubic/sinc interpolation during pitched or time-stretched playback.
    // fileLength intentionally does NOT include these guard samples — they
    // are never "valid" audio and should not be iterated over by playback code.
    static constexpr int kInterpolationGuardSamples = 4;

    audioBuffer.setSize (juce::jmin (2, static_cast<int> (reader->numChannels)),
                         maxSamples + kInterpolationGuardSamples);
    reader->read (&audioBuffer, 0, maxSamples + kInterpolationGuardSamples, 0, true, true);

    fileSampleRate = reader->sampleRate;
    fileLength     = maxSamples;
    loadedFile     = file;
}

//==============================================================================
void WaveFiletProcessor::addSlice (double pos)
{
    pos = juce::jlimit (0.0, 1.0, pos);
    slicePositions.push_back (pos);
    std::sort (slicePositions.begin(), slicePositions.end());
}

void WaveFiletProcessor::moveSlice (int index, double pos)
{
    if (index < 0 || index >= static_cast<int> (slicePositions.size()))
        return;

    slicePositions[index] = juce::jlimit (0.0, 1.0, pos);
    std::sort (slicePositions.begin(), slicePositions.end());
}

void WaveFiletProcessor::removeSlice (int index)
{
    if (index < 0 || index >= static_cast<int> (slicePositions.size()))
        return;

    slicePositions.erase (slicePositions.begin() + index);
}

//==============================================================================
void WaveFiletProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree state ("WaveFiletState");
    state.setProperty ("filePath", loadedFile.getFullPathName(), nullptr);

    for (double pos : slicePositions)
    {
        juce::ValueTree slice ("Slice");
        slice.setProperty ("pos", pos, nullptr);
        state.appendChild (slice, nullptr);
    }

    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void WaveFiletProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml == nullptr || ! xml->hasTagName ("WaveFiletState"))
        return;

    juce::ValueTree state = juce::ValueTree::fromXml (*xml);

    const juce::String path = state.getProperty ("filePath", "").toString();
    if (path.isNotEmpty())
    {
        const juce::File f (path);
        if (f.existsAsFile())
            loadFile (f);
    }

    slicePositions.clear();
    for (int i = 0; i < state.getNumChildren(); ++i)
    {
        const auto child = state.getChild (i);
        if (child.hasType ("Slice"))
            slicePositions.push_back (static_cast<double> (child.getProperty ("pos", 0.0)));
    }
    // Ensure sorted (should already be, but defensive)
    std::sort (slicePositions.begin(), slicePositions.end());
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new WaveFiletProcessor();
}
