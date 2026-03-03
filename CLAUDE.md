# CLAUDE.md — WaveFilet Project Context

This file is the authoritative source of project context for AI assistants. It is kept concise to avoid redundancy with `README.md` and inline code comments.

---

## What This Project Is

A JUCE-based VST3/AU/Standalone sample slicer plugin. See `README.md` for the full feature roadmap.

---

## Architecture

### Key Classes

| Class | File | Role |
|---|---|---|
| `WaveFiletProcessor` | `Source/PluginProcessor.h/.cpp` | AudioProcessor subclass. Owns the loaded audio buffer, the slice position list, and the AudioFormatManager. Handles DAW state persistence. |
| `WaveFiletEditor` | `Source/PluginEditor.h/.cpp` | AudioProcessorEditor subclass. Thin shell: hosts WaveformComponent, Load File button, and filename label. Owns AudioFormatManager and AudioThumbnailCache. |
| `WaveformComponent` | `Source/WaveformComponent.h/.cpp` | The main visual component. Draws the waveform via AudioThumbnail, renders slice markers, and handles all mouse interactions for slicing. |

### Slice Data Model
- Slice positions are stored as a `std::vector<double>` of **normalized values [0.0, 1.0]** in `WaveFiletProcessor`.
- `0.0` = start of sample, `1.0` = end of sample.
- The vector is always kept sorted after any mutation.
- Public API: `addSlice()`, `moveSlice()`, `removeSlice()`, `getSlicePositions()`.

### State Persistence
- `getStateInformation` / `setStateInformation` serialize to XML via JUCE `ValueTree`.
- Persisted: loaded file path + sorted slice positions.
- On `setStateInformation`, the file is reloaded from disk if it still exists.

---

## JUCE Version & Modules

- **JUCE version:** 8.0.12 (commit `501c07674e`)
- **Submodule path:** `deps/JUCE`
- **Modules used:**
  - `juce_audio_utils` → AudioThumbnail, AudioThumbnailCache
  - `juce_audio_formats` → AudioFormatManager, AudioFormatReader, WAV support
  - `juce_audio_processors` → AudioProcessor, AudioProcessorEditor (pulled in transitively by `juce_audio_utils`)

---

## Build System

- **CMake** only (no Projucer).
- Build directory: `build/` (git-ignored).
- Targets: VST3, AU (macOS), Standalone.

---

## Mouse Interaction Contract (WaveformComponent)

| Gesture | Effect |
|---|---|
| `Ctrl + Left-click` (no marker nearby) | Add slice at that position |
| Left-click + drag on marker handle | Move that slice |
| Right-click on marker handle | Delete that slice |
| Other clicks/drags | Reserved for future pan/zoom |

---

## Phase Status

| Phase | Status | Description |
|---|---|---|
| 1 | **In progress** | Project scaffold + waveform display + manual slicing |
| 2 | Planned | MIDI + UI click playback |
| 3 | Planned | Per-slice .wav replacement + mousewheel start-point offset |
| 4 | Planned | Mode selector (manual / transient / fixed divisions) |

---

## Conventions

- Header include order: `<juce_*/juce_*.h>` first, then local headers.
- All JUCE types used with the `juce::` prefix (no `using namespace juce`).
- Slice position type: `double` (normalized). Sample-accurate conversion done at render time.
- No heap allocation on the audio thread.
