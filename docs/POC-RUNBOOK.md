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
.\build\OpenHDK.exe --list-devices
.\build\OpenHDK.exe --midi C:\Music\demo.mid --soundfont C:\SoundFonts\demo.sf2
.\build\OpenHDK.exe --midi C:\Music\demo.mid --soundfont C:\SoundFonts\demo.sf2 --device 0
.\build\OpenHDK.exe --midi C:\Music\demo.mid --soundfont C:\SoundFonts\demo.sf2 --volume 65
.\build\OpenHDK.exe --midi C:\Music\demo.mid --soundfont C:\SoundFonts\demo.sf2 --mute
.\build\OpenHDK.exe --midi C:\Music\demo.mid --soundfont C:\SoundFonts\demo.sf2 --no-device
~~~

The device command enumerates endpoints without opening a playback stream; an
asterisk marks the default endpoint. The final command validates MIDI-to-PCM
without hardware. This PoC
does not add Qt, KAR/NCN, HNK/HNK3, MIDI hardware, VST/VST3, or any BASS API.
