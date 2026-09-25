# FluidSynth + miniaudio 0.1 proof of concept

Configure with the vcpkg toolchain and the manifest baseline:

~~~powershell
cmake -S . -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" -DOPENHDK_BUILD_TESTS=ON -DVCPKG_APPLOCAL_DEPS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
~~~

The test creates an independently authored middle-C MIDI file and downloads a
pinned, SHA-256-verified test SoundFont. It opens no audio device.

~~~powershell
.\build\OpenHDK.exe --midi-diagnostics C:\Music\demo.mid
.\build\OpenHDK.exe --list-devices
.\build\OpenHDK.exe --midi C:\Music\demo.mid --soundfont C:\SoundFonts\demo.sf2
.\build\OpenHDK.exe --midi C:\Music\demo.mid --soundfont C:\SoundFonts\demo.sf2 --device 0
.\build\OpenHDK.exe --midi C:\Music\demo.mid --soundfont C:\SoundFonts\demo.sf2 --volume 65
.\build\OpenHDK.exe --midi C:\Music\demo.mid --soundfont C:\SoundFonts\demo.sf2 --channel-volume 10:70 --channel-volume 2:85
.\build\OpenHDK.exe --midi C:\Music\demo.mid --soundfont C:\SoundFonts\demo.sf2 --mute
.\build\OpenHDK.exe --midi C:\Music\demo.mid --soundfont C:\SoundFonts\demo.sf2 --no-device
~~~

`--midi-diagnostics <file.mid>` is a standalone read-only path for supported
SMF format 0 and 1 files. It reads the bounded SMF input, uses the same parser,
event decoder, and deterministic compiled timeline as playback, prints all 16
channels, and exits before an audio backend is constructed. It therefore needs
neither a SoundFont nor an audio device. Each channel reports the count of
nonzero-velocity note-ons, distinct observed program numbers in ascending
user-facing 1–128 order, and the final observed CC7 volume in native 0–127
units. An unobserved program or CC7 field is reported as `unavailable`.

`--list-devices` enumerates endpoints without opening a playback stream; an
asterisk marks the default endpoint. In contrast, `--device <index>` selects
that endpoint and opens output. The final `--no-device` command validates
MIDI-to-PCM without hardware.

Current CLI playback reads, parses, and compiles SMF before activating the
backend, then exercises `PlaybackSession` and `SmfMidiEventDispatcher` through
the compiled-timeline entry point. The retained legacy file-player API and
compiled-timeline path cannot control one synth at the same time.
`--channel-volume <1-16>:<0-100>` may be repeated to trim the GM MIDI channel
shown (using the conventional 1–16 numbering; channel 10 is percussion). It
multiplies the SMF's CC7 channel volume, including the default CC7 value of
100, without changing the MIDI file or SoundFont. It applies only to compiled
timeline playback; master `--volume` remains a separate final output gain.
The backend also exposes a zero-based 0–15 runtime API for channel gain, mute,
and solo. It is intentionally not a CLI option or UI yet; a caller publishes
state from its control thread and the audio callback applies it at the next
render-block boundary. `resetRuntimeMixer()` clears all of that runtime state.
This PoC has no implicit or bundled SoundFont fallback: a missing or unloadable
configured SoundFont returns a structured recoverable error. This PoC
does not add Qt, KAR/NCN, HNK/HNK3, MIDI hardware, VST/VST3, or any BASS API.
