#include "PluginEditor.h"
#include "Theme.h"

WaveFiletEditor::WaveFiletEditor (WaveFiletProcessor& p)
    : AudioProcessorEditor (&p),
      audioProcessor (p),
      waveformComponent (p, p.getFormatManager(), thumbnailCache)
{

    addAndMakeVisible (waveformComponent);

    loadButton.onClick = [this] { onLoadButtonClicked(); };
    addAndMakeVisible (loadButton);

    fileLabel.setText ("No file loaded", juce::dontSendNotification);
    fileLabel.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    // juce::Font(float) is deprecated in JUCE 8; use FontOptions instead (Issue #5).
    fileLabel.setFont (juce::Font (juce::FontOptions{}.withHeight (13.0f)));
    addAndMakeVisible (fileLabel);

    // After session restore the processor already has audio loaded but the
    // thumbnail was never populated (setSource is only called via UI gestures).
    // restoreFromProcessor() rebuilds the thumbnail from the existing file so
    // the waveform is visible immediately on re-open.
    if (audioProcessor.hasAudio())
    {
        fileLabel.setText (audioProcessor.getLoadedFile().getFileName(),
                           juce::dontSendNotification);
        waveformComponent.restoreFromProcessor();
    }

    setSize (700, 300);
}

WaveFiletEditor::~WaveFiletEditor() {}

//==============================================================================
void WaveFiletEditor::paint (juce::Graphics& g)
{
    g.fillAll (Theme::editor_background);
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
