#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "WaveformComponent.h"

class WaveFiletEditor final : public juce::AudioProcessorEditor,
                               public juce::ChangeListener
{
public:
    explicit WaveFiletEditor (WaveFiletProcessor&);
    ~WaveFiletEditor() override;

    void paint   (juce::Graphics&) override;
    void resized () override;

    // ChangeListener — reacts to processor file-load events (updates file label)
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

private:
    WaveFiletProcessor& audioProcessor;

    juce::AudioThumbnailCache thumbnailCache { 5 };

    WaveformComponent waveformComponent;

    // Toolbar widgets
    juce::TextButton loadButton  { "Load File" };
    juce::Label      fileLabel;

    // Phase 4: mode selector
    juce::ComboBox   modeSelector;
    juce::Label      modeLabel;
    juce::Slider     fixedDivisionsSlider;
    juce::Label      fixedDivisionsLabel;
    juce::TextButton applyAutoButton { "Apply" };
    juce::TextButton detectButton    { "Detect" };

    std::unique_ptr<juce::FileChooser> fileChooser;

    void onLoadButtonClicked ();
    void onModeChanged       ();
    void onApplyAutoClicked  ();
    void updateModeWidgets   ();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveFiletEditor)
};
