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
| `WaveFiletEditor` | `Source/PluginEditor.h/.cpp` | AudioProcessorEditor subclass. Thin shell: hosts WaveformComponent, Load File button, and filename label. Owns AudioThumbnailCache. Shares the processor's AudioFormatManager (does not own its own). |
| `WaveformComponent` | `Source/WaveformComponent.h/.cpp` | The main visual component. Draws the waveform via AudioThumbnail, renders slice markers, and handles all mouse interactions for slicing. |

### Slice Data Model
- Slice positions are stored as a `std::vector<double>` of **normalized values [0.0, 1.0]** in `WaveFiletProcessor`.
- `0.0` = start of sample, `1.0` = end of sample.
- The vector is always kept sorted after any mutation.
- `slicePositions` is cleared on every `loadFile()` call — old markers do not persist across file loads.
- Public API: `addSlice()`, `moveSlice()`, `removeSlice()`, `getSlicePositions()`.
- **⚠ Thread Safety (Issue #1):** `slicePositions` is currently only safe to access on the message thread. Before Phase 2 (audio thread reads in `processBlock`), this must be replaced with a lock-free double-buffer snapshot. See `docs/code-review-2026-03-03.md` and GitHub Issue #1.
- **⚠ MIDI Identity (Issue #8):** The slice-to-MIDI note mapping model (sort-order vs. creation-order) has not yet been decided. This is a required design decision before Phase 2 starts. See GitHub Issue #8.

### Audio Buffer
- `audioBuffer` is sized to `fileLength + kInterpolationGuardSamples` (currently 4). `fileLength` does NOT include the guard samples — playback code must never iterate beyond `fileLength`.
- **⚠ Thread Safety (Issue #2):** `audioBuffer` is written on the message thread and will be read on the audio thread in Phase 2. Requires an atomic buffer-swap pattern and a background loading thread. See GitHub Issue #2.

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
| 1 | **Complete (pending PR merge)** | Project scaffold + waveform display + manual slicing. Branch `review/phase1-findings` has pending fixes. |
| 2 | **Blocked** | MIDI + UI click playback. Blocked on GitHub Issues #1, #2, #8 (thread safety + MIDI identity design). |
| 3 | Planned | Per-slice .wav replacement + mousewheel start-point offset |
| 4 | Planned | Mode selector (manual / transient / fixed divisions) |

## Open Issues (GitHub)

Full details: <https://github.com/ihess1/WaveFilet/issues>

| # | Priority | Title | Blocks |
|---|---|---|---|
| [#1](https://github.com/ihess1/WaveFilet/issues/1) | **P0** | `slicePositions` lock-free audio-thread snapshot | Phase 2 |
| [#2](https://github.com/ihess1/WaveFilet/issues/2) | **P0** | `audioBuffer` data race between `loadFile()` and `processBlock()` | Phase 2 |
| [#3](https://github.com/ihess1/WaveFilet/issues/3) | P1 | File I/O synchronous on message thread (UI freeze) | Phase 2 |
| [#4](https://github.com/ihess1/WaveFilet/issues/4) | P2 | Hit-test only covers handle circle, not marker line | Phase 2 |
| [#5](https://github.com/ihess1/WaveFilet/issues/5) | P2 | Deprecated `juce::Font(float)` constructor | — |
| [#6](https://github.com/ihess1/WaveFilet/issues/6) | P3 | Plugin codes identical and unregistered with Steinberg | Distribution |
| [#7](https://github.com/ihess1/WaveFilet/issues/7) | P3 | Hardcoded color literals (needs `Theme.h`) | Phase 4 |
| [#8](https://github.com/ihess1/WaveFilet/issues/8) | P2 | Slice-to-MIDI identity model undecided | Phase 2 |

---

## Conventions

- Header include order: `<juce_*/juce_*.h>` first, then local headers.
- All JUCE types used with the `juce::` prefix (no `using namespace juce`).
- Slice position type: `double` (normalized). Sample-accurate conversion done at render time.
- No heap allocation on the audio thread.
- `AudioFormatManager` is owned by `WaveFiletProcessor`. The editor and component share it via `processor.getFormatManager()` — do not create additional instances.

## Review History

| Date | Document | Summary |
|---|---|---|
| 2026-03-03 | [`docs/code-review-2026-03-03.md`](docs/code-review-2026-03-03.md) | Full Phase 1 code review. 6 mechanical fixes applied on `review/phase1-findings` branch. 8 GitHub issues filed. |
