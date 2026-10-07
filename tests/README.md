# Test coverage and failure diagnostics

The hardware-free configuration registers eight CTest suites: parser fixtures,
MIDI diagnostics CLI, runtime mixer console, velocity curves, mixer presets,
stereo peak limiter, song catalog core, and filesystem discovery. The audio-enabled test configuration adds
FluidSynth
headless playback, CLI help, and diagnostics error cases, for eleven suites.
These counts describe the development tree; v0.2.0 has six/nine respectively.
Linux core CI runs eight hardware-free suites; Windows bootstrap runs all eleven.

## Planned 0.3.0 acceptance

The accepted library and lyric contracts define target test requirements in
[the library contract](../docs/KARAOKE-LIBRARY-CONTRACT.md) and
[the lyric contract](../docs/LYRIC-TIMELINE-CONTRACT.md).
`song-catalog-core` checks logical IDs, unsigned UTF-8 ordering, strict locators,
ASCII case collisions, state changes on complete scans, rollback including a
late staging failure, boundary limits, relocation conflicts, catalog-only
removal, and old snapshot preservation. It performs no filesystem or media I/O.
`song-discovery` uses isolated synthetic directories and SMFs. It checks pinned
SHA-256 vectors, mixed extension case, UTF-8 paths, root overlap/aliases,
per-file nested parse/compile errors, content changes with unchanged size/time,
complete Missing states, detected-change/cancellation rollback, depth/count/byte
limits, hard links, and no-follow symlink escape reads. Windows additionally
tests sharing-denied files; Linux CI tests source/directory permissions. A local
privileged process skips permission tests, and Windows symlink tests report
unavailable fixtures when the account lacks symlink privilege. Windows reparse
code is compiled and executed in Windows CI; this does not establish coverage
of every junction/mount variant.

Remaining metadata, root reattachment, and encoding/selection/timing tests
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
