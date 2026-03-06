#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
// FileLoadingThread — decodes audio on a background thread, posts result to
// the message thread via callAsync so the audio thread is never blocked.
//==============================================================================
class FileLoadingThread : public juce::Thread
{
public:
    explicit FileLoadingThread (WaveFiletProcessor& p)
        : juce::Thread ("WaveFilet-Loader"), owner (p) {}

    void load (const juce::File& f)
    {
        pendingFile = f;
        startThread (juce::Thread::Priority::background);
    }

    void run() override
    {
        const juce::File file = pendingFile;

        juce::AudioFormatManager fmt;
        fmt.registerBasicFormats();

        std::unique_ptr<juce::AudioFormatReader> reader (fmt.createReaderFor (file));
        if (reader == nullptr || threadShouldExit())
            return;

        static constexpr double kMaxSeconds = 600.0;
        const juce::int64 maxSamples = juce::jmin (
            reader->lengthInSamples,
            static_cast<juce::int64> (kMaxSeconds * reader->sampleRate));

        const int numChannels = juce::jmin (2, static_cast<int> (reader->numChannels));
        const int totalToRead = static_cast<int> (maxSamples)
                                + WaveFiletProcessor::kInterpolationGuardSamples;

        auto buffer = std::make_unique<juce::AudioBuffer<float>> (numChannels, totalToRead);
        reader->read (buffer.get(), 0, totalToRead, 0, true, true);

        if (threadShouldExit())
            return;

        const double      sr  = reader->sampleRate;
        const juce::int64 len = maxSamples;

        juce::WeakReference<WaveFiletProcessor> weakOwner (&owner);
        juce::MessageManager::callAsync (
            [buf = std::move (buffer), sr, len, file, weakOwner]() mutable
            {
                if (auto* p = weakOwner.get())
                    p->onFileLoaded (std::move (buf), sr, len, file);
            });
    }

private:
    WaveFiletProcessor& owner;
    juce::File          pendingFile;
};

//==============================================================================
// WaveFiletProcessor
//==============================================================================
WaveFiletProcessor::WaveFiletProcessor()
    : AudioProcessor (BusesProperties()
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    formatManager.registerBasicFormats();
    loadingThread = std::make_unique<FileLoadingThread> (*this);
}

WaveFiletProcessor::~WaveFiletProcessor()
{
    loadingThread->stopThread (2000);
}

//==============================================================================
const juce::String WaveFiletProcessor::getName() const { return JucePlugin_Name; }
bool WaveFiletProcessor::acceptsMidi() const            { return true;  }
bool WaveFiletProcessor::producesMidi() const           { return false; }
bool WaveFiletProcessor::isMidiEffect() const           { return false; }
double WaveFiletProcessor::getTailLengthSeconds() const { return 2.0;   }

int WaveFiletProcessor::getNumPrograms()                              { return 1; }
int WaveFiletProcessor::getCurrentProgram()                           { return 0; }
void WaveFiletProcessor::setCurrentProgram (int)                      {}
const juce::String WaveFiletProcessor::getProgramName (int)           { return {}; }
void WaveFiletProcessor::changeProgramName (int, const juce::String&) {}

//==============================================================================
void WaveFiletProcessor::prepareToPlay (double sampleRate, int)
{
    playbackSampleRate = sampleRate;
    stopAllVoices();
}

void WaveFiletProcessor::releaseResources()
{
    stopAllVoices();
}

bool WaveFiletProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono()
        || out == juce::AudioChannelSet::stereo();
}

//==============================================================================
void WaveFiletProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                        juce::MidiBuffer& midiMessages)
{
    buffer.clear();

    if (! audioLoaded.load (std::memory_order_relaxed))
        return;

    const SliceSnapshot* snap = audioThreadSnapshot.load (std::memory_order_acquire);

    // Process MIDI sample-accurately
    int samplePos = 0;
    for (const auto meta : midiMessages)
    {
        const int eventPos = meta.samplePosition;
        if (eventPos > samplePos)
        {
            renderVoices (buffer, samplePos, eventPos - samplePos);
            samplePos = eventPos;
        }
        const auto msg = meta.getMessage();
        if (snap != nullptr)
        {
            if (msg.isNoteOn() && msg.getVelocity() > 0)
                noteOn (*snap, msg.getNoteNumber());
            else if (msg.isNoteOff() || (msg.isNoteOn() && msg.getVelocity() == 0))
                noteOff (msg.getNoteNumber());
        }
    }

    if (samplePos < buffer.getNumSamples())
        renderVoices (buffer, samplePos, buffer.getNumSamples() - samplePos);
}

void WaveFiletProcessor::noteOn (const SliceSnapshot& snap, int noteNumber)
{
    // Find the snapshot slice whose MIDI note matches
    int sliceIdx = -1;
    for (int i = 0; i < snap.count; ++i)
    {
        if (kBaseMidiNote + snap.ids[i] == noteNumber)
        {
            sliceIdx = i;
            break;
        }
    }
    if (sliceIdx < 0)
        return;

    const int slot = activeAudioSlot.load (std::memory_order_acquire);
    const AudioData& ad = audioSlots[slot];
    if (ad.buffer == nullptr || ad.length == 0)
        return;

    // Compute start and end within the main audio buffer
    const double startNorm = juce::jlimit (0.0, 1.0,
                                snap.positions[sliceIdx] + snap.offsets[sliceIdx]);
    const double endNorm   = (sliceIdx + 1 < snap.count)
                                 ? snap.positions[sliceIdx + 1]
                                 : 1.0;

    const juce::int64 startSample = static_cast<juce::int64> (startNorm * ad.length);
    const juce::int64 endSample   = static_cast<juce::int64> (
                                        juce::jlimit (0.0, 1.0, endNorm) * ad.length);
    if (startSample >= endSample)
        return;

    // Check for custom audio override (Phase 3)
    juce::AudioBuffer<float>* customBuf = nullptr;
    juce::int64               customLen = 0;
    if (sliceIdx < kMaxSlices)
    {
        customBuf = customSlots[sliceIdx].ptr.load (std::memory_order_acquire);
        customLen = customSlots[sliceIdx].len.load (std::memory_order_acquire);
    }

    // Find a free voice, or steal voice 0
    int voiceIdx = 0;
    for (int i = 0; i < kMaxVoices; ++i)
    {
        if (! voices[i].active) { voiceIdx = i; break; }
    }

    Voice& v     = voices[voiceIdx];
    v.active     = true;
    v.noteNumber = noteNumber;
    v.samplePos  = (customBuf != nullptr) ? 0 : startSample;
    v.endSample  = endSample;
    v.customBuf  = customBuf;
    v.customLen  = customLen;
    v.gain       = 1.0f;
}

void WaveFiletProcessor::noteOff (int noteNumber)
{
    for (auto& v : voices)
        if (v.active && v.noteNumber == noteNumber)
            v.active = false;
}

void WaveFiletProcessor::renderVoices (juce::AudioBuffer<float>& output,
                                        int startSample, int numSamples)
{
    if (numSamples <= 0)
        return;

    const int slot = activeAudioSlot.load (std::memory_order_acquire);
    const AudioData& ad = audioSlots[slot];
    if (ad.buffer == nullptr)
        return;

    const int outChannels = output.getNumChannels();

    for (auto& v : voices)
    {
        if (! v.active)
            continue;

        const juce::AudioBuffer<float>* src = (v.customBuf != nullptr)
                                                  ? v.customBuf
                                                  : ad.buffer.get();
        const juce::int64 readEnd = (v.customBuf != nullptr) ? v.customLen : v.endSample;
        const int         srcChannels = src->getNumChannels();

        int remaining = numSamples;
        int outOffset = startSample;

        while (remaining > 0 && v.active)
        {
            const juce::int64 framesLeft = readEnd - v.samplePos;
            if (framesLeft <= 0) { v.active = false; break; }

            const int chunk = static_cast<int> (
                juce::jmin (static_cast<juce::int64> (remaining), framesLeft));

            for (int ch = 0; ch < outChannels; ++ch)
            {
                const int srcCh = juce::jmin (ch, srcChannels - 1);
                output.addFrom (ch, outOffset,
                                *src, srcCh,
                                static_cast<int> (v.samplePos),
                                chunk, v.gain);
            }

            v.samplePos += chunk;
            outOffset   += chunk;
            remaining   -= chunk;

            if (v.samplePos >= readEnd)
                v.active = false;
        }
    }
}

//==============================================================================
bool WaveFiletProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor* WaveFiletProcessor::createEditor()
{
    return new WaveFiletEditor (*this);
}

//==============================================================================
void WaveFiletProcessor::loadFile (const juce::File& file)
{
    loadingThread->stopThread (1000);

    slices.clear();
    nextSliceId = 0;

    // Clear custom buffers from previous file
    for (int i = 0; i < kMaxSlices; ++i)
    {
        customSlots[i].ptr.store (nullptr, std::memory_order_release);
        customSlots[i].len.store (0,       std::memory_order_release);
        customSlots[i].storage.reset();
    }

    publishSliceSnapshot();
    loadingThread->load (file);
}

void WaveFiletProcessor::onFileLoaded (std::unique_ptr<juce::AudioBuffer<float>> buffer,
                                        double sampleRate, juce::int64 numSamples,
                                        const juce::File& file)
{
    const int inactiveSlot = 1 - activeAudioSlot.load (std::memory_order_relaxed);
    audioSlots[inactiveSlot].buffer     = std::move (buffer);
    audioSlots[inactiveSlot].sampleRate = sampleRate;
    audioSlots[inactiveSlot].length     = numSamples;

    activeAudioSlot.store (inactiveSlot, std::memory_order_release);

    messageSampleRate = sampleRate;
    messageFileLength = numSamples;
    currentFile       = file;
    audioLoaded.store (true, std::memory_order_release);

    sendChangeMessage();
}

//==============================================================================
void WaveFiletProcessor::addSlice (double pos)
{
    if (static_cast<int> (slices.size()) >= kMaxSlices)
        return;

    Slice s;
    s.position = juce::jlimit (0.0, 1.0, pos);
    s.id       = nextSliceId++;
    slices.push_back (s);
    std::sort (slices.begin(), slices.end(),
               [] (const Slice& a, const Slice& b) { return a.position < b.position; });
    publishSliceSnapshot();
}

void WaveFiletProcessor::moveSlice (int index, double pos)
{
    if (index < 0 || index >= static_cast<int> (slices.size()))
        return;
    slices[index].position = juce::jlimit (0.0, 1.0, pos);
    std::sort (slices.begin(), slices.end(),
               [] (const Slice& a, const Slice& b) { return a.position < b.position; });
    publishSliceSnapshot();
}

void WaveFiletProcessor::removeSlice (int index)
{
    if (index < 0 || index >= static_cast<int> (slices.size()))
        return;

    // Clear any custom audio for this slot
    if (index < kMaxSlices)
    {
        customSlots[index].ptr.store (nullptr, std::memory_order_release);
        customSlots[index].len.store (0,       std::memory_order_release);
        customSlots[index].storage.reset();
    }

    slices.erase (slices.begin() + index);
    publishSliceSnapshot();
}

void WaveFiletProcessor::publishSliceSnapshot()
{
    // Write into the buffer the audio thread is NOT currently reading
    writeSnapshotIdx = 1 - writeSnapshotIdx;
    SliceSnapshot& snap = snapshotBuffers[writeSnapshotIdx];
    snap.count = static_cast<int> (juce::jmin (static_cast<int> (slices.size()), kMaxSlices));
    for (int i = 0; i < snap.count; ++i)
    {
        snap.positions[i] = slices[i].position;
        snap.ids[i]       = slices[i].id;
        snap.offsets[i]   = slices[i].startOffset;
    }
    audioThreadSnapshot.store (&snap, std::memory_order_release);
}

//==============================================================================
// Phase 3 — per-slice custom audio
//==============================================================================
void WaveFiletProcessor::setSliceCustomFile (int index, const juce::File& file)
{
    if (index < 0 || index >= static_cast<int> (slices.size()) || index >= kMaxSlices)
        return;

    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
    if (reader == nullptr)
        return;

    const int         numCh  = juce::jmin (2, static_cast<int> (reader->numChannels));
    const juce::int64 len    = juce::jmin (reader->lengthInSamples,
                                           static_cast<juce::int64> (600.0 * reader->sampleRate));
    auto buf = std::make_unique<juce::AudioBuffer<float>> (numCh, static_cast<int> (len));
    reader->read (buf.get(), 0, static_cast<int> (len), 0, true, true);

    slices[index].customFile = file;
    customSlots[index].storage = std::move (buf);
    customSlots[index].len.store (len,                              std::memory_order_release);
    customSlots[index].ptr.store (customSlots[index].storage.get(), std::memory_order_release);
}

void WaveFiletProcessor::clearSliceCustomFile (int index)
{
    if (index < 0 || index >= kMaxSlices)
        return;
    customSlots[index].ptr.store (nullptr, std::memory_order_release);
    customSlots[index].len.store (0,       std::memory_order_release);
    customSlots[index].storage.reset();
    if (index < static_cast<int> (slices.size()))
        slices[index].customFile = juce::File();
}

void WaveFiletProcessor::adjustSliceStartOffset (int index, double delta)
{
    if (index < 0 || index >= static_cast<int> (slices.size()))
        return;
    slices[index].startOffset = juce::jlimit (0.0, 0.99,
                                               slices[index].startOffset + delta);
    publishSliceSnapshot();
}

//==============================================================================
// Phase 4 — auto-slicing
//==============================================================================
void WaveFiletProcessor::setSlicingMode (SlicingMode mode)
{
    slicingMode = mode;
}

void WaveFiletProcessor::autoSliceTransients (float threshold, double minGapSec)
{
    const int slot = activeAudioSlot.load (std::memory_order_relaxed);
    const AudioData& ad = audioSlots[slot];
    if (ad.buffer == nullptr || ad.length == 0)
        return;

    slices.clear();
    nextSliceId = 0;

    const int         windowSize     = 512;
    const juce::int64 minGapSamples  = static_cast<juce::int64> (minGapSec * ad.sampleRate);
    const int         numChannels    = ad.buffer->getNumChannels();
    const float       threshSq       = threshold * threshold;

    float       prevEnergy      = 0.0f;
    juce::int64 lastSliceSample = 0;

    for (juce::int64 i = 0; i + windowSize <= ad.length; i += windowSize / 2)
    {
        float energy = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float* data = ad.buffer->getReadPointer (ch, static_cast<int> (i));
            for (int s = 0; s < windowSize; ++s)
                energy += data[s] * data[s];
        }
        energy /= static_cast<float> (windowSize * numChannels);

        if ((energy - prevEnergy) > threshSq
            && (i - lastSliceSample) > minGapSamples
            && static_cast<int> (slices.size()) < kMaxSlices)
        {
            Slice s;
            s.position = static_cast<double> (i) / static_cast<double> (ad.length);
            s.id       = nextSliceId++;
            slices.push_back (s);
            lastSliceSample = i;
        }
        prevEnergy = energy;
    }

    std::sort (slices.begin(), slices.end(),
               [] (const Slice& a, const Slice& b) { return a.position < b.position; });
    publishSliceSnapshot();
    sendChangeMessage();
}

void WaveFiletProcessor::autoSliceFixed (int divisions)
{
    divisions = juce::jlimit (1, kMaxSlices, divisions);
    slices.clear();
    nextSliceId = 0;
    for (int i = 0; i < divisions; ++i)
    {
        Slice s;
        s.position = static_cast<double> (i) / divisions;
        s.id       = nextSliceId++;
        slices.push_back (s);
    }
    publishSliceSnapshot();
    sendChangeMessage();
}

//==============================================================================
// Phase 2 — UI-triggered playback
//==============================================================================
void WaveFiletProcessor::triggerSlice (int sliceIndex)
{
    if (sliceIndex < 0 || sliceIndex >= static_cast<int> (slices.size()))
        return;

    const int noteNumber = kBaseMidiNote + slices[sliceIndex].id;
    if (noteNumber > 127)
        return;

    const SliceSnapshot* snap = audioThreadSnapshot.load (std::memory_order_acquire);
    if (snap != nullptr)
        noteOn (*snap, noteNumber);
}

void WaveFiletProcessor::stopAllVoices()
{
    for (auto& v : voices)
        v.active = false;
}

//==============================================================================
// State persistence
//==============================================================================
void WaveFiletProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree state ("WaveFiletState");
    state.setProperty ("filePath",    currentFile.getFullPathName(),       nullptr);
    state.setProperty ("nextSliceId", nextSliceId,                         nullptr);
    state.setProperty ("slicingMode", static_cast<int> (slicingMode),      nullptr);

    for (const auto& s : slices)
    {
        juce::ValueTree child ("Slice");
        child.setProperty ("pos",         s.position,                      nullptr);
        child.setProperty ("id",          s.id,                            nullptr);
        child.setProperty ("startOffset", s.startOffset,                   nullptr);
        child.setProperty ("customFile",  s.customFile.getFullPathName(),   nullptr);
        state.appendChild (child, nullptr);
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

    slicingMode = static_cast<SlicingMode> (
        static_cast<int> (state.getProperty ("slicingMode", 0)));

    nextSliceId = static_cast<int> (state.getProperty ("nextSliceId", 0));

    const juce::String path = state.getProperty ("filePath", "").toString();
    if (path.isNotEmpty())
    {
        const juce::File f (path);
        if (f.existsAsFile())
            loadFile (f);  // async; fires sendChangeMessage when done
    }

    slices.clear();
    for (int i = 0; i < state.getNumChildren(); ++i)
    {
        const auto child = state.getChild (i);
        if (! child.hasType ("Slice"))
            continue;

        Slice s;
        s.position    = static_cast<double> (child.getProperty ("pos",         0.0));
        s.id          = static_cast<int>    (child.getProperty ("id",          i));   // fallback: index
        s.startOffset = static_cast<double> (child.getProperty ("startOffset", 0.0));

        const juce::String cf = child.getProperty ("customFile", "").toString();
        if (cf.isNotEmpty())
            s.customFile = juce::File (cf);

        slices.push_back (s);
    }
    std::sort (slices.begin(), slices.end(),
               [] (const Slice& a, const Slice& b) { return a.position < b.position; });

    // Reload custom audio files
    for (int i = 0; i < static_cast<int> (slices.size()); ++i)
        if (slices[i].customFile.existsAsFile())
            setSliceCustomFile (i, slices[i].customFile);

    publishSliceSnapshot();
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new WaveFiletProcessor();
}
