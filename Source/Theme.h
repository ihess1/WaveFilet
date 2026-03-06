#pragma once

#include <juce_graphics/juce_graphics.h>

/**
 * @file Theme.h
 * @brief Centralised colour palette for WaveFilet.
 *
 * All paint methods reference these named constants rather than scattering
 * 0xffRRGGBB literals throughout the codebase.  Collecting them here makes
 * a future "light/dark" or "mode-selector" theme switch (Phase 4) a one-file
 * change instead of a grep-and-replace across every Component.
 *
 * Colours are declared as `inline const` (C++17) so each translation unit
 * gets the same object without requiring a separate .cpp definition.
 *
 * Naming convention:
 *   <element>_<role>   e.g. sliceMarker_line, editor_background
 */
namespace Theme
{
    // =========================================================================
    // Editor shell

    /** Deep-navy background that fills WaveFiletEditor. */
    inline const juce::Colour editor_background    { 0xff16213e };

    // =========================================================================
    // WaveformComponent

    /** Near-black indigo that fills the waveform canvas. */
    inline const juce::Colour waveform_background  { 0xff1a1a2e };

    /** Teal fill used when drawing the waveform channels. */
    inline const juce::Colour waveform_fill        { 0xff4ecdc4};

    // =========================================================================
    // Slice markers

    /** Coral-red vertical line drawn for each slice marker. */
    inline const juce::Colour sliceMarker_line     { 0xffff6b6b };

    /** Amber fill for the circular drag-handle at the top of each marker. */
    inline const juce::Colour sliceMarker_handle   { 0xffffd166 };

} // namespace Theme
