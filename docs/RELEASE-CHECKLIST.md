# Release checklist

Use this checklist to publish an OpenHDK source release. Replace `<version>`
with the release version without the leading `v`. Run the Git commands from the
repository clone in Windows Subsystem for Linux (WSL) that has the maintainer's
GitHub GPG signing key.

The `v0.1.0` source release completed this procedure on 2026-10-05. Its release
record remains in `CHANGELOG.md` and in the GitHub Release; do not reopen or
edit that historical changelog section for later work.

## Before merge

- [ ] Confirm that the `<version>` release pull request changes only version metadata,
  documentation, and release records.
- [ ] Confirm that `git diff --check` reports no error.
- [ ] Confirm that the Linux core and Windows bootstrap workflows pass on the
  release pull request head.
- [ ] Confirm that the `<version>` scope in `CHANGELOG.md` matches
  `SPECIFICATION.md` and `ROADMAP.md`.
- [ ] Confirm that `THIRD_PARTY_NOTICES.md` and `DEPENDENCY-POLICY.md` identify
  the dependencies used by the source baseline.
- [ ] Confirm that the manual Windows audio-device validation remains recorded
  in `ROADMAP.md`. Do not add the private MIDI or SoundFont assets.
- [ ] For v0.2.0, confirm the revision and asset identification in
  `V020-WINDOWS-ACCEPTANCE.md`; preserve its tested revision and reported scope.

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
   `main` commit that will receive the tag. Rebuild and run the applicable
   local CTest suites from that commit as well. Preserve the manual listening
   record's source revision; if runtime behavior changed after that check,
   repeat device listening before tagging.

4. Create and verify an annotated GPG-signed tag.

   ```bash
   git tag -s v<version> -m "OpenHDK v<version>"
   git tag -v v<version>
   ```

   Verification must report a good signature from the maintainer's expected
   signing key.

5. Push only the verified tag.

   ```bash
   git push origin v<version>
   ```

6. Create a GitHub Release from `v<version>`. Use the `<version>` section of
   `CHANGELOG.md` as the release notes.

7. Publish the release as source-only. Do not attach a prebuilt binary, private
   MIDI file, private SoundFont, or the test SoundFont as a release asset.

8. Verify that GitHub shows the signed tag, source archives, GPL-3.0-or-later
   license, and required third-party notices.

## WSL signing recovery

If GPG cannot open its pinentry from the active WSL terminal, set the terminal
for the GPG agent before creating the tag:

```bash
export GPG_TTY="$(tty)"
gpg-connect-agent updatestartuptty /bye
```

Retry the signing command, then verify the tag before pushing it.

## Stop conditions

Do not create or push the tag if a required workflow fails, the tag signature
cannot be verified, the release commit differs from the tested commit, or the
working tree is not clean. Correct the problem through a reviewed pull request,
then run this checklist again.
