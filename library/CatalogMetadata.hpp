// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 OpenHDK contributors
#pragma once

#include "library/SourceRevision.hpp"
#include "lyrics/KarLyricExtractor.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace OpenHDK {

// Pure control-path values only. Registration, catalog transactions, extraction
// and display resolution must validate/use these in later integration slices.
enum class LibrarySourceMode { SmfKar };
struct LyricSelectionPolicy {
  LyricTextEncoding encoding = LyricTextEncoding::Utf8;
  std::optional<std::size_t> trackIndex = std::nullopt;
  bool operator==(const LyricSelectionPolicy&) const = default;
};
struct RootSourcePolicy {
  LibrarySourceMode mode = LibrarySourceMode::SmfKar;
  LyricSelectionPolicy lyrics{};
  bool operator==(const RootSourcePolicy&) const = default;
};
constexpr bool isValidLyricSelectionPolicy(const LyricSelectionPolicy& policy) noexcept {
  return policy.encoding == LyricTextEncoding::Utf8 || policy.encoding == LyricTextEncoding::Tis620;
}
constexpr bool isValidRootSourcePolicy(const RootSourcePolicy& policy) noexcept {
  return policy.mode == LibrarySourceMode::SmfKar && isValidLyricSelectionPolicy(policy.lyrics);
}
// Track existence depends on a source; even SIZE_MAX is a valid policy value
// here and must be rejected as InvalidLyricTrack by extraction when out of range.
inline std::optional<KarLyricOptions> karOptionsForPolicy(
    const LyricSelectionPolicy& policy, KarLyricLimits limits = {}) noexcept {
  if (!isValidLyricSelectionPolicy(policy)) return std::nullopt;
  return KarLyricOptions{policy.encoding, policy.trackIndex, limits};
}

enum class MetadataTextErrorCode { InvalidConfiguration, InvalidText, LimitExceeded };
struct MetadataTextError {
  MetadataTextErrorCode code;
  std::size_t byteOffset;
  bool operator==(const MetadataTextError&) const = default;
};
class MetadataTextResult;
class MetadataText {
public:
  static constexpr std::size_t kMaxBytes = 4096U;
  static MetadataTextResult create(std::string_view input, std::size_t byteLimit = kMaxBytes);
  const std::string& bytes() const noexcept { return bytes_; }
  bool operator==(const MetadataText&) const = default;
private:
  explicit MetadataText(std::string bytes) : bytes_(std::move(bytes)) {}
  std::string bytes_;
};
class MetadataTextResult {
public:
  explicit MetadataTextResult(MetadataText text) : text_(std::move(text)) {}
  explicit MetadataTextResult(MetadataTextError error) : error_(error) {}
  bool succeeded() const noexcept { return text_.has_value(); }
  const std::optional<MetadataText>& text() const noexcept { return text_; }
  const std::optional<MetadataTextError>& error() const noexcept { return error_; }
private:
  std::optional<MetadataText> text_;
  std::optional<MetadataTextError> error_;
};
// Nonempty strict UTF-8; reuse the decoder's C0 policy and preserve exact bytes.
// Bounds precede decoding/allocation. No partial text; allocation exceptions
// propagate. These allocating operations MUST NOT run in an audio callback.
inline MetadataTextResult MetadataText::create(std::string_view input, std::size_t byteLimit) {
  if (byteLimit == 0U || byteLimit > kMaxBytes) {
    return MetadataTextResult(MetadataTextError{MetadataTextErrorCode::InvalidConfiguration, 0U});
  }
  if (input.empty()) {
    return MetadataTextResult(MetadataTextError{MetadataTextErrorCode::InvalidText, 0U});
  }
  if (input.size() > byteLimit) {
    return MetadataTextResult(MetadataTextError{MetadataTextErrorCode::LimitExceeded, byteLimit});
  }
  const auto source = std::span<const std::uint8_t>(
      reinterpret_cast<const std::uint8_t*>(input.data()), input.size());
  auto decoded = decodeLyricText(source, LyricTextEncoding::Utf8, {byteLimit, byteLimit});
  if (!decoded.succeeded()) {
    const auto error = *decoded.error();
    // Configuration and bounds were validated above, so only malformed text
    // can fail this UTF-8 path (UTF-8 preservation has no expansion).
    return MetadataTextResult(MetadataTextError{MetadataTextErrorCode::InvalidText, error.byteOffset});
  }
  // Result access is const; take one owned copy. Integration must account for
  // this temporary coexistence in its staging budget, not just retained bytes.
  return MetadataTextResult(MetadataText(*decoded.text()));
}

// A single primary member owns the authoritative content token. No compatibility
// token copy is stored. A future compatibility accessor must derive from here.
enum class SourceMemberRole { PrimaryMidi };
enum class VerifiedSourceKind { Unverified, CanonicalSmf };
struct SourceMemberDescriptor {
  SourceMemberRole role = SourceMemberRole::PrimaryMidi;
  std::string locator;
  std::optional<SourceRevision> revision = std::nullopt;
};
struct CatalogLyricSummary {
  KarLyricProfile profile = KarLyricProfile::NoLyrics;
  std::optional<std::size_t> selectedTrack = std::nullopt;
  std::size_t cueCount = 0U;
  LyricSelectionPolicy effectivePolicy{};
};
constexpr bool isValidCatalogLyricSummary(const CatalogLyricSummary& summary) noexcept {
  if (!isValidLyricSelectionPolicy(summary.effectivePolicy)) return false;
  if (summary.profile == KarLyricProfile::NoLyrics) {
    return !summary.selectedTrack && summary.cueCount == 0U;
  }
  if (summary.profile != KarLyricProfile::LyricMeta && summary.profile != KarLyricProfile::NamedText) {
    return false;
  }
  return summary.selectedTrack.has_value() && summary.cueCount > 0U
      && (!summary.effectivePolicy.trackIndex
          || summary.selectedTrack == summary.effectivePolicy.trackIndex);
}
struct SourceTitleProvenance {
  SourceMemberRole member = SourceMemberRole::PrimaryMidi;
  // Historical evidence for this title, not a separately mutable member token.
  SourceRevision revision;
  LyricSelectionPolicy effectivePolicy;
  LyricSourcePosition position;
};
struct SourceTitleMetadata {
  MetadataText text;
  SourceTitleProvenance provenance;
};
struct CatalogSourceMetadata {
  VerifiedSourceKind kind = VerifiedSourceKind::Unverified;
  std::optional<CatalogLyricSummary> lyrics = std::nullopt;
  std::optional<SourceTitleMetadata> title = std::nullopt;
  // No artist-tag convention is accepted in this slice. No complete lyric
  // payloads/timeline or synthesized filename title are stored in this record.
};
struct CatalogUserOverrides {
  std::optional<MetadataText> title = std::nullopt;
  std::optional<MetadataText> artist = std::nullopt;
  bool operator==(const CatalogUserOverrides&) const = default;
};

// A ledger over a caller's existing catalog payload allowance, not a second
// allowance. Integration must charge every coexisting owned copy, including
// decoder temporaries. Record/candidate and SMF parse bounds remain separate.
class MetadataPayloadBudget {
public:
  static constexpr std::size_t kMaxBytes = 64U * 1024U * 1024U;
  static std::optional<MetadataPayloadBudget> create(
      std::size_t limit = kMaxBytes, std::size_t alreadyOwned = 0U) noexcept {
    if (limit == 0U || limit > kMaxBytes || alreadyOwned > limit) return std::nullopt;
    return MetadataPayloadBudget(limit, alreadyOwned);
  }
  bool charge(std::size_t bytes) noexcept {
    if (bytes > limit_ - used_) return false;
    used_ += bytes;
    return true;
  }
  std::size_t used() const noexcept { return used_; }
  std::size_t remaining() const noexcept { return limit_ - used_; }
private:
  MetadataPayloadBudget(std::size_t limit, std::size_t used) : limit_(limit), used_(used) {}
  std::size_t limit_;
  std::size_t used_;
};

} // namespace OpenHDK
