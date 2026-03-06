#include "PluginEditor.h"
#include "Theme.h"

WaveFiletEditor::WaveFiletEditor (WaveFiletProcessor& p)
    : AudioProcessorEditor (&p),
      audioProcessor (p),
      waveformComponent (p, p.getFormatManager(), thumbnailCache)
{
    //--------------------------------------------------------------------------
    // Load File button
    loadButton.onClick = [this] { onLoadButtonClicked(); };
    addAndMakeVisible (loadButton);

    //--------------------------------------------------------------------------
    // File label
    fileLabel.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    fileLabel.setFont (juce::Font (juce::FontOptions{}.withHeight (13.0f)));
    addAndMakeVisible (fileLabel);

    //--------------------------------------------------------------------------
    // Mode selector (Phase 4)
    modeLabel.setText ("Mode:", juce::dontSendNotification);
    modeLabel.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    modeLabel.setFont (juce::Font (juce::FontOptions{}.withHeight (12.0f)));
    addAndMakeVisible (modeLabel);

    modeSelector.addItem ("Manual",    1);
    modeSelector.addItem ("Transient", 2);
    modeSelector.addItem ("Fixed",     3);
    modeSelector.setSelectedId (1, juce::dontSendNotification);
    modeSelector.onChange = [this] { onModeChanged(); };
    addAndMakeVisible (modeSelector);

    // Fixed-divisions slider (visible only in Fixed mode)
    fixedDivisionsSlider.setRange (2, 64, 1);
    fixedDivisionsSlider.setValue (8, juce::dontSendNotification);
    fixedDivisionsSlider.setSliderStyle (juce::Slider::IncDecButtons);
    fixedDivisionsSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 32, 20);
    addChildComponent (fixedDivisionsSlider);

    fixedDivisionsLabel.setText ("Divs:", juce::dontSendNotification);
    fixedDivisionsLabel.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    fixedDivisionsLabel.setFont (juce::Font (juce::FontOptions{}.withHeight (12.0f)));
    addChildComponent (fixedDivisionsLabel);

    // Apply button (Fixed / Transient modes)
    applyAutoButton.onClick = [this] { onApplyAutoClicked(); };
    addChildComponent (applyAutoButton);

    //--------------------------------------------------------------------------
    // Waveform
    addAndMakeVisible (waveformComponent);

    //--------------------------------------------------------------------------
    // Listen to processor for file-load events so we can update the file label
    audioProcessor.addChangeListener (this);

    //--------------------------------------------------------------------------
    // Restore state if session already has audio
    if (audioProcessor.hasAudio())
    {
        fileLabel.setText (audioProcessor.getLoadedFile().getFileName(),
                           juce::dontSendNotification);
        waveformComponent.restoreFromProcessor();
    }
    else
    {
        fileLabel.setText ("No file loaded", juce::dontSendNotification);
    }

    // Restore mode selector from processor
    switch (audioProcessor.getSlicingMode())
    {
        case WaveFiletProcessor::SlicingMode::Manual:    modeSelector.setSelectedId (1, juce::dontSendNotification); break;
        case WaveFiletProcessor::SlicingMode::Transient: modeSelector.setSelectedId (2, juce::dontSendNotification); break;
        case WaveFiletProcessor::SlicingMode::Fixed:     modeSelector.setSelectedId (3, juce::dontSendNotification); break;
    }
    updateModeWidgets();

    setSize (780, 340);
}

WaveFiletEditor::~WaveFiletEditor()
{
    audioProcessor.removeChangeListener (this);
}

//==============================================================================
void WaveFiletEditor::paint (juce::Graphics& g)
{
    g.fillAll (Theme::background);
}

void WaveFiletEditor::resized()
{
    auto area = getLocalBounds().reduced (8);

    // Top toolbar row: [Load File] [filename] [Mode: combo] [divs label] [divs slider] [Apply]
    auto toolbar = area.removeFromTop (32);
    loadButton.setBounds (toolbar.removeFromLeft (90).reduced (2));
    toolbar.removeFromLeft (4);

    // Mode controls sit on the right
    auto modeArea = toolbar.removeFromRight (280);
    modeLabel.setBounds    (modeArea.removeFromLeft (40).reduced (2));
    modeSelector.setBounds (modeArea.removeFromLeft (80).reduced (2));
    modeArea.removeFromLeft (4);
    fixedDivisionsLabel.setBounds (modeArea.removeFromLeft (36).reduced (2));
    fixedDivisionsSlider.setBounds (modeArea.removeFromLeft (90).reduced (2));
    applyAutoButton.setBounds (modeArea.removeFromLeft (60).reduced (2));

    fileLabel.setBounds (toolbar.reduced (4, 2));

    area.removeFromTop (4);
    waveformComponent.setBounds (area);
}

//==============================================================================
void WaveFiletEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (audioProcessor.hasAudio())
        fileLabel.setText (audioProcessor.getLoadedFile().getFileName(),
                           juce::dontSendNotification);
}

//==============================================================================
void WaveFiletEditor::onLoadButtonClicked()
{
    fileChooser = std::make_unique<juce::FileChooser> (
        "Select an audio file",
        juce::File::getSpecialLocation (juce::File::userMusicDirectory),
        "*.wav;*.aif;*.aiff");

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

//==============================================================================
void WaveFiletEditor::onModeChanged()
{
    const int id = modeSelector.getSelectedId();
    if (id == 1) audioProcessor.setSlicingMode (WaveFiletProcessor::SlicingMode::Manual);
    if (id == 2) audioProcessor.setSlicingMode (WaveFiletProcessor::SlicingMode::Transient);
    if (id == 3) audioProcessor.setSlicingMode (WaveFiletProcessor::SlicingMode::Fixed);
    updateModeWidgets();
}

void WaveFiletEditor::updateModeWidgets()
{
    const int id = modeSelector.getSelectedId();
    const bool isFixed     = (id == 3);
    const bool isTransient = (id == 2);
    const bool showApply   = isFixed || isTransient;

    fixedDivisionsLabel.setVisible (isFixed);
    fixedDivisionsSlider.setVisible (isFixed);
    applyAutoButton.setVisible (showApply);

    applyAutoButton.setButtonText (isTransient ? "Detect" : "Apply");
}

void WaveFiletEditor::onApplyAutoClicked()
{
    const int id = modeSelector.getSelectedId();
    if (id == 2)
        audioProcessor.autoSliceTransients();
    else if (id == 3)
        audioProcessor.autoSliceFixed (static_cast<int> (fixedDivisionsSlider.getValue()));

    waveformComponent.repaint();
}
