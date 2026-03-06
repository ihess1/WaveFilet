#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <array>
#include <atomic>
#include <vector>

/**
 * WaveFiletProcessor — the AudioProcessor subclass that owns all audio state.
 *
 * Thread-safety model
 * -------------------
 * There are two threads that matter:
 *
 *   Message thread  — UI events, loadFile(), all slice mutations, state save/restore.
 *   Audio thread    — processBlock() called by the host at ~10 ms intervals.
 *
 * The two critical shared resources and how they are protected:
 *
 *   slicePositions  (Issue #1)
 *     The UI-owned std::vector<Slice> is message-thread-only.  After every
 *     mutation a lock-free SliceSnapshot is written into one of two pre-
 *     allocated fixed-size buffers and the pointer is atomically promoted so
 *     the audio thread always reads a consistent, fully-written snapshot with
 *     no locks and no heap allocation.
 *
 *   audioBuffer  (Issues #2 and #3)
 *     File decoding happens on a dedicated FileLoadingThread to avoid freezing
 *     the message thread (Issue #3).  The decoded payload is stored in one of
 *     two pre-allocated AudioData slots.  The audio thread reads from the slot
 *     indicated by the atomic activeAudioSlot index; the message thread writes
 *     into the other (inactive) slot and then atomically promotes it.  Because
 *     the two slots are independent objects and the audio thread never touches
 *     the inactive one, this is race-free without any lock on the audio thread.
 *
 * MIDI identity model  (Issue #8)
 * --------------------------------
 * Slices carry a stable integer `id` assigned at creation time that maps
 * directly to a MIDI note (id 0 → kBaseMidiNote, id 1 → kBaseMidiNote+1, …).
 * The id is immutable for the lifetime of the slice — dragging a marker to a
 * new position never changes which MIDI note it plays.  This matches the
 * creation-order model used by hardware samplers (Akai MPC, Roland SP-404).
 * See the Slice struct documentation below for details.
 */
class WaveFiletProcessor final : public juce::AudioProcessor,
                                 public juce::ChangeBroadcaster
{
public:
    // =========================================================================
    // Slice data model

    /**
     * A single slice marker with a stable MIDI-note identity.
     *
     * position  Normalised position in [0.0, 1.0] relative to the loaded file.
     *           The slice list (WaveFiletProcessor::slices) is always kept
     *           sorted by this field so that left-to-right visual order
     *           corresponds to ascending indices.
     *
     * id        Stable, monotonically-assigned identifier.  Assigned once at
     *           addSlice() time and never changed thereafter, even when the
     *           slice is dragged past other slices and the sort order changes.
     *           Maps directly to a MIDI note: midiNote = kBaseMidiNote + id.
     *           IDs are never reused after a removeSlice() call, so gaps are
     *           possible (e.g. [id=0, id=2] after deleting id=1).
     */
    struct Slice
    {
        double position = 0.0;  // normalised [0.0, 1.0]
        int    id       = 0;    // stable creation-order MIDI-note identifier
    };

    /** Maximum number of simultaneous slices.  Matches SliceSnapshot capacity.
     *  Enforced at addSlice() time; the UI should not allow the user to exceed it. */
    static constexpr int kMaxSlices = 64;

    /** MIDI note number assigned to the slice with id == 0.
     *  C1 in the convention used by most DAWs (middle C = C3/60). */
    static constexpr int kBaseMidiNote = 36;  // C1

    // =========================================================================
    // Lock-free audio-thread snapshot  (Issue #1)

    /**
     * Immutable, fixed-capacity snapshot of the current slice list, safe for
     * wait-free consumption on the audio thread.
     *
     * Rationale for the fixed-size array:
     *   std::vector would require heap allocation.  Any heap allocation on the
     *   audio thread risks a lock inside malloc and causes priority inversion,
     *   which is undefined behaviour under the real-time contract.  A fixed
     *   std::array with a count field avoids this entirely.
     *
     * Publishing protocol (always on the message thread):
     *   1. Copy slices[] into the *inactive* snapshotBuffers[] slot.
     *   2. Store the pointer to that slot via audioThreadSnapshot (acq-rel).
     *   3. Flip inactiveSnapshotIdx to the other slot.
     * Audio thread just loads audioThreadSnapshot with acquire ordering and
     * reads count + entries — zero locks, zero allocation.
     */
    struct SliceSnapshot
    {
        struct Entry
        {
            double position = 0.0;
            int    id       = 0;
        };

        std::array<Entry, kMaxSlices> entries {};
        int count = 0;
    };

    // =========================================================================
    // Lifecycle

    WaveFiletProcessor();
    ~WaveFiletProcessor() override;

    // =========================================================================
    // AudioProcessor overrides

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // =========================================================================
    // File loading  (message thread only)

    /** Begins loading `file` on the FileLoadingThread.  Returns immediately;
     *  call hasAudio() to check when loading is complete.  Clears all slice
     *  markers as a side-effect (old positions are meaningless on a new file). */
    void loadFile (const juce::File& file);

    /** Returns true once a file has been loaded successfully.
     *  Message thread only.  Audio thread uses activeAudioSlot / AudioData. */
    bool hasAudio() const noexcept { return audioLoaded; }

    /** Sample rate of the currently loaded file.  Message thread only. */
    double getFileSampleRate() const noexcept { return messageSampleRate; }

    /** Length of the loaded file in samples (guard samples not included).
     *  Message thread only. */
    juce::int64 getLengthInSamples() const noexcept { return messageFileLength; }

    /** The last successfully loaded file.  Message thread only. */
    const juce::File& getLoadedFile() const noexcept { return currentFile; }

    // =========================================================================
    // Slice model  (all methods: message thread only)

    /** Returns the slice list sorted by position.  Message thread only.
     *  The audio thread should use getAudioThreadSnapshot() instead. */
    const std::vector<Slice>& getSlices() const noexcept { return slices; }

    /** Adds a slice at `normalizedPosition` if the list is below kMaxSlices.
     *  Position is clamped to [0.0, 1.0].  Keeps the list sorted.
     *  Publishes a fresh SliceSnapshot for the audio thread. */
    void addSlice (double normalizedPosition);

    /** Moves the slice at sort-index `index` to `normalizedPosition`.
     *  The list is re-sorted afterwards; the caller must re-acquire the index
     *  via the nearest-position heuristic (see WaveformComponent::mouseDrag).
     *  Publishes a fresh SliceSnapshot for the audio thread. */
    void moveSlice (int index, double normalizedPosition);

    /** Removes the slice at sort-index `index`.
     *  Publishes a fresh SliceSnapshot for the audio thread. */
    void removeSlice (int index);

    /** Returns the MIDI note number for a given stable slice id.
     *  Pure utility; does not touch any mutable state. */
    static int midiNoteForSliceId (int sliceId) noexcept
    {
        return kBaseMidiNote + sliceId;
    }

    // =========================================================================
    // Shared resources

    /** The format manager is owned here and shared (not duplicated) with the
     *  editor and WaveformComponent via this accessor. */
    juce::AudioFormatManager& getFormatManager() noexcept { return formatManager; }

    /** Returns the most recent lock-free slice snapshot published for the
     *  audio thread.  Safe to call from any thread; uses acquire ordering. */
    const SliceSnapshot* getAudioThreadSnapshot() const noexcept
    {
        return audioThreadSnapshot.load (std::memory_order_acquire);
    }

private:
    // =========================================================================
    // Audio data double-buffer  (Issues #2 and #3)

    /**
     * Self-contained audio payload for one "slot" in the double buffer.
     *
     * The processor keeps two of these (audioSlots[0] and audioSlots[1]).
     * The audio thread reads from audioSlots[activeAudioSlot]; the message
     * thread writes into the *other* slot (always inactive from the audio
     * thread's perspective), then atomically promotes it via activeAudioSlot.
     *
     * Because we never write into the active slot, there is no data race even
     * though we are not using a lock on the audio thread.
     *
     * kInterpolationGuardSamples: extra samples allocated beyond fileLength to
     * provide headroom for future cubic/sinc interpolation during pitched or
     * time-stretched playback.  fileLength (= AudioData::length) intentionally
     * does NOT include these guard samples — playback code must never iterate
     * beyond length.
     */
    struct AudioData
    {
        static constexpr int kInterpolationGuardSamples = 4;

        juce::AudioBuffer<float> samples;
        double      sampleRate = 0.0;
        juce::int64 length     = 0;    // valid samples, NOT including guard
        juce::File  file;
        bool        valid      = false;
    };

    AudioData        audioSlots[2];           // pre-allocated double-buffer
    std::atomic<int> activeAudioSlot { 0 };   // index of slot the audio thread reads

    // Message-thread metadata mirrors — safe to read without crossing to audio
    // thread, updated in onFileLoaded() and cleared in loadFile().
    double      messageSampleRate = 0.0;
    juce::int64 messageFileLength = 0;
    juce::File  currentFile;
    bool        audioLoaded       = false;

    // =========================================================================
    // Slice data  (message thread only)

    std::vector<Slice> slices;          // kept sorted by position
    int                nextSliceId = 0; // monotonically incremented; never reused

    // Lock-free audio-thread snapshot
    SliceSnapshot              snapshotBuffers[2];
    std::atomic<const SliceSnapshot*> audioThreadSnapshot { nullptr };
    int                        inactiveSnapshotIdx = 0; // message thread only

    /** Copies slices[] into the inactive snapshot buffer and atomically
     *  publishes it via audioThreadSnapshot.  Call after every slice mutation. */
    void publishSliceSnapshot();

    // =========================================================================
    // Background file loading  (Issue #3)

    // FileLoadingThread is defined fully in PluginProcessor.cpp to keep this
    // header free of juce::Thread implementation details.
    class FileLoadingThread;
    std::unique_ptr<FileLoadingThread> loadingThread;

    /** Called on the message thread (via MessageManager::callAsync) when
     *  background loading completes.  Installs the new audio into the
     *  double-buffer and fires a ChangeBroadcaster notification so the editor
     *  can update the waveform display. */
    void onFileLoaded (AudioData&& newData);

    // =========================================================================
    // Shared

    juce::AudioFormatManager formatManager;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveFiletProcessor)
    JUCE_DECLARE_WEAK_REFERENCEABLE (WaveFiletProcessor)
};
