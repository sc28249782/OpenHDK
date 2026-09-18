# Third-party dependency policy

Do not copy prebuilt binaries or SDK directories into this tree.

A dependency needs: pinned source revision or package version; SPDX license;
bundled-notice requirements; reproducible CMake acquisition; technical
rationale; and an update/removal owner.

The planned candidates are FluidSynth, miniaudio and RtMidi/libremidi. None is
vendored or downloaded by the bootstrap build.
