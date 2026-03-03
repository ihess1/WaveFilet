#include "PluginEditor.h"

WaveFiletEditor::WaveFiletEditor (WaveFiletProcessor& p)
    : AudioProcessorEditor (&p),
      audioProcessor (p),
      waveformComponent (p, formatManager, thumbnailCache)
{
    formatManager.registerBasicFormats();

    addAndMakeVisible (waveformComponent);

    loadButton.onClick = [this] { onLoadButtonClicked(); };
    addAndMakeVisible (loadButton);

    fileLabel.setText ("No file loaded", juce::dontSendNotification);
    fileLabel.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    fileLabel.setFont (juce::Font (13.0f));
    addAndMakeVisible (fileLabel);

    // Restore filename label from processor state
    if (audioProcessor.hasAudio())
        fileLabel.setText (audioProcessor.getLoadedFile().getFileName(),
                           juce::dontSendNotification);

    setSize (700, 300);
}

WaveFiletEditor::~WaveFiletEditor() {}

//==============================================================================
void WaveFiletEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff16213e));
}

void WaveFiletEditor::resized()
{
    auto area = getLocalBounds().reduced (8);

    // Top toolbar: [Load File btn] [filename label]
    auto toolbar = area.removeFromTop (32);
    loadButton.setBounds (toolbar.removeFromLeft (100).reduced (2));
    fileLabel.setBounds  (toolbar.reduced (4, 2));

    area.removeFromTop (4); // gap

    // Remainder: waveform
    waveformComponent.setBounds (area);
}

//==============================================================================
void WaveFiletEditor::onLoadButtonClicked()
{
    fileChooser = std::make_unique<juce::FileChooser> (
        "Select a WAV file",
        juce::File::getSpecialLocation (juce::File::userMusicDirectory),
        "*.wav");

    constexpr auto flags = juce::FileBrowserComponent::openMode
                         | juce::FileBrowserComponent::canSelectFiles;

    fileChooser->launchAsync (flags, [this] (const juce::FileChooser& fc)
    {
        const auto result = fc.getResult();
        if (result.existsAsFile())
        {
            waveformComponent.setSource (result);
            fileLabel.setText (result.getFileName(), juce::dontSendNotification);
        }
    });
}
