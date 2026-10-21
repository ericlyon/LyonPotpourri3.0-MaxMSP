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

### `el.samm~`
* Inverted the perform loop hierarchy to process samples before streams, eliminating phase desynchronization across outlets upon incoming reset impulses.
* Removed redundant `trigger_vec` and dead `beats` arrays, eliminating `realloc()` allocations during `dsp64` graph compilation.
* Registered `samm_free` in `class_new` and added active `mute` state inspection to guarantee output silence.

### `el.sarec~`
* Prevented audio-thread `SIGSEGV` crashes by validating sample pointers from `buffer_locksamples` and clamping active channels to buffer boundaries.
* Hoisted `buffer_ref_set` and console logging out of `perform64`, eliminating audio-thread lock contention and priority inversion.
* Enforced positive lower bounds on punch-mode crossfading to prevent division-by-zero `NaN` sample corruption.
* Registered `sarec_free` in `class_new` to deallocate the `armed_chans` array and release `recbuf`.

### `el.sel~`
* Fixed a logic flaw where input values equal to `0.0` were bypassed, restoring match functionality for zero-indexed counters.
* Added constructor bounds validation to prevent empty instantiation crashes and out-of-bounds assist tooltip dereferencing.
* Modernized runtime initialization to `ext_main`, standardizing `length` as an integer and vectorizing outlet clearing with `memset`.

### `el.shoehorn~`
* Registered `shoehorn_free` in `class_new` to release spatial panning tables and eradicate persistent heap leaks.
* Eliminated the redundant `inarr` heap buffer and enforced `Z_NO_INPLACE` signal isolation to read inputs in-register.
* Unified output accumulation and vectorized channel clearing with `memset`, resolving downmixing calculation hazards.

### `el.sigseq~`
* Added missing `class_dspinit` call during class registration, resolving the error where inlets failed to bind incoming audio signals.
* Guarded attack, decay, and release envelope stages against division-by-zero, preventing `NaN` sample corruption on zero-duration settings.
* Eradicated the fixed-size `trigger_vec` buffer to prevent heap overruns on signal vector sizes exceeding 2048.
* Registered `sigseq_free` to deallocate the sequence buffer and properly dispose of the clock object.

### `el.splitbank~`
* Fixed tail clearing in the equal and smaller-than-vector paths, which zeroed the channel index instead of the buffer tail and let stale samples pile up in every output.
* Fixed an input copy in the smaller-than-vector path that read past the MSP input vector and overran `input_buffer`.
* Made `scramble`, `recall`, and `setstate` ramp consistently instead of jumping, and guarded the zero-ramp case against `NaN` on the sync outlet.
* Stored `ramptime` in seconds so it survives DSP restarts, and made the `overlap` message update the hop size.
* Added `dsp_free` to `splitbank_free` and fixed leaks of the internal vectors and oscbank structs, so every deleted instance now releases all its memory. Stored slots are now sized for the real FFT, saving about 30 MB per instance.
* Validated `setstate` bin numbers and sanitized `NaN`/inf on signal inlets, preventing out-of-bounds writes and runaway oscillator wrapping.
* Moved DSP re-initialization under a mutex, with perform outputting silence rather than reading half-updated state.
* Persisted `minfreq`/`maxfreq` across DSP restarts instead of resetting them to defaults.

### `el.splitspec~`
* Added thread-safe buffer exchange using `systhread_mutex_trylock` to eliminate audio-thread race conditions during bin scrambling and state recall.
* Fixed an out-of-bounds heap write where `stored_slots` was improperly sized to the vector size rather than the preset slot maximum.
* Corrected teardown leaks by making deallocation unconditional and properly disposing of `phase_clock` and mutex locks.
* Added input sanitization against `NaN`/$\pm\infty$ on table and bin offset inlets, and enforced $[0, N2 - 1]$ bounds checks on `setstate` lists.

### `el.squash~`
* Guarded `squash_squat` against division-by-zero and `log10(0)` domain errors, eliminating `NaN` sample explosions on digital silence.
* Implemented dynamic buffer reallocation and window regeneration in `dsp64` when the signal vector size changes.
* Registered `squash_dsp_free` in `class_new` to deallocate all analysis, synthesis, and overlap-add buffers on object destruction.

### `el.stutter~`
* Fixed an out-of-bounds memory read and write crash when playing mono buffers or running in single-outlet mode.
* Replaced the global `rand_state` with an instance-local 32-bit XorShift generator, eliminating cross-instance race conditions and fixing loop timing on Windows.
* Added missing null-pointer guards for `buffer_locksamples` and protected taper envelopes against division-by-zero.
* Registered `stutter_free` in `class_new` to deallocate loop storage arrays and release `l_buf`.

### `el.vdb~`
* Replaced unsafe audio-thread `outlet_float` invocations with a deferred `t_clock`, eliminating priority inversions and thread deadlocks.
* Hoisted `buffer_ref_set` out of `perform64` and added null-pointer checks for `buffer_locksamples` to prevent audio crashes.
* Registered `vdb_free` in `class_new` to deallocate the `connections` array, `phase_clock`, and buffer reference on deletion.
* Added anti-denormal flushing to feedback accumulation and replaced linear subtraction loops with $O(1)$ modulo wrapping.

### `el.vdel~`
* Fixed a method binding error where `protect` was declared with `A_CANT`, preventing message toggling.
* Standardized no-argument constructor default delay to 50 ms, resolving an unintentional ~19 MB memory allocation.
* Replaced iterative while-loop LFO wrapping with $O(1)$ bitwise masking and branch-free modulo arithmetic.
* Enforced `Z_NO_INPLACE` signal isolation and modernized runtime initialization to `ext_main`.

