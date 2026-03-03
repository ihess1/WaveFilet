# WaveFilet

A modular VST3/AU sample slicer plugin built with JUCE. Load a `.wav` file, visually slice it into segments, and trigger each segment via MIDI notes or UI interaction.

---

## Features

### Phase 1 — Waveform Display & Manual Slicing (current)
- Load a `.wav` file via drag-and-drop or the "Load File" button
- Visual waveform display using JUCE's `AudioThumbnail`
- **Manual slice mode:** `Ctrl+Click` on the waveform to add a slice marker
- Drag markers to reposition them
- Right-click a marker to delete it
- Slice positions persist in DAW session state

### Phase 2 (planned)
- MIDI note triggering: each slice mapped to a chromatic MIDI note starting at C1
- Click a slice region in the UI to audition it

### Phase 3 (planned)
- Replace individual slice segments with alternate `.wav` files
- Mousewheel scrubbing to offset the start point within a replacement file

### Phase 4 (planned)
- **Mode selector:** Manual markers / Auto transient detection / Fixed equal divisions
- Auto-transient mode with sensitivity slider
- Fixed divisions mode with integer input (max 64)

---

## Building

### Prerequisites
- CMake 3.22+
- A C++17-capable compiler (MSVC 2022+, Clang, GCC)
- VST3 SDK is bundled with JUCE — no separate download needed

### Clone (with submodule)
```bash
git clone <repo-url> WaveFilet
cd WaveFilet
git submodule update --init --recursive
```

### Configure & Build
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

The plugin will be built inside `build/WaveFilet_artefacts/`.

### Debug Build
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug
```

---

## Project Structure

```
WaveFilet/
├── deps/
│   └── JUCE/                     ← JUCE v8.0.12 (git submodule)
├── Source/
│   ├── PluginProcessor.h/.cpp    ← Audio processor: file loading, slice model, state I/O
│   ├── PluginEditor.h/.cpp       ← Top-level plugin UI
│   └── WaveformComponent.h/.cpp  ← Waveform display + slice marker interaction
├── CMakeLists.txt
├── README.md
├── CLAUDE.md                     ← AI assistant context (architecture notes)
└── .gitignore
```

---

## Dependencies

| Dependency | Version | How included |
|---|---|---|
| JUCE | 8.0.12 | Git submodule (`deps/JUCE`) |

---

## License

MIT — see `LICENSE` (to be added).
