# Third-party dependency policy

Do not copy prebuilt binaries or SDK directories into this tree.

A dependency needs: pinned source revision or package version; SPDX license;
bundled-notice requirements; reproducible CMake acquisition; technical
rationale; and an update/removal owner.

FluidSynth and miniaudio are approved audio dependencies. The audio-enabled
build resolves FluidSynth through vcpkg and fetches pinned miniaudio source.
Audio tests also download the pinned Vintage Dreams Waves SoundFont. The
hardware-free core configuration disables audio and fixture acquisition.
RtMidi/libremidi remains deferred.

See [the dependency policy](../docs/DEPENDENCY-POLICY.md) for exact pins and
[third-party notices](../docs/THIRD_PARTY_NOTICES.md) for required notices.
No dependency binary or SoundFont is bundled in the source releases.
