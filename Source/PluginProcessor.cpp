#include "PluginProcessor.h"
#include "PluginEditor.h"

// =============================================================================
// FileLoadingThread
// =============================================================================

/**
 * Background thread that decodes an audio file without blocking the message
 * thread (Issue #3: synchronous file I/O freeze / DAW watchdog violation).
 *
 * Lifecycle
 * ---------
 * The processor creates a new FileLoadingThread on every loadFile() call.
 * If a previous load is still in progress it is signalled to exit and we
 * wait up to 200 ms for it to finish before starting the new one.
 *
 * Delivery
 * --------
 * On successful decode, the thread posts the result to the message thread via
 * MessageManager::callAsync.  The callback is guarded by a WeakReference so
 * it becomes a no-op if the processor is destroyed before the callback fires
 * (e.g. the user closes the plugin window mid-load).
 *
 * Cancellation checkpoints
 * ------------------------
 * threadShouldExit() is tested after every expensive step so a superseded load
 * (user opened a second file before the first finished) exits quickly without
 * wasting CPU decoding data that will be thrown away.
 */
class WaveFiletProcessor::FileLoadingThread : public juce::Thread
{
public:
    FileLoadingThread (WaveFiletProcessor& owner,
                       const juce::File&   fileToLoad,
                       juce::AudioFormatManager& fmgr)
        : juce::Thread ("WaveFilet FileLoader"),
          owner        (owner),
          file         (fileToLoad),
          formatManager (fmgr)
    {}

    void run() override
    {
        if (threadShouldExit())
            return;

        std::unique_ptr<juce::AudioFormatReader> reader (
            formatManager.createReaderFor (file));

        // If the file can't be decoded (unsupported format, missing file, etc.)
        // we simply return without posting a result.  The processor stays in
        // its previous state.
        if (reader == nullptr || threadShouldExit())
            return;

        // Cap at 10 minutes to avoid allocating enormous buffers for
        // accidentally-long files (e.g. a full album rip).
        static constexpr double kMaxSeconds = 600.0;
        const juce::int64 maxSamples = juce::jmin (
            reader->lengthInSamples,
            static_cast<juce::int64> (kMaxSeconds * reader->sampleRate));

        // Allocate the result payload.  This is the only heap allocation in
        // the loading path; it happens here on the background thread, never on
        // the audio thread.
        AudioData result;
        result.samples.setSize (
            juce::jmin (2, static_cast<int> (reader->numChannels)),
            static_cast<int> (maxSamples) + AudioData::kInterpolationGuardSamples);

        if (threadShouldExit())
            return;

        // Decode the entire file into the buffer.  This is the slow part.
        reader->read (&result.samples, 0,
                      static_cast<int> (maxSamples) + AudioData::kInterpolationGuardSamples,
                      0, true, true);

        if (threadShouldExit())
            return;

        result.sampleRate = reader->sampleRate;
        result.length     = maxSamples;
        result.file       = file;
        result.valid      = true;

        // Deliver to the message thread.  A WeakReference guards against the
        // case where the processor is destroyed before this fires.
        juce::WeakReference<WaveFiletProcessor> weakOwner (&owner);
        juce::MessageManager::callAsync (
            [weakOwner, data = std::move (result)] () mutable
            {
                if (auto* p = weakOwner.get())
                    p->onFileLoaded (std::move (data));
            });
    }

private:
    WaveFiletProcessor&       owner;
    juce::File                file;
    juce::AudioFormatManager& formatManager;
};

// =============================================================================
// WaveFiletProcessor
// =============================================================================

WaveFiletProcessor::WaveFiletProcessor()
    : AudioProcessor (BusesProperties()
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    formatManager.registerBasicFormats();

    // Initialise the slice snapshot to point at an empty buffer so the audio
    // thread never dereferences a null pointer even before the first slice is
    // added.  inactiveSnapshotIdx starts at 1 because snapshotBuffers[0] is
    // the initial active snapshot.
    snapshotBuffers[0].count = 0;
    snapshotBuffers[1].count = 0;
    audioThreadSnapshot.store (&snapshotBuffers[0], std::memory_order_release);
    inactiveSnapshotIdx = 1;
}

WaveFiletProcessor::~WaveFiletProcessor()
{
    // Stop the loading thread before any members are destroyed.  This ensures
    // the FileLoadingThread's run() loop exits before the formatManager and
    // audioSlots that it references are torn down.
    if (loadingThread != nullptr)
    {
        loadingThread->signalThreadShouldExit();
        loadingThread->waitForThreadToExit (500);
    }
}

// =============================================================================
// AudioProcessor boilerplate
// =============================================================================

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

void WaveFiletProcessor::prepareToPlay (double, int) {}
void WaveFiletProcessor::releaseResources()          {}

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
    // Phase 1: no audio output yet — clear the buffer to silence.
    //
    // Phase 2 will read from audioSlots[activeAudioSlot.load(acquire)] and
    // use getAudioThreadSnapshot() for slice positions.  Both are safe to call
    // here because:
    //   - activeAudioSlot is atomic; the message thread only writes to the
    //     *inactive* slot so there is no data race on the buffer contents.
    //   - audioThreadSnapshot is an atomic pointer to a fully-written,
    //     immutable snapshot buffer.
    buffer.clear();
}

// =============================================================================
// Editor
// =============================================================================

bool WaveFiletProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor* WaveFiletProcessor::createEditor()
{
    return new WaveFiletEditor (*this);
}

// =============================================================================
// File loading
// =============================================================================

void WaveFiletProcessor::loadFile (const juce::File& file)
{
    // Clear slice markers immediately.  Old positions are meaningless relative
    // to a new file's timeline, so they should not appear on screen while the
    // new file is loading.
    slices.clear();
    nextSliceId = 0;
    publishSliceSnapshot();

    // If a previous load is still running, signal it to exit and wait briefly.
    // The 200 ms timeout is generous; the thread checks threadShouldExit() at
    // every read checkpoint so it usually exits within a few milliseconds.
    if (loadingThread != nullptr && loadingThread->isThreadRunning())
    {
        loadingThread->signalThreadShouldExit();
        loadingThread->waitForThreadToExit (200);
    }

    loadingThread = std::make_unique<FileLoadingThread> (*this, file, formatManager);
    loadingThread->startThread();
}

void WaveFiletProcessor::onFileLoaded (AudioData&& newData)
{
    // This runs on the message thread (posted via callAsync from the loading
    // thread).  Install the decoded audio into the inactive double-buffer slot,
    // then atomically promote it so the audio thread picks it up on the next
    // processBlock() call.

    // Determine which slot is currently inactive (i.e. not being read by the
    // audio thread).  This is always the slot that is NOT activeAudioSlot.
    const int inactiveSlot = 1 - activeAudioSlot.load (std::memory_order_relaxed);

    // Write the new audio into the inactive slot.  The audio thread never
    // touches an inactive slot, so this write is race-free.
    audioSlots[inactiveSlot] = std::move (newData);

    // Atomically promote: the next processBlock() load of activeAudioSlot will
    // see the new slot.  release ordering ensures all prior writes to
    // audioSlots[inactiveSlot] are visible to the audio thread's acquire load.
    activeAudioSlot.store (inactiveSlot, std::memory_order_release);

    // Update message-thread metadata mirrors so hasAudio(), getFileSampleRate(),
    // etc. reflect the new file without touching the atomic activeAudioSlot again.
    const AudioData& installed = audioSlots[inactiveSlot];
    messageSampleRate  = installed.sampleRate;
    messageFileLength  = installed.length;
    currentFile        = installed.file;
    audioLoaded        = true;

    // Notify the editor (WaveformComponent) that new audio is available so it
    // can repaint even if the thumbnail has already finished loading.
    sendChangeMessage();
}

// =============================================================================
// Slice model
// =============================================================================

void WaveFiletProcessor::addSlice (double pos)
{
    // Silently ignore if we have hit the snapshot capacity.  The UI should
    // enforce kMaxSlices before calling here, but defensive coding is cheap.
    if (static_cast<int> (slices.size()) >= kMaxSlices)
        return;

    pos = juce::jlimit (0.0, 1.0, pos);

    // Assign the next stable id and bump the counter.  The id maps directly to
    // a MIDI note and never changes, even after drag-reordering.
    Slice s;
    s.position = pos;
    s.id       = nextSliceId++;

    slices.push_back (s);

    // Keep the vector sorted by position so left-to-right visual index matches
    // the sort index passed to moveSlice / removeSlice.
    std::sort (slices.begin(), slices.end(),
               [] (const Slice& a, const Slice& b) { return a.position < b.position; });

    publishSliceSnapshot();
}

void WaveFiletProcessor::moveSlice (int index, double pos)
{
    if (index < 0 || index >= static_cast<int> (slices.size()))
        return;

    slices[index].position = juce::jlimit (0.0, 1.0, pos);

    // Re-sort by position.  The stable id is preserved through the sort so the
    // MIDI mapping is unchanged even if the slice crosses another marker.
    std::sort (slices.begin(), slices.end(),
               [] (const Slice& a, const Slice& b) { return a.position < b.position; });

    // Publish after the sort so the snapshot reflects the new sort order.
    publishSliceSnapshot();
}

void WaveFiletProcessor::removeSlice (int index)
{
    if (index < 0 || index >= static_cast<int> (slices.size()))
        return;

    // The removed slice's id is retired — it is never reused.  This preserves
    // the MIDI-note assignment of all surviving slices.
    slices.erase (slices.begin() + index);
    publishSliceSnapshot();
}

// =============================================================================
// Lock-free audio-thread snapshot  (Issue #1)
// =============================================================================

void WaveFiletProcessor::publishSliceSnapshot()
{
    // Write into the *inactive* snapshot buffer (the one the audio thread is
    // NOT currently pointing at).  The inactive buffer is message-thread-owned
    // until we atomically swap the pointer below.
    SliceSnapshot& inactive = snapshotBuffers[inactiveSnapshotIdx];

    inactive.count = juce::jmin (static_cast<int> (slices.size()), kMaxSlices);

    for (int i = 0; i < inactive.count; ++i)
    {
        inactive.entries[i].position = slices[i].position;
        inactive.entries[i].id       = slices[i].id;
    }

    // Atomically publish the newly-written buffer.  release ordering ensures
    // the audio thread's acquire load sees the fully-written entries above.
    audioThreadSnapshot.store (&inactive, std::memory_order_release);

    // Flip: the buffer we just published becomes the active one, so the other
    // buffer is now the inactive one we'll write into next time.
    inactiveSnapshotIdx = 1 - inactiveSnapshotIdx;
}

// =============================================================================
// State persistence
// =============================================================================

void WaveFiletProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree state ("WaveFiletState");
    state.setProperty ("filePath",    currentFile.getFullPathName(), nullptr);
    // nextSliceId must be saved so that restored slices keep their MIDI-note
    // assignments even after the session is closed and re-opened.
    state.setProperty ("nextSliceId", nextSliceId,                   nullptr);

    for (const auto& s : slices)
    {
        juce::ValueTree slice ("Slice");
        slice.setProperty ("pos", s.position, nullptr);
        // id is saved so that MIDI-note identity survives a round-trip through
        // the host's save/restore cycle.
        slice.setProperty ("id",  s.id,       nullptr);
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

    // loadFile() clears slices and starts background loading.  We restore
    // the slices immediately after so they are visible once the audio loads.
    const juce::String path = state.getProperty ("filePath", "").toString();
    if (path.isNotEmpty())
    {
        const juce::File f (path);
        if (f.existsAsFile())
            loadFile (f);  // also clears slices and resets nextSliceId to 0
    }

    // Restore slices with their stable ids.
    // Backward-compat: old save files that predate the id field use the slice's
    // list index as a fallback id (preserves the implicit sort-order mapping).
    slices.clear();
    for (int i = 0; i < state.getNumChildren(); ++i)
    {
        const auto child = state.getChild (i);
        if (child.hasType ("Slice"))
        {
            Slice s;
            s.position = static_cast<double> (child.getProperty ("pos", 0.0));
            // Fall back to list index for old save files without an id attribute.
            s.id       = static_cast<int>    (child.getProperty ("id",  i));
            slices.push_back (s);
        }
    }

    // Sort by position (should already be sorted, but defensive).
    std::sort (slices.begin(), slices.end(),
               [] (const Slice& a, const Slice& b) { return a.position < b.position; });

    // Restore the next-id counter: it must be greater than every existing id so
    // new slices never collide with restored ones.
    nextSliceId = 0;
    for (const auto& s : slices)
        nextSliceId = std::max (nextSliceId, s.id + 1);

    // The saved nextSliceId may be higher than max(id)+1 if the user added
    // slices and then deleted the highest-id ones — respect that to avoid
    // re-issuing an id that was previously in use in a recorded MIDI pattern.
    const int savedNextId = static_cast<int> (state.getProperty ("nextSliceId", nextSliceId));
    nextSliceId = std::max (nextSliceId, savedNextId);

    publishSliceSnapshot();
}

// =============================================================================
// Plugin entry point
// =============================================================================

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new WaveFiletProcessor();
}
