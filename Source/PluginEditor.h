#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "WaveformComponent.h"

class WaveFiletEditor final : public juce::AudioProcessorEditor
{
public:
    explicit WaveFiletEditor (WaveFiletProcessor&);
    ~WaveFiletEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    WaveFiletProcessor& audioProcessor;

    // Format manager is owned by the processor; we share it here for the thumbnail.
    juce::AudioThumbnailCache  thumbnailCache { 5 };

    WaveformComponent waveformComponent;
    juce::TextButton  loadButton  { "Load File" };
    juce::Label       fileLabel;

    std::unique_ptr<juce::FileChooser> fileChooser;

    void onLoadButtonClicked();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveFiletEditor)
};
