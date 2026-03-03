#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>

class WaveFiletProcessor final : public juce::AudioProcessor
{
public:
    WaveFiletProcessor();
    ~WaveFiletProcessor() override;

    //==============================================================================
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

    //==============================================================================
    // File loading
    void loadFile (const juce::File& file);
    bool hasAudio() const;
    double getFileSampleRate() const     { return fileSampleRate; }
    juce::int64 getLengthInSamples() const { return fileLength; }
    const juce::File& getLoadedFile() const { return loadedFile; }

    //==============================================================================
    // Slice model — positions are normalized doubles in [0.0, 1.0]
    const std::vector<double>& getSlicePositions() const { return slicePositions; }
    void addSlice (double normalizedPosition);
    void moveSlice (int index, double normalizedPosition);
    void removeSlice (int index);

    juce::AudioFormatManager& getFormatManager() { return formatManager; }

private:
    juce::AudioFormatManager formatManager;
    juce::AudioBuffer<float> audioBuffer;
    double fileSampleRate = 0.0;
    juce::int64 fileLength = 0;
    juce::File loadedFile;

    std::vector<double> slicePositions; // always sorted

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveFiletProcessor)
};
