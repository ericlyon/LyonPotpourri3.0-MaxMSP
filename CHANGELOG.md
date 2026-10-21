# LyonPotpourri 3.0 — Modernization Changelog

The following externals from the LyonPotpourri package have been updated to support the Cycling '74 Max 8/9 SDK. This release focuses on stability, 64-bit performance, memory safety, and thread-boundary compliance.

---

### `el.adsr~`
* Fixed a fatal crash in `perform64` where the audio thread would enter an infinite loop and corrupt heap memory if the object was muted.
* Resolved division-by-zero errors occurring with 0ms stage times by implementing reciprocal multiplication.
* Corrected off-by-one errors in assist string inlet definitions.
* Optimized DSP performance by pre-calculating segment sample multipliers.

### `el.bashfest~`
* Eliminated fatal out-of-bounds array access in `bashfest_deploy_dsp` when grabbing buffer data.
* Fixed buffer locking race conditions by ensuring symmetric unlocking across all error branches.
* Optimized DSP performance by replacing real-time string lookups (`buffer_ref_set`) in the perform loop with direct object pointer access.
* Implemented anti-denormal flushing macros (`FLUSH_DENORM`) on all recursive feedback taps to prevent CPU spikes.
* Corrected multiple integer truncation and indexing bugs within the DSP effect chain.

### `el.buffet~`
* Fixed critical buffer-locking leaks where early returns left samples locked, causing system-wide audio deadlocks.
* Implemented bounds checking for `A_GIMME` messages to prevent garbage heap reads.
* Fixed an undefined behavior bug in `buffet_rotatetozero` where `sysmem_copyptr` was used for overlapping memory shifts; updated to `memmove`.
* Fixed a double-locking typo in `buffet_copy_to_mono_buffer` that previously caused NULL pointer dereferences.

### `el.buffi~`
* Fixed fatal in-place signal buffer aliasing by adding `Z_NO_INPLACE`, preventing the input signal vector from being zeroed before the interpolation phase was read.
* Standardized 64-bit memory management and corrected destructor logic to release all temporary function buffers on object deletion.
* Removed audio-thread heap allocations.

### `el.channel~`
* Resolved out-of-bounds heap reads by strictly bounding channel index lookups to the current vector size (`vectorsize`) rather than an arbitrary max.
* Added mandatory output zeroing when the gate is inactive or indices are out of range to prevent passing dirty uninitialized memory downstream.

### `el.click2float~`
* Resolved a fatal crash on object deletion by implementing a proper destructor (`click2float_free`) to cancel pending scheduler clocks before memory is released.
* Migrated from scheduler `clock` to `t_qelem` for thread-safe float emission, ensuring compatibility with Overdrive and preventing UI thread race conditions.
* Optimized DSP graph efficiency by bypassing perform routines for disconnected inlets.

### `el.convolver~`
* Fixed multi-megabyte heap leaks in the destructor by recursively deallocating impulse and source buffers.
* Resolved a critical crash in `convolver_static_memory` where allocated buffers were assigned to local pointers instead of object struct fields.
* Corrected lock-leak logic in `convolvechans` to guarantee buffer unlocking on all early-return paths.

### `el.counter~`
* Fixed the uninitialized state bug where the first trigger produced a zero-value output instead of the minimum count.
* Added `Z_NO_INPLACE` to prevent audio vector aliasing.
* Enforced input bounds checking to prevent undefined behavior on rapid parameter changes.

### `el.clean_gate~`
* Resolved a fatal crash caused by missing `return` statements in the mute branch, which previously allowed execution to fall through into invalid memory loops.
* Fixed division-by-zero potential in crossfade calculations when using 0ms fade times.
* Corrected broken logic where the `linear` flag was ignored in the perform loop.

### `el.clean_selector~`
* Prevented stack and heap overruns by bounding signal-connection lookups to the object's actual inlet count.
* Fixed a severe memory leak in `dsp_free` by correctly registering the destructor and releasing the `connected_list` array.
* Resolved `last_chan` dereference crashes when signal inputs were invalid or disconnected.

### `el.expflam~`
* Fixed memory corruption in `perform64` caused by using `sizeof(float)` instead of `sizeof(t_double)` in `memcpy` operations.
* Corrected heap memory leaks by implementing `expflam_free` and removing legacy C standard library `malloc`/`calloc` calls.
* Guarded against negative frame-count allocations in `memset` calls during flam pattern generation.

### `el.granule~`
* Resolved memory leaks by correctly registering `granule_dsp_free` and releasing grain arrays/pitch grids on destruction.
* Fixed a critical buffer-bound crash in `granule_perform64` caused by unvalidated null pointer access to waveform wavetables.
* Prevented CPU-locking infinite loops when empty buffers were provided as inputs.

### `el.granulesf~`
* Added support for both mono and stereo wavetables.
* Fixed memory corruption caused by invalid stereo frame boundary lookups.
* Removed high-priority audio thread console logging (`error()`) to prevent priority inversion.
* Implemented `notify` callbacks to correctly handle external buffer changes/resizes.

### `el.killdc~`
* Resolved dead code issues and removed unimplemented prototypes.
* Integrated anti-denormal protection inside the filter recursion loop to prevent performance spikes during quiet signal passages.

### `el.latch~`
* Fixed the sample-and-hold trigger bug where the object failed to update from Inlet 1 due to improper proxy management.
* Added missing cleanup for internal state and enforced signal inlet isolation.

### `el.magfreq_analysis~`
* Eliminated massive memory leaks by registering the destructor and properly deallocating all 15 spectral analysis workbuffers.
* Fixed an out-of-bounds heap access in `perform64` when the signal vector size exceeded the FFT bin count.
* Re-enabled FFT size limiting to prevent buffer overruns during complex spectral analysis.

### `el.markov~`
* Fixed a fatal constructor bug where incorrect parsing of `A_GIMME` arguments forced custom pattern lengths to a default of 16.
* Resolved a heap corruption vulnerability in `markov_new` where dynamic row allocation failed to scale with `event_count`.
* Patched an out-of-bounds write in `perform64` occurring when the `manual_override` logic failed to terminate execution, causing an underflow loop.

### `el.multigrain~`
* Resolved memory leaks by implementing full recursive deallocation of dynamic grain, pitchscale, and buffer objects.
* Added new `maxskip` and `minskip` attributes for user-definable range control.
* Enabled support for stereo soundfile buffers.
* Added `mixmode` attribute to control mono/stereo routing behavior.

### `el.npan~`
* Fixed the panner wrap-around bug where an input of `1.0` abruptly snapped to speaker 0 instead of the final channel.
* Guarded out-of-bounds pointer accesses when performing multi-channel signal routing.

### `el.oscil~`
* Fixed destructor leaks by ensuring `old_wavetable` and `wavetable64` are properly released.
* Fixed a state-reset bug in `dsp64` where the oscillator phase was forced to `0.0` on every graph recompilation, causing audible clicks.
* Standardized all wavetable data to 64-bit `t_double` to prevent precision loss.

### `el.phasemod~`
* Resolved a crash in the `manual_override` branch caused by a missing `return`, which triggered heap memory corruption.
* Added safety guards to prevent division-by-zero and log/exp domain collapse during phase modulation calculations.
* Properly registered destructor for memory cleanup.

### `el.quadpanin~`
* Fixed an asymmetric coordinate clamping bug where `xval > 1.0` was omitted while `yval` was checked redundantly, preventing panning distortion on out-of-bounds inputs.
* Corrected `Z_NO_INPLACE` assignment to use bitwise OR, preserving default DSP setup flags.
* Modernized runtime initialization to `ext_main` with 64-bit `dsp64` optimizations and registered a dedicated destructor for object teardown.

### `el.rotapan~`
* Guarded against zero-channel initialization to prevent `SIGFPE` divide-by-zero crashes during panning offset modulo calculations.
* Replaced discontinuous threshold snapping on the pan controller with continuous modular `fmod` wrapping, eliminating audio clicks during modulation sweeps.
* Eradicated the redundant heap-allocated `inarr` buffer and registered `rotapan_free` for zero-leak teardown.

### `el.rtrig~`
* Replaced global `rand()`/`srand()` calls with a thread-safe, instance-local 32-bit XorShift PRNG, eliminating cross-thread contention and fixing complete trigger failure on Windows.
* Connected `mute` state inspection inside `rtrig_perform64` to enforce true output silence when enabled.
* Modernized runtime initialization to `ext_main`, standardizing all control messages to 64-bit double precision.
