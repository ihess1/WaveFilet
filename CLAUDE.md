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
| `WaveFiletProcessor` | `Source/PluginProcessor.h/.cpp` | AudioProcessor subclass. Owns the audio double-buffer, the slice list, the FileLoadingThread, and the AudioFormatManager. Handles DAW state persistence. Also a ChangeBroadcaster — broadcasts when a new file finishes loading. |
| `WaveFiletEditor` | `Source/PluginEditor.h/.cpp` | AudioProcessorEditor subclass. Thin shell: hosts WaveformComponent, Load File button, and filename label. Owns AudioThumbnailCache. Shares the processor's AudioFormatManager (does not own its own). |
| `WaveformComponent` | `Source/WaveformComponent.h/.cpp` | The main visual component. Draws the waveform via AudioThumbnail, renders slice markers, and handles all mouse interactions for slicing. ChangeListener for both the AudioThumbnail and the processor. |
| `Theme` | `Source/Theme.h` | Namespace of named `inline const juce::Colour` constants. All paint methods use these instead of hardcoded 0xffRRGGBB literals. |

### Slice Data Model

- Slices are stored as `std::vector<WaveFiletProcessor::Slice>` (sorted by `position`) in `WaveFiletProcessor`.
- Each `Slice` has two fields:
  - `position` — normalised `double` in `[0.0, 1.0]`. `0.0` = file start, `1.0` = file end.
  - `id` — stable `int` assigned at creation time via a monotonically-incrementing counter (`nextSliceId`). Never changes after creation, never reused after deletion.
- **MIDI identity model (Issue #8 — resolved):** `id` maps directly to a MIDI note (`kBaseMidiNote + id`). This is the **creation-order** model used by hardware samplers (Akai MPC, Roland SP-404). Dragging a slice past another marker changes the sort index but never the MIDI note.
- `slices` is cleared on every `loadFile()` call — old markers do not persist across file loads.
- Public API: `addSlice()`, `moveSlice()`, `removeSlice()`, `getSlices()`.
- **Thread Safety (Issue #1 — resolved):** The UI-owned `slices` vector is message-thread-only. After every mutation `publishSliceSnapshot()` copies it into one of two pre-allocated `SliceSnapshot` buffers and atomically swaps the pointer via `audioThreadSnapshot`. The audio thread reads through this atomic pointer with no locks and no heap allocation.

### Audio Buffer (Issues #2 and #3 — resolved)

- The processor keeps two `AudioData` slots (`audioSlots[0]` and `audioSlots[1]`).
- `std::atomic<int> activeAudioSlot` tells the audio thread which slot to read from.
- File decoding happens on `FileLoadingThread` (a `juce::Thread` subclass defined in `PluginProcessor.cpp`). When decoding is complete it posts the result to the message thread via `MessageManager::callAsync`, which calls `onFileLoaded()`.
- `onFileLoaded()` writes the new audio into the *inactive* slot, then atomically promotes it — no lock on the audio thread, no UI freeze.
- `AudioData::kInterpolationGuardSamples = 4`: extra samples allocated beyond `AudioData::length` for future interpolation headroom. Playback code must never iterate beyond `length`.
- Message-thread metadata mirrors (`messageSampleRate`, `messageFileLength`, `currentFile`, `audioLoaded`) are updated in `onFileLoaded()` and are safe to read without touching the atomic slot index.

### State Persistence

- `getStateInformation` / `setStateInformation` serialize to XML via JUCE `ValueTree`.
- Persisted: loaded file path, `nextSliceId` counter, and each slice's `position` + `id`.
- On `setStateInformation`, the file is reloaded from disk (background thread) and slices are restored with their original `id` values so MIDI-note assignments survive a session round-trip.
- Backward compatibility: old save files without the `id` attribute use the slice's list index as a fallback id.

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
- `PLUGIN_MANUFACTURER_CODE Ihss` / `PLUGIN_CODE WfSl` — placeholder codes; register with Steinberg before distribution (Issue #6).

---

## Mouse Interaction Contract (WaveformComponent)

| Gesture | Effect |
|---|---|
| `Ctrl + Left-click` (not near a marker) | Add slice at that position |
| Left-click + drag on any part of a marker | Move that slice |
| Right-click on any part of a marker | Delete that slice |
| Other clicks/drags | Reserved for future pan/zoom |

The hit-test covers both the small drag-handle circle at the top *and* the full vertical line body (within `hitTolerance = 8 px` horizontally). See `WaveformComponent::getSliceHandleAt()`.

---

## Phase Status

| Phase | Status | Description |
|---|---|---|
| 1 | **Complete** | Project scaffold + waveform display + manual slicing. All Phase 1 issues resolved. |
| 2 | **Ready to start** | MIDI + UI click playback. All blocking issues resolved (see below). |
| 3 | Planned | Per-slice .wav replacement + mousewheel start-point offset |
| 4 | Planned | Mode selector (manual / transient / fixed divisions) |

## Open Issues (GitHub)

Full details: <https://github.com/ihess1/WaveFilet/issues>

| # | Priority | Title | Status |
|---|---|---|---|
| [#1](https://github.com/ihess1/WaveFilet/issues/1) | **P0** | `slicePositions` lock-free audio-thread snapshot | ✅ Resolved — `SliceSnapshot` double-buffer + `audioThreadSnapshot` atomic |
| [#2](https://github.com/ihess1/WaveFilet/issues/2) | **P0** | `audioBuffer` data race between `loadFile()` and `processBlock()` | ✅ Resolved — `AudioData` double-buffer + `activeAudioSlot` atomic |
| [#3](https://github.com/ihess1/WaveFilet/issues/3) | P1 | File I/O synchronous on message thread (UI freeze) | ✅ Resolved — `FileLoadingThread` decodes on background thread |
| [#4](https://github.com/ihess1/WaveFilet/issues/4) | P2 | Hit-test only covers handle circle, not marker line | ✅ Resolved — `getSliceHandleAt` tests full line body |
| [#5](https://github.com/ihess1/WaveFilet/issues/5) | P2 | Deprecated `juce::Font(float)` constructor | ✅ Resolved — replaced with `juce::Font(juce::FontOptions{}.withHeight(...))` |
| [#6](https://github.com/ihess1/WaveFilet/issues/6) | P3 | Plugin codes identical and unregistered with Steinberg | ✅ Resolved — `Ihss` / `WfSl` (register before distribution) |
| [#7](https://github.com/ihess1/WaveFilet/issues/7) | P3 | Hardcoded color literals (needs `Theme.h`) | ✅ Resolved — `Source/Theme.h` namespace, all paint methods updated |
| [#8](https://github.com/ihess1/WaveFilet/issues/8) | P2 | Slice-to-MIDI identity model undecided | ✅ Resolved — creation-order model; `Slice::id` carries stable MIDI identity |

---

## Conventions

- Header include order: `<juce_*/juce_*.h>` first, then local headers.
- All JUCE types used with the `juce::` prefix (no `using namespace juce`).
- Slice position type: `double` (normalised). Sample-accurate conversion done at render time.
- No heap allocation on the audio thread.
- `AudioFormatManager` is owned by `WaveFiletProcessor`. The editor and component share it via `processor.getFormatManager()` — do not create additional instances.
- All colour literals belong in `Source/Theme.h` — do not add raw `0xffRRGGBB` values to paint methods.

## Review History

| Date | Document | Summary |
|---|---|---|
| 2026-03-03 | [`docs/code-review-2026-03-03.md`](docs/code-review-2026-03-03.md) | Full Phase 1 code review. 6 mechanical fixes applied on `review/phase1-findings` branch. 8 GitHub issues filed. |
| 2026-03-06 | *(this commit)* | All 8 issues resolved. Architecture upgraded to production-ready thread-safety baseline. Phase 2 unblocked. |
