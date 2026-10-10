#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Existing local ext4 parent only. Output is a new collector-owned directory.
set -euo pipefail
if [[ $# != 2 ]]; then
  printf 'Usage: bash %s FULL_COMMIT_SHA EXISTING_EXT4_PARENT\n' "$0" >&2
  exit 64
fi
expected="$1"
parent="$(realpath -e -- "$2")"
source_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
[[ "$expected" =~ ^[0-9a-f]{40}$ ]] || exit 64
for required in git g++ python3 findmnt timeout sha256sum mktemp uname; do
  command -v "$required" >/dev/null || { printf 'Missing program: %s\n' "$required" >&2; exit 69; }
done
[[ "$(git -C "$source_root" rev-parse HEAD)" == "$expected" ]] || { printf 'Revision mismatch\n' >&2; exit 65; }
[[ -z "$(git -C "$source_root" status --porcelain --untracked-files=all)" ]] || { printf 'Source tree must be clean\n' >&2; exit 65; }
git -C "$source_root" diff --check
[[ "$(findmnt -n -o FSTYPE -T "$parent")" == ext4 ]] || { printf 'Parent must be ext4; no bypass\n' >&2; exit 69; }
case "$parent/" in "$source_root/"*) printf 'Evidence parent must be outside source tree\n' >&2; exit 64;; esac
out="$(mktemp -d -- "$parent/openhdk-receipts-XXXXXX")"
mkdir -- "$out/bin"
printf 'Evidence directory: %s\n' "$out"
# The EXIT trap records failures too; no cleanup/removal of the collected bundle.
finish() {
  rc=$?
  trap - EXIT
  {
    printf 'REVISION %s\n' "$(git -C "$source_root" rev-parse HEAD)"
    git -C "$source_root" status --porcelain --untracked-files=all
    git -C "$source_root" diff --check
  } > "$out/source-after.log" || { if [[ "$rc" == 0 ]]; then rc=74; fi; }
  printf 'OVERALL exit=%s\n' "$rc" | tee -a "$out/receipts.log"
  (
    cd -- "$out"
    shopt -s nullglob
    logs=(*.log)
    sha256sum -- "${logs[@]}" > SHA256SUMS
  ) || { printf 'Manifest incomplete\n' >&2; if [[ "$rc" == 0 ]]; then rc=74; fi; }
  exit "$rc"
}
trap finish EXIT
flags=(-std=c++20 -Wall -Wextra -Werror -pedantic -O1 -g -pthread -I"$source_root")
sanitizers="${OPENHDK_EVIDENCE_SANITIZERS:-0}"
case "$sanitizers" in
  0) ;;
  1) flags+=(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie) ;;
  *) printf 'Sanitizer setting must be 0 or 1\n' >&2; exit 64 ;;
esac
{
  printf 'REVISION %s\n' "$expected"
  printf 'SOURCE %s\nPARENT %s\n' "$source_root" "$parent"
  printf 'STATUS clean=1\nDIFF_CHECK exit=0\nSANITIZERS %s\n' "$sanitizers"
  uname -a
  g++ --version
  findmnt -T "$parent" -o TARGET,SOURCE,FSTYPE,OPTIONS
  printf 'FLAGS'; printf ' %q' "${flags[@]}"; printf '\n'
  printf 'TIMEOUT compile=180s run=120s\n'
} > "$out/environment.log"
# Preserve actual command exit codes independently of log pipelines.
run_receipt() {
  local kind="$1" suite="$2" log="$3" rc
  shift 3
  if timeout --kill-after=5s "$@" > "$log" 2>&1; then rc=0; else rc=$?; fi
  printf 'EXIT kind=%s suite=%s code=%s\n' "$kind" "$suite" "$rc" | tee -a "$out/receipts.log"
  cat -- "$log"
  return "$rc"
}
export OPENHDK_NATIVE_CHECKPOINT_DIR="$parent"
count=0
for src in "$source_root"/tests/*_tests.cpp; do
  suite="$(basename -- "$src" .cpp)"
  seam=()
  case "$suite" in
    linux_checkpoint_lease_tests|linux_checkpoint_provider_tests|linux_checkpoint_evidence_tests|root_reattachment_tests|catalog_override_display_tests|media_clock_publication_tests|native_durable_service_tests|native_store_binding_tests|durable_library_service_tests)
      seam=(-DOPENHDK_ENABLE_TEST_SEAMS=1) ;;
  esac
  run_receipt compile "$suite" "$out/$suite.compile.log" 180s g++ "${flags[@]}" "${seam[@]}" "$src" -o "$out/bin/$suite"
  run_receipt run "$suite" "$out/$suite.run.log" 120s "$out/bin/$suite"
  count=$((count+1))
done
run_receipt verify native-admission "$out/native-admission-verification.log" 120s python3 "$source_root/tests/verify-native-admission-receipts.py" "$out/native_store_binding_tests.run.log" "$out/native_durable_service_tests.run.log"
{
  printf 'REVISION %s\n' "$(git -C "$source_root" rev-parse HEAD)"
  git -C "$source_root" status --porcelain --untracked-files=all
  git -C "$source_root" diff --check
} > "$out/source-after.log"
[[ -z "$(git -C "$source_root" status --porcelain --untracked-files=all)" ]]
[[ "$(git -C "$source_root" rev-parse HEAD)" == "$expected" ]]
printf 'SUMMARY suites=%s compile_exit=0 run_exit=0 source_clean=1\n' "$count" | tee -a "$out/receipts.log"
