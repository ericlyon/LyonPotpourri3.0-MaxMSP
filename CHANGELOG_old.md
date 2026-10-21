# LyonPotpourri 3.0 — Max 8/9 Maintenance & Stability Release

This release brings modern macOS toolchain support, critical stability fixes, multi-threading safety, and memory leak resolutions to key externals in the **LyonPotpourri 3.0** collection for Cycling '74 Max 8 and Max 9.

---

## 🚀 Highlights

* **Modern Apple Silicon & macOS Support:** Upgraded build targets to **macOS 12.0+ Universal 2 (`arm64` and `x86_64`)**, ensuring native compatibility with modern Xcode toolchains.
* **Zero-Leak Teardowns:** Resolved persistent memory leaks where custom object destructors were bypassed in `class_new()`.
* **Audio-Thread Hardening:** Eliminated UI and console calls from high-priority audio perform loops (`perform64`), preventing thread priority inversions and audio stutters.
* **Buffer Safety & Memory Integrity:** Plugged buffer sample lock leaks and resolved boundary overruns across soundfile and wavetable operations.

---

## 📦 Object-by-Object Changes

### `el.click2float~`
* **Thread-Safe Event Emission:** Replaced the legacy scheduler `clock` with a deferred **`t_qelem`**. Clicks are now output safely on the Main (UI) thread, preventing race conditions and crashes when connected directly to UI objects.
* **Destructor Teardown:** Implemented a dedicated `click2float_free()` destructor to decouple from the DSP chain and cancel/free pending `qelem` handles upon patch deletion.
* **DSP Optimization:** Guarded `dsp64` routine with `count[0]` to bypass execution when the signal inlet is unconnected.

### `el.buffet~`
* **Buffer Lock Leak Fixes:** Symmetrically paired `buffer_locksamples()` with `buffer_unlocksamples()` across all error and early-return paths in `events`, `pevents`, `specswap`, `retroblock`, `copy_to_buffer`, and `erase`.
* **64-bit DSP Sync Inlet:** Configured signal inlet 0 via `dsp_setup()` to safely read groove sync signals without dereferencing non-existent signal outlets.
* **Memory Shift Safety:** Replaced undefined self-referential `sysmem_copyptr` calls with standard ISO C `memmove()` inside `buffet_rotatetozero`.
* **Mono Copy Validation:** Fixed a pointer validation typo in `copy_to_mono_buffer` where source samples were checked twice while destination samples were left unchecked.
* **Trimzero Logic:** Refined `trimzero` edge-case handling to safely detect leading/trailing silence and consistently fire the completion bang on both active operations and no-ops.
* **Message Boundary Checks:** Added strict `argc` validation across all `A_GIMME` methods to prevent invalid memory reads on empty or truncated messages.

### `el.granule~`
* **Destructor Leak Fix:** Registered `granule_dsp_free` in `class_new()` to properly free grain structures, pitch grids, and `buffer_ref` instances upon deletion.
* **Window Buffer Validation:** Fixed a copy-paste bug in `granule_setbuf()` where the window buffer was improperly validated against the wavetable pointer, preventing null-dereference crashes on invalid buffer names.
* **Zero-Frame / Empty Buffer Protection:** Added bounds checks to eliminate infinite phase-wrapping loops and null pointer crashes when empty buffers are loaded.
* **Dynamic Buffer Tracking:** Added `granule_notify()` to ensure reloaded, resized, or swapped buffers update dynamically without stale pointer crashes.
* **Mute Handling:** Formally exposed the `mute` message in `main()` and wired it into `perform64` to bypass audio processing with 0% CPU consumption when inactive.

### `el.granulesf~`
* **Destructor Registration:** Registered `granulesf_dsp_free` in `class_new()`, eliminating leaks of grain allocations, pitch scales, and buffer references on object destruction.
* **Guarded Cleanup:** Resolved a critical null-pointer crash in `perform64` by ensuring `buffer_unlocksamples()` is never called on null buffer handles.
* **Stereo Boundary Overrun Fix:** Corrected tail-boundary checks on stereo buffers (`(b_frames - 1) * 2`), preventing reads past the end of the heap allocation during final-sample frame interpolation.
* **Audio-Thread Optimization:** Removed dynamic `buffer_ref_set()` symbol lookups from `perform64` and eliminated audio-thread `error()` console logging inside the sample loop.
* **Retro Odds Bug Fix:** Fixed a logic bug where `retro_odds` was hardcoded to 0.0, restoring user-controlled reverse grain playback.
* **Dynamic Notification & Muting:** Added `granulesf_notify()` and implemented `mute` handling in the audio loop.

---

## 🛠 Build System Updates

* **CMake Deployment Target:** Updated root and target `MACOSX_DEPLOYMENT_TARGET` properties from obsolete `10.11`/`10.12` to `12.0` to resolve build errors on modern Apple Clang / Xcode toolchains.
* **Clean Universal Builds:** Configured standard target properties across all `.mxo` bundles for universal deployment across Intel (`x86_64`) and Apple Silicon (`arm64`).

---

## 📋 Compatibility

* **Host Environment:** Cycling '74 Max 8.x / Max 9.x
* **Operating Systems:**
  * macOS 12.0 (Monterey) through current macOS releases (Intel & Apple Silicon native)
  * Windows 10 / 11 (64-bit)