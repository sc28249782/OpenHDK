# v0.2 Windows validation record

## Evidence and revision

Somchai Pongkasem reported these results on 2026-10-06 (Asia/Bangkok).
This is a maintainer-reported device listening check, not an automated
measurement of sound quality.

The tested baseline was PR #31's merge commit
`86ff592aec22e64c58325333d8d1b1a7b3712aa9`. The maintainer confirmed this
local HEAD, an empty `git status --short`, and a successful `git diff --check`.

The test uses external maintainer-owned MIDI and SoundFont assets. No asset
is included in this record or added to the repository. The maintainer
confirmed the pair `B005049.mid` and `SOMSAK2009_2026-BES-ROOM-V2.SF2`.

## Automated test report

Windows CTest passed all nine tests in 1.41 seconds:

- smf-parser-fixtures;
- midi-diagnostics-cli-success;
- runtime-mixer-console;
- midi-velocity-curves;
- midi-mixer-presets;
- stereo-peak-limiter;
- fluidsynth-headless-poc;
- openhdk-cli-help; and
- openhdk-midi-diagnostics-errors.

## Device listening report

| Check | Maintainer-reported result |
| --- | --- |
| Linear, soft, and hard playback | Playback completed without a crash or abnormal sound. |
| Linear balance | Instruments sounded complete and sufficiently loud. |
| Soft balance | Sound was softer; drums were substantially quieter. |
| Recall NoDrums | Drums became silent. |
| Unmute after first recall | Drums returned with the current channel gain of 50. |
| Reset, then recall | The preset remained available and drums became silent again. |
| Preset list | NoDrums appeared in the list. |
| Recall after deletion | A not-found error was shown; playback continued. |
| Quit | The application exited. |

The soft curve applies to positive note-ons on every MIDI channel, including
the drum channel. Its integer mapping lowers middle velocities substantially
(64 becomes 32). The quieter drums are consistent with this global curve;
this report does not establish a defect or change the accepted mapping.
Linear remains the default. Per-channel curve selection or a drum bypass
would require a separate contract and implementation change.

## Limits and remaining release gates

Listening does not measure the limiter ceiling. Deterministic tests check
the linked stereo 0.98 ceiling, non-finite sample handling, stereo balance,
and block-partition invariance. This report adds device-output observations.
It does not establish compatibility with every MIDI file, SoundFont, or device.

Before release preparation is finalized:

- verify both CI workflows on the release preparation PR and exact tag commit;
- align version metadata, specification status, changelog, and roadmap; and
- follow `RELEASE-CHECKLIST.md` for the signed source-only release.

This record does not authorize creating a tag or publishing a release.

The device listening gate passed for the recorded source revision and asset
pair. Release preparation changes metadata and documentation only. The final
tag commit still requires clean-tree, build/test, CI, and signature checks.
