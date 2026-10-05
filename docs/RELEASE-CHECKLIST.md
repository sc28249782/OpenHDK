# Release checklist

Use this checklist to publish the OpenHDK `0.1.0` source release. Run the Git
commands from the repository clone in Windows Subsystem for Linux (WSL) that
has the maintainer's GitHub GPG signing key.

## Before merge

- [ ] Confirm that the release pull request changes only version metadata,
  documentation, and release records.
- [ ] Confirm that `git diff --check` reports no error.
- [ ] Confirm that the Linux core and Windows bootstrap workflows pass on the
  release pull request head.
- [ ] Confirm that the `0.1.0` scope in `CHANGELOG.md` matches
  `SPECIFICATION.md` and `ROADMAP.md`.
- [ ] Confirm that `THIRD_PARTY_NOTICES.md` and `DEPENDENCY-POLICY.md` identify
  the dependencies used by the source baseline.
- [ ] Confirm that the manual Windows audio-device validation remains recorded
  in `ROADMAP.md`. Do not add the private MIDI or SoundFont assets.

## After merge

1. Update the local `main` branch without creating a merge commit.

   ```bash
   git switch main
   git pull --ff-only
   ```

2. Confirm that the working tree is clean.

   ```bash
   git status --short
   ```

   The command must not print a tracked or untracked path.

3. Confirm that both required GitHub Actions workflows pass on the exact
   `main` commit that will receive the tag.

4. Create and verify an annotated GPG-signed tag.

   ```bash
   git tag -s v0.1.0 -m "OpenHDK v0.1.0"
   git tag -v v0.1.0
   ```

   Verification must report a good signature from the maintainer's expected
   signing key.

5. Push only the verified tag.

   ```bash
   git push origin v0.1.0
   ```

6. Create a GitHub Release from `v0.1.0`. Use the `0.1.0` section of
   `CHANGELOG.md` as the release notes.

7. Publish the release as source-only. Do not attach a prebuilt binary, private
   MIDI file, private SoundFont, or the test SoundFont as a release asset.

8. Verify that GitHub shows the signed tag, source archives, GPL-3.0-or-later
   license, and required third-party notices.

## Stop conditions

Do not create or push the tag if a required workflow fails, the tag signature
cannot be verified, the release commit differs from the tested commit, or the
working tree is not clean. Correct the problem through a reviewed pull request,
then run this checklist again.
