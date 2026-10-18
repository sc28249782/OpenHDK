// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 OpenHDK contributors
#pragma once
#include "app/LibraryCliOutput.hpp"
#include "lyrics/KarLyricExtractor.hpp"
#include <span>
#include <string>
#include <string_view>

namespace OpenHDK {

// Pure control-path reconstruction of a PLAIN-TEXT preview from the current
// owning observed cue prefix (controller.observed()), for the audio entry's
// opt-in `lyrics` query. This is not a screen layout contract: Text operations
// take the exact decoded byte range (no inferred spaces, no normalized raw
// markers), LineBreak emits LF and ParagraphBreak emits LF LF. Contract limits
// are logical application bounds charged to the caller's existing
// kCallerBytes-style allowance; they are not a process-memory claim. The
// returned text is what a single bounded output buffer retained; rendering or
// highlighting remains a separate, unimplemented adapter concern.
struct LyricObservationTextLimits {
  std::size_t maxOutputBytes = 4096U;
  std::size_t maxCues = 1024U;
  std::size_t maxOperations = 4096U;
};

// Fixed contract caps: callers may LOWER a limit but never raise it above
// these values, and a zero or above-maximum limit is invalid (rejected out
// of band with no scanning and no allocation). Staying inside them keeps the
// total work (cues, operations and output bytes) bounded regardless of any
// caller mutation.
inline constexpr std::size_t kLyricObservationMaxOutputBytes = 4096U;
inline constexpr std::size_t kLyricObservationMaxCues = 1024U;
inline constexpr std::size_t kLyricObservationMaxOperations = 4096U;

[[nodiscard]] inline bool lyricObservationLimitsValid(
    const LyricObservationTextLimits& limits) noexcept {
  return limits.maxOutputBytes > 0U
      && limits.maxOutputBytes <= kLyricObservationMaxOutputBytes
      && limits.maxCues > 0U && limits.maxCues <= kLyricObservationMaxCues
      && limits.maxOperations > 0U
      && limits.maxOperations <= kLyricObservationMaxOperations;
}

struct LyricObservationTextResult {
  // Always valid UTF-8 on success (clipped at a scalar boundary);
  // empty on failure, where failed() is true and no text is returned.
  std::string text;
  bool truncated = false;
  bool failed = false;
};

[[nodiscard]] inline LyricObservationTextResult lyricObservationText(
    std::span<const KarLyricCue> cues,
    const LyricObservationTextLimits& limits = {}) {
  LyricObservationTextResult result;
  // Invalid limits are rejected OUT OF BAND: no scanning and no allocation.
  if (!lyricObservationLimitsValid(limits)) {
    result.failed = true;
    return result;
  }
  std::string& out = result.text;
  const auto have_room = [&](std::size_t bytes) {
    return limits.maxOutputBytes - out.size() >= bytes;
  };
  // Append up to `bytes`, clipped at a UTF-8 scalar boundary. Returns false
  // when the output cap forces this append to be the last one.
  const auto append = [&](std::string_view bytes) {
    if (bytes.empty()) return true;
    if (!have_room(1U)) { result.truncated = true; return false; }
    const auto room = limits.maxOutputBytes - out.size();
    const auto take = bytes.size() <= room ? bytes.size() : room;
    out.append(bytes.data(), take);
    if (take < bytes.size()) {
      // Back off to a whole scalar so Thai (and any other multi-byte scalar)
      // is never split: walk back to the start of the last scalar and drop
      // it when its sequence was cut short by the cap.
      while (!out.empty()) {
        std::size_t i = out.size();
        while (i > 0U && (static_cast<unsigned char>(out[i - 1U]) & 0xC0U)
            == 0x80U) --i;
        if (i == 0U) { out.clear(); result.truncated = true; return false; }
        const auto lead = static_cast<unsigned char>(out[i - 1U]);
        if ((lead & 0x80U) == 0U) break; // complete ASCII
        const std::size_t need = (lead & 0xE0U) == 0xC0U ? 2U
            : (lead & 0xF0U) == 0xE0U ? 3U : 4U;
        if (out.size() - i == need - 1U) break; // complete sequence
        out.resize(i - 1U); // drop the incomplete scalar (lead + continuations)
      }
      result.truncated = true;
      return false;
    }
    return true;
  };
  std::size_t visited = 0U; // cue index
  std::size_t visitedOperations = 0U;
  for (; visited < cues.size(); ++visited) {
    if (visited >= limits.maxCues) {
      // Cue cap: unvisited cues still exist; truncated iff any remain.
      result.truncated = true;
      break;
    }
    const auto& cue = cues[visited];
    for (const auto& operation : cue.operations) {
      if (visitedOperations >= limits.maxOperations) {
        result.truncated = true;
        goto done; // stop traversal immediately on the cap
      }
      ++visitedOperations;
      switch (operation.kind) {
      case LyricDisplayKind::Text: {
        // Defensive subtraction-bound checks: an internally inconsistent
        // operation can never read past its owning cue's decoded text.
        if (operation.byteOffset > cue.decoded.size()
            || operation.byteLength > cue.decoded.size() - operation.byteOffset) {
          result.failed = true;
          result.text.clear();
          return result;
        }
        if (!append(std::string_view(cue.decoded).substr(
                operation.byteOffset, operation.byteLength))) goto done;
        break;
      }
      case LyricDisplayKind::LineBreak:
        if (!append("\n")) goto done;
        break;
      case LyricDisplayKind::ParagraphBreak:
        if (!append("\n\n")) goto done;
        break;
      default:
        // Unknown kind: defensive fail, never emit guessed bytes.
        result.failed = true;
        result.text.clear();
        return result;
      }
    }
  }
done:
  result.truncated = result.truncated || visited < cues.size();
  // The observed cues carry arbitrary decoded payloads assembled by kinds and
  // offsets; the helper validates ONLY the assembled bounded preview (at most
  // 4096 bytes above) as UTF-8 with the shared CLI validator. This is not a
  // timeline validator: whole cues beyond the caps go unvalidated by design.
  if (!result.failed && !libraryCliUtf8(result.text)) {
    result.failed = true;
    result.text.clear();
    result.truncated = false;
  }
  return result;
}

} // namespace OpenHDK