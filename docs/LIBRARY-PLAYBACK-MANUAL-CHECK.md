# Manual listening and clock observation

Status: this interactive procedure is prepared from public source and has
not been executed in full or accepted. A later
[user-attended automated finish probe](LIBRARY-PLAYBACK-LYRIC-EVIDENCE.md#user-attended-synthetic-listening-observation-2026-10-17)
has a maintainer report of brief audible output on the intended headphones,
without observed distortion or interruption. That report does not imply
execution of all commands below or visual lyric rendering.
This is a WSL2 Linux audio-session check. It does not validate native Windows
playback or Windows checkpoint persistence. Keep Step 5 and NCN OPEN, and
Windows checkpoint acknowledgment BLOCKED.

## What this entry can show

[OpenHDKLibraryAudio](../app/library_audio_main.cpp) accepts the launch flags
below. Its [status output](../app/LibraryAudioSession.hpp) shows the observed
cue count and committed clock diagnostics. It does **not** print the lyric
labels or implement text rendering/highlighting. Therefore this procedure
can record audible synthetic output and manual clock/count observations;
it cannot establish visual lyric rendering or a complete karaoke UI.

Use only the existing generated 65-byte synthetic lyric fixture and public
pinned FluidSynth test SoundFont. Do not point discovery at a private media
folder. The 1.5-second last-event fixture is short; missing a Playing sample
during manual interaction is inconclusive, not a defect or a passed
progression check. Last-event time is not a general audio-duration contract.

## Prepare in a WSL terminal

Use the retained executable at the tested PR #110 head. Do not rebuild or
replace its worktree for this procedure. Keep normal speaker/headphone
volume comfortable and select the intended Windows output locally.

Replace the two input paths locally: SF2 is the public pinned
VintageDreamsWaves-v2.sf2 from the tested build, and ROOT is the absolute
directory containing only the generated probe-note.mid. Do not publish these
local paths. Source CMake pins the public SF2 to SHA256
`52132b2b83994f0067d3a66b93b4f1b67d53ff8c0d3be90d39f23f92b4585cdd`.

```bash
OPENHDK_MANUAL_EXE="$HOME/openhdk-wt-step5-status-clock-20261017-131454/build-status-clock/OpenHDKLibraryAudio"
read -r -p "Public pinned SF2 absolute path: " OPENHDK_MANUAL_SF2
read -r -p "Synthetic fixture directory absolute path: " OPENHDK_MANUAL_ROOT
```

Before launch, verify executable/SF2/fixture pins locally against the retained
evidence; stop if anything differs or is missing. Expected executable SHA256:
`44abd6ba6c76cb5210dc015fdd0f2968e874e04b7153c1c8ba298449b29248b5`.
The fixture expectations are in
[the evidence record](LIBRARY-PLAYBACK-LYRIC-EVIDENCE.md#fixture-and-management-preflight).
Do not infer file presence or byte identity from these example commands.

Create a fresh store on WSL ext4. Keep it separate from earlier stores and
do not use a /mnt/c or /mnt/e store for this Linux persistence path.

```bash
OPENHDK_MANUAL_STORE=$(mktemp -d "$HOME/openhdk-listen-XXXXXX")
printf 'Copy this first command into the app: register "%s"\n' "$OPENHDK_MANUAL_ROOT"
"$OPENHDK_MANUAL_EXE" --store-dir "$OPENHDK_MANUAL_STORE" \
  --store-name manual.ohkcat create --soundfont "$OPENHDK_MANUAL_SF2"
OPENHDK_MANUAL_EXIT=$?
printf 'app_exit=%s\n' "$OPENHDK_MANUAL_EXIT"
```

If initialization fails, record that outcome and stop. There is no playback
device selector in this library entry. Do not assume a successful backend
initialization proves that the intended speaker produced audible sound.

## Commands and observations

After the session greeting, paste the printed register command with its
absolute synthetic directory. The app grammar does not expand shell
variables or tilde. Read the returned root ID; use it instead of ROOT-ID:

```text
scan ROOT-ID
songs ROOT-ID 0 100
```

Require one candidate, zero diagnostics and one listed synthetic song.
Use its actual song ID instead of SONG-ID:

```text
prepare SONG-ID
```

Before play, require source_bytes 65, event_count 8, last_event_us 1500000,
cue_count 4, lyric_profile 1, track 0 and encoding utf8. If any value differs,
stop this procedure and record the discrepancy.

Type `play SONG-ID` when ready to listen. Record whether the short synthetic
tone is heard on the intended output, and whether there is any audible
problem. Type `status` during playback if practical, then again after the
tone. Record the actual phase/count/media values. Only an explicit
clock_phase Finished proves that terminal clock observation; elapsed waiting
or active true/false does not. Terminal Finished can still have active true
until explicit stop.

Then type:

```text
stop
status
quit
```

Require stopped acknowledgment, inactive/Unbound status with null generation,
quit acknowledgment and terminal return. With no intentional rejected
commands in this procedure, expected app exit is zero. A PlaybackActive or
other ordinary error changes the retained exit; report it rather than hiding
it. An unexpected hang or missing acknowledgment is not a clean result.

## Report and remaining gates

Report only: audible yes/no/uncertain; intended output reached yes/no;
audible issue if any; observed phase/count/media; stop/quit acknowledgments;
and app exit. Keep original transcripts local pending sanitized export.
Record missing observations as Pending or Inconclusive.

A user report can support only the specific audible/manual observations
actually made. Display text/highlighting, full mutation-verb coverage,
native Windows playback and broader runtime acceptance remain separate.
Source-to-binary correspondence remains not independently proven.

## Relationship to the audio-entry `lyrics` verb

A later implementation adds the exact bare verb `lyrics` to the audio entry:
one opt-in, read-only JSON preview (fields `text` and `text_truncated` beside
the same status fields) of the current observed cue prefix, with fixed caps
and LF/LF-LF plain-text break representation. It did not exist during the
historical runs recorded here, so those observations remain count-only and
stay true as recorded. `lyrics` is still not rendering or highlighting: it
returns bounded text, not a screen layout, and the interactive procedure
above remains the manual path.
