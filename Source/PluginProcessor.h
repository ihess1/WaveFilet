#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>
#include <array>
#include <memory>
#include <vector>

class FileLoadingThread;

class WaveFiletProcessor final : public juce::AudioProcessor,
                                  public juce::ChangeBroadcaster
{
public:
    //==========================================================================
    // Constants
    static constexpr int kBaseMidiNote            = 36;
    static constexpr int kMaxSlices               = 92;  // notes 36..127
    static constexpr int kMaxVoices               = 16;
    static constexpr int kInterpolationGuardSamples = 4;

    //==========================================================================
    // Slice — creation-order MIDI identity model.
    // id maps directly to MIDI note: kBaseMidiNote + id.
    // Dragging a marker past another never changes its id.
    struct Slice
    {
        double     position    = 0.0;  // normalised [0,1]
        int        id          = 0;    // stable MIDI identity, never reused
        double     startOffset = 0.0;  // normalised within-slice start offset [0,1)
        juce::File customFile;         // optional per-slice wav override (Phase 3)
    };

    enum class SlicingMode { Manual, Transient, Fixed };

    //==========================================================================
    // SliceSnapshot — lock-free view used by the audio thread.
    // Published after every slice mutation via publishSliceSnapshot().
    struct SliceSnapshot
    {
        double positions[kMaxSlices] = {};
        double offsets  [kMaxSlices] = {};
        int    ids      [kMaxSlices] = {};
        int    count                 = 0;
    };

    //==========================================================================
    WaveFiletProcessor();
    ~WaveFiletProcessor() override;

    //==========================================================================
    // AudioProcessor overrides
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
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
    void setCurrentProgram (int) override;
    const juce::String getProgramName (int) override;
    void changeProgramName (int, const juce::String&) override;

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    //==========================================================================
    // File loading
    void loadFile (const juce::File& file);

    // Called on the message thread by FileLoadingThread when decoding is done.
    void onFileLoaded (std::unique_ptr<juce::AudioBuffer<float>> buffer,
                       double sampleRate, juce::int64 numSamples,
                       const juce::File& file);

    bool              hasAudio()          const { return audioLoaded.load(); }
    double            getFileSampleRate() const { return messageSampleRate;  }
    juce::int64       getLengthInSamples()const { return messageFileLength;  }
    const juce::File& getLoadedFile()     const { return currentFile;        }

    //==========================================================================
    // Slice API — message thread only
    const std::vector<Slice>& getSlices() const { return slices; }
    void addSlice    (double normalizedPosition);
    void moveSlice   (int index, double normalizedPosition);
    void removeSlice (int index);

    // Phase 3: per-slice customisation
    void setSliceCustomFile     (int index, const juce::File& file);
    void clearSliceCustomFile   (int index);
    void adjustSliceStartOffset (int index, double deltaNormalized);

    // Phase 4: auto-slicing
    void setSlicingMode      (SlicingMode mode);
    SlicingMode getSlicingMode() const { return slicingMode; }
    void autoSliceTransients (float threshold = 0.15f, double minGapSec = 0.05);
    void autoSliceFixed      (int divisions);

    // Phase 2: UI-triggered playback
    void triggerSlice  (int sliceIndex);
    void stopAllVoices ();

    juce::AudioFormatManager& getFormatManager() { return formatManager; }

    // Audio-thread snapshot — call from audio thread only
    const SliceSnapshot* getAudioThreadSnapshot() const
    {
        return audioThreadSnapshot.load (std::memory_order_acquire);
    }

private:
    //==========================================================================
    // Audio double-buffer (lock-free file reload)
    struct AudioData
    {
        std::unique_ptr<juce::AudioBuffer<float>> buffer;
        double      sampleRate = 0.0;
        juce::int64 length     = 0;
    };

    AudioData        audioSlots[2];
    std::atomic<int> activeAudioSlot { 0 };

    // Message-thread mirrors — safe to read without touching atomic slot index
    double          messageSampleRate = 0.0;
    juce::int64     messageFileLength = 0;
    juce::File      currentFile;
    std::atomic<bool> audioLoaded    { false };

    //==========================================================================
    // Slice state — message thread only
    std::vector<Slice> slices;
    int         nextSliceId = 0;
    SlicingMode slicingMode = SlicingMode::Manual;

    // Per-slice custom audio (Phase 3)
    // storage owned on message thread; ptr/len exposed atomically to audio thread
    struct CustomBufSlot
    {
        std::unique_ptr<juce::AudioBuffer<float>> storage;
        std::atomic<juce::AudioBuffer<float>*>    ptr { nullptr };
        std::atomic<juce::int64>                  len { 0 };
    };
    CustomBufSlot customSlots[kMaxSlices];

    //==========================================================================
    // SliceSnapshot double-buffer (ABA-safe: write into the inactive buffer,
    // then atomically swap the pointer so the audio thread never sees a partial write)
    SliceSnapshot               snapshotBuffers[2];
    std::atomic<SliceSnapshot*> audioThreadSnapshot { &snapshotBuffers[0] };
    int                         writeSnapshotIdx = 0;

    void publishSliceSnapshot();

    //==========================================================================
    // Voice table — audio thread only
    struct Voice
    {
        bool        active     = false;
        int         noteNumber = -1;
        juce::int64 samplePos  = 0;
        juce::int64 endSample  = 0;  // exclusive, in main-buffer coords
        // Phase 3: custom buffer override (nullptr = use main audio slot)
        juce::AudioBuffer<float>* customBuf = nullptr;
        juce::int64               customLen = 0;
        float gain = 1.0f;
    };

    Voice voices[kMaxVoices];

    void noteOn      (const SliceSnapshot& snap, int noteNumber);
    void noteOff     (int noteNumber);
    void renderVoices(juce::AudioBuffer<float>& output, int startSample, int numSamples);

    //==========================================================================
    juce::AudioFormatManager           formatManager;
    std::unique_ptr<FileLoadingThread> loadingThread;
    double                             playbackSampleRate = 0.0;

    juce::WeakReference<WaveFiletProcessor>::Master masterReference;
    friend class juce::WeakReference<WaveFiletProcessor>;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveFiletProcessor)
};
