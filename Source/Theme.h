#pragma once

#include <juce_graphics/juce_graphics.h>

/**
 * Centralised colour palette for WaveFilet.
 * All paint methods must reference these constants instead of raw 0xffRRGGBB literals.
 */
namespace Theme
{
    inline const juce::Colour background      { 0xff16213e };
    inline const juce::Colour waveformBg      { 0xff1a1a2e };
    inline const juce::Colour waveform        { 0xff4ecdc4 };
    inline const juce::Colour sliceLine       { 0xffff6b6b };
    inline const juce::Colour sliceHandle     { 0xffffd166 };
    inline const juce::Colour sliceHandleRing { 0x80000000 };
    inline const juce::Colour dragOverFill    { 0x26ffffff };
    inline const juce::Colour dragOverBorder  { 0xccffffff };
    inline const juce::Colour noFileText      { 0x66ffffff };
    inline const juce::Colour playhead        { 0xffffffff };
    inline const juce::Colour playheadActive  { 0xff06d6a0 };
    inline const juce::Colour offsetMarker    { 0xfff9c74f };
    inline const juce::Colour customSliceFlag { 0xff90e0ef };
}
