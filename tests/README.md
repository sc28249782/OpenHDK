# Test coverage and failure diagnostics

The hardware-free configuration registers six CTest suites: parser fixtures,
MIDI diagnostics CLI, runtime mixer console, velocity curves, mixer presets,
and stereo peak limiter. The audio-enabled test configuration adds FluidSynth
headless playback, CLI help, and diagnostics error cases, for nine suites.
Both counts describe v0.2.0. Linux core CI runs the six hardware-free suites;
Windows bootstrap CI runs all nine.

## Planned 0.3.0 acceptance

The library and lyric proposals define future test requirements in
[the library contract](../docs/KARAOKE-LIBRARY-CONTRACT.md) and
[the lyric contract](../docs/LYRIC-TIMELINE-CONTRACT.md). They add no CTest
suite yet. Identity/discovery/transaction and encoding/selection/timing tests
must use independently authored fixtures. NCN24 requires a reviewed evidence
supplement before normalizer implementation. No private song or SoundFont
is a repository test asset.

## Failure diagnostics

`stereo-peak-limiter` checks deterministic contract vectors, extreme finite
inputs, NaN/Infinity sanitization, linked stereo balance, unchanged samples
below the ceiling, and equal results across block partitions. The tolerance
for float output and ceiling checks is `1e-6`. Incomplete interleaved frames
fail without changing PCM. The audio smoke test checks bounded finite output
from compiled and legacy playback and a dense maximum-velocity chord.

`midi-mixer-presets` checks name validation, save/overwrite/recall/delete/list,
all-channel flags, revision counts, gain preservation, and existing channel
setters. A control writer and render reader also check that mute/solo masks
remain one complete pair during repeated recall. This is a core-only test;
the console test now covers command argument validation, overwrite, recall,
sorted list, deletion, error reporting, gain preservation, and one revision
per recall. The headless audio smoke test exercises preset mute/recall around
CC7 automation and a later note-on, lifecycle persistence, and legacy rejection.

`midi-velocity-curves` checks the pure velocity helper without audio hardware.
It covers fixed vectors, all positive MIDI velocities with an independent
nearest-integer oracle, monotonicity, velocity zero, and invalid inputs. The
dispatcher tests cover all three curves, zero-velocity note-off, explicit
note-off velocity, adjacent CC7/CC11, program and pitch-bend messages, ignored
pressure events, default linear behavior, invalid curve rejection before
dispatch, and preservation of source event data and times. The audio smoke
test also plays soft and hard compiled sessions and checks legacy rejection.

The C++ test executables use `TestCheck.hpp` to report failed conditions.
`OPENHDK_FAIL_IF(code, condition)` returns `code` from the test function when
`condition` is true. It writes the source file, line, exit code, and condition
to standard error. CTest shows this message with `--output-on-failure`.

For example, a failing MIDI division check reports:

```text
tests/smf_parser_tests.cpp:150: test failed (exit code 1)
  unexpected condition: !expectsSuccess(format0, 0U, 97U, 1U)
```

The line number depends on the source revision. Keep fixture names descriptive
so the reported condition identifies the behavior under test. The diagnostics
CLI test also prints its unexpected output when a channel summary differs.

The helper evaluates the condition once. It preserves short-circuit evaluation
and remains enabled in Release builds with `NDEBUG`. It returns from the test
function, so local objects still run their destructors. Use a nonzero exit code
and call it only from a test function that returns `int`.

To inspect a failure, run CTest after building the tests:

```sh
ctest --test-dir build --output-on-failure
```

A failing test must show its condition and source location. A passing test
keeps its existing exit code of zero. The helper is used only by tests; it is
not part of the audio callback or the production application.
