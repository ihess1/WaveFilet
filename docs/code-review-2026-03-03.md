# Code Review — Phase 1 — 2026-03-03

**Branch:** `review/phase1-findings`
**Reviewer:** Claude Code (Staff SWE)
**Scope:** Full review of master at commit `c2b4760` (init scaffold)
**Files reviewed:** `PluginProcessor.{h,cpp}`, `PluginEditor.{h,cpp}`, `WaveformComponent.{h,cpp}`, `CMakeLists.txt`

---

## Actions Taken

### Fixes Applied (`review/phase1-findings` branch)

Six mechanical fixes were committed to the branch. The branch is pushed and ready to merge.

| Issue | File(s) | Fix Summary |
|---|---|---|
| **#3 (partial)** | `WaveformComponent.{h,cpp}` | Removed dead `dragStartNorm` member and its assignment in `mouseDown`. Replaced misleading comment in `mouseDrag` with a clear explanation of why nearest-index re-acquisition is correct. |
| **#4** | `WaveformComponent.{h,cpp}`, `PluginEditor.cpp` | Added `WaveformComponent::restoreFromProcessor()` which rebuilds the `AudioThumbnail` from the processor's already-loaded file. Called from `WaveFiletEditor` constructor after session restore, fixing the blank waveform bug. |
| **#6** | `PluginEditor.{h,cpp}` | Removed the duplicate `juce::AudioFormatManager` from `WaveFiletEditor`. The editor now passes `p.getFormatManager()` to `WaveformComponent`, consolidating format registration in the processor. |
| **#7** | `PluginProcessor.cpp` | Added `slicePositions.clear()` at the top of `loadFile()` so markers from a previous file do not silently persist onto a new file's timeline. |
| **#8** | `PluginProcessor.cpp` | Replaced magic literal `+4` buffer guard with named constant `kInterpolationGuardSamples = 4` and a comment explaining the purpose and the deliberate asymmetry between `fileLength` and actual buffer size. |
| **#10** | `CMakeLists.txt` | Added `CMAKE_CXX_STANDARD 17`, `CMAKE_CXX_STANDARD_REQUIRED ON`, `CMAKE_CXX_EXTENSIONS OFF`. |

### GitHub Issues Filed

All architectural findings that require design discussion are tracked as GitHub Issues.

| Issue | Priority | Title |
|---|---|---|
| [#1](https://github.com/ihess1/WaveFilet/issues/1) | **P0 / Phase 2 blocker** | `slicePositions` requires lock-free audio-thread snapshot |
| [#2](https://github.com/ihess1/WaveFilet/issues/2) | **P0 / Phase 2 blocker** | `audioBuffer` / `fileSampleRate` data race between `loadFile()` and `processBlock()` |
| [#3](https://github.com/ihess1/WaveFilet/issues/3) | P1 | File I/O is synchronous on the message thread (UI freeze / DAW watchdog) |
| [#4](https://github.com/ihess1/WaveFilet/issues/4) | P2 | Slice marker hit-test only covers handle circle, not marker line body |
| [#5](https://github.com/ihess1/WaveFilet/issues/5) | P2 | Deprecated `juce::Font(float)` constructor in `PluginEditor.cpp` |
| [#6](https://github.com/ihess1/WaveFilet/issues/6) | P3 | `PLUGIN_MANUFACTURER_CODE` and `PLUGIN_CODE` are identical and unregistered with Steinberg |
| [#7](https://github.com/ihess1/WaveFilet/issues/7) | P3 | Hardcoded color literals should be named theme constants |
| [#8](https://github.com/ihess1/WaveFilet/issues/8) | P2 | No slice-to-MIDI identity model — sort order changes MIDI mapping when markers are dragged |

---

## Full Findings (all severities)

### [CRITICAL] Thread Safety — `slicePositions`

**Issue #1**

`slicePositions` is a `std::vector<double>` written on the message thread (`addSlice`, `moveSlice`, `removeSlice`) and will be read on the audio thread by `processBlock()` in Phase 2. This is undefined behavior in C++ — a data race that causes memory corruption and DAW crashes.

**Required fix:** Lock-free double-buffer snapshot. UI thread writes to an inactive `std::array<double, 64>` buffer and atomically swaps the pointer. Audio thread reads through the atomic pointer with no lock and zero heap allocation.

```
// Conceptual model:
snapshotA / snapshotB  ← two pre-allocated fixed-size buffers (no heap on audio thread)
audioThreadSnapshot    ← std::atomic<SliceSnapshot*>, swapped on every UI-side mutation
```

### [CRITICAL] Thread Safety — `audioBuffer` / `fileSampleRate`

**Issue #2**

`loadFile()` calls `audioBuffer.setSize()` (malloc/free) and writes `fileSampleRate` / `fileLength` on the message thread. `processBlock()` will read all three on the audio thread in Phase 2. A concurrent `setSize()` while the audio thread holds a read pointer is a crash.

**Required fix:** Background file loading thread + atomic buffer swap with a "loading" silence flag.

### [MAJOR] Architecture — Drag Index Stability

**Issue #3 (partially fixed in branch)**

`moveSlice()` calls `std::sort` after every position update. The `draggedSliceIndex` stored in `WaveformComponent` becomes stale after the sort. The recovery heuristic (find nearest slice to cursor) is correct in most cases because the dragged slice is always at exactly the cursor position after the move. Dead code (`dragStartNorm`) was removed.

**Remaining concern:** The sort-during-drag approach re-sorts on every mouse move event (~60/sec). For small slice counts this is fine, but sorting on `mouseUp` only would be cleaner and eliminate the index re-acquisition entirely. See also Issue #8 (MIDI identity model), which may necessitate a data model change anyway.

### [MAJOR] Architecture — Session Restore Blank Waveform

**Issue #4 — Fixed in branch**

`setStateInformation()` calls `loadFile()` which populates `audioBuffer` but never updates the `AudioThumbnail`. The thumbnail is only populated via `setSource()` which is only reachable through user interaction. Fixed by adding `restoreFromProcessor()` and calling it from the editor constructor.

### [MAJOR] Architecture — Synchronous File I/O on Message Thread

**Issue #3 (GitHub)**

`WaveformComponent::setSource()` → `processor.loadFile()` decodes the entire audio file synchronously on the message thread. For a 10-minute 192 kHz stereo file (~880 MB), this freezes the UI for a noticeable duration. Some DAW hosts (Ableton Live) will kill a plugin that blocks the message thread.

**Required fix:** `juce::Thread` subclass for file decoding. After decoding, swap the buffer into the processor via `MessageManager::callAsync`. This also resolves Issue #2 (thread safety).

### [MODERATE] Duplicate `AudioFormatManager`

**Issue #6 — Fixed in branch**

Both `WaveFiletProcessor` and `WaveFiletEditor` owned a `juce::AudioFormatManager` with `registerBasicFormats()` called in both. Fixed by removing the editor's copy; it now passes `processor.getFormatManager()` to `WaveformComponent`.

### [MODERATE] Loading New File Retains Old Slices

**Issue #7 — Fixed in branch**

`loadFile()` did not clear `slicePositions`. Old marker positions (meaningful relative to the previous file's duration) silently persisted onto the new file's timeline. Fixed with `slicePositions.clear()` at the top of `loadFile()`.

### [MODERATE] Buffer Guard Magic Number

**Issue #8 — Fixed in branch**

`audioBuffer` was allocated with `maxSamples + 4` and `fileLength = maxSamples`. The `+4` guard is for future interpolation headroom but was undocumented. Replaced with `kInterpolationGuardSamples = 4` and a comment explaining the asymmetry.

### [MODERATE] Hit-Test Covers Only Handle Circle

**Issue #4 (GitHub)**

`getSliceHandleAt()` performs a circular hit-test around a small circle at the very top of the component (`y = handleRadius`). Clicking anywhere along the vertical marker line body returns `-1`. Right-clicking the line to delete does nothing. For Phase 2's click-to-audition feature, a fundamentally different hit model is needed (region between two markers, not a point on a handle).

### [MINOR] CMakeLists — No C++ Standard Declaration

**Issue #10 — Fixed in branch**

`CMAKE_CXX_STANDARD 17` was not explicitly set. JUCE enforces it transitively but explicit declaration is better practice and required by some IDEs/linters.

### [MINOR] Deprecated `juce::Font(float)` Constructor

**Issue #5 (GitHub)**

`fileLabel.setFont(juce::Font(13.0f))` uses the JUCE 8 deprecated float constructor. Fix:
```cpp
fileLabel.setFont(juce::Font(juce::FontOptions{}.withHeight(13.0f)));
```

### [MINOR] `PLUGIN_MANUFACTURER_CODE` = `PLUGIN_CODE` = `Wflt`

**Issue #6 (GitHub)**

Both codes are `Wflt`. The manufacturer code must be distinct from the plugin code and should be registered with Steinberg before distribution. No runtime impact.

### [MINOR] Hardcoded Color Literals

**Issue #7 (GitHub)**

Five `0xffRRGGBB` literals scattered across paint methods. Should be moved to a `Source/Theme.h` namespace before Phase 4 (mode selector), which will require conditional coloring.

### [DESIGN] Slice-to-MIDI Identity Model

**Issue #8 (GitHub) — Design decision required before Phase 2**

`slicePositions` is always sorted. When the user drags a marker past another, its sort index changes. If MIDI mapping is index-based (slice 0 → C1), moving a marker changes which note triggers it, silently invalidating any recorded MIDI pattern.

**Options:**
- **Sort-order mapping** (current implicit): simple, but recorded patterns break on marker moves.
- **Creation-order mapping**: stable IDs per slice, independent of position sort. Requires `struct Slice { double pos; int id; }` and a data model change throughout. Most samplers use this (Akai MPC, Roland SP).
- **User-assigned mapping**: explicit UI binding. Most flexible, most complex.

This must be decided before Phase 2 implementation. It affects the data model, state persistence XML schema, and the public API surface.

---

## Pre-Phase 2 Checklist

Before writing any MIDI playback code:

- [ ] Resolve Issue #1 — lock-free `slicePositions` snapshot
- [ ] Resolve Issue #2 — atomic buffer swap + background loading thread (also resolves Issue #3)
- [ ] Decide Issue #8 — slice-to-MIDI identity model (document decision in CLAUDE.md)
- [ ] Resolve Issue #4 — hit-test model (needed for click-to-audition in Phase 2)
- [ ] Merge `review/phase1-findings` branch

---

*Review conducted 2026-03-03. Reviewer: Claude Code (Sonnet 4.6).*
