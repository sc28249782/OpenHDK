// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 OpenHDK contributors
#pragma once
#include "library/CatalogMetadata.hpp"
#include <algorithm>
#include <memory>
#include <set>

namespace OpenHDK {
// Charge shared compact records once, including when candidate and snapshot
// reference the same object. Locator copies are charged separately by owners.
inline bool chargeSourceMetadata(MetadataPayloadBudget& budget,
    const std::shared_ptr<const CatalogSourceMetadata>& metadata,
    std::set<const CatalogSourceMetadata*>& charged) {
  if (!metadata || charged.contains(metadata.get())) return true;
  const auto bytes = metadata->title ? metadata->title->text.bytes().size() : 0U;
  if (bytes > budget.remaining()) return false;
  charged.insert(metadata.get()); // Allocation failure leaves budget unchanged.
  return budget.charge(bytes);
}
inline bool chargeLyricPayload(MetadataPayloadBudget& budget, const KarLyricTimeline& lyrics) noexcept {
  for (const auto& cue : lyrics.cues()) {
    if (!budget.charge(cue.raw.size()) || !budget.charge(cue.decoded.size())) return false;
  }
  for (const auto& tag : lyrics.metadata()) {
    if (!budget.charge(tag.raw.size()) || !budget.charge(tag.decoded.size())) return false;
  }
  return !lyrics.title() || budget.charge(lyrics.title()->size());
}
struct SourceMetadataResult {
  std::shared_ptr<const CatalogSourceMetadata> metadata;
  std::optional<KarLyricError> error;
};
// Only canonical extraction supplies this helper; no guessing or second pass
// over unselected SMF tracks. Budget includes the caller's live lyric payload.
inline SourceMetadataResult compactSourceMetadata(const KarLyricTimeline& lyrics,
    const SourceRevision& revision, const LyricSelectionPolicy& policy,
    std::size_t titleLimit, MetadataPayloadBudget& budget) {
  const auto fail = [](KarLyricErrorCode code) {
    return SourceMetadataResult{nullptr, KarLyricError{code, {}, {}}};
  };
  CatalogLyricSummary summary{lyrics.profile(), lyrics.trackIndex(), lyrics.cues().size(), policy};
  if (!isValidCatalogLyricSummary(summary)) return fail(KarLyricErrorCode::InvalidConfiguration);
  auto output = std::make_shared<CatalogSourceMetadata>();
  output->kind = VerifiedSourceKind::CanonicalSmf;
  output->lyrics = summary;
  if (lyrics.title()) {
    // Reserve both validation/retained text copies before allocating either.
    auto peak = budget;
    if (!peak.charge(lyrics.title()->size()) || !peak.charge(lyrics.title()->size()))
      return fail(KarLyricErrorCode::LimitExceeded);
    const auto text = MetadataText::create(*lyrics.title(), titleLimit);
    if (!text.succeeded()) return fail(text.error()->code == MetadataTextErrorCode::LimitExceeded
        ? KarLyricErrorCode::LimitExceeded : KarLyricErrorCode::InvalidConfiguration);
    const auto tag = std::find_if(lyrics.metadata().begin(), lyrics.metadata().end(), [](const auto& item) {
      return item.decoded.starts_with("@T") && item.decoded.size() > 2U;
    });
    if (summary.profile != KarLyricProfile::NamedText || tag == lyrics.metadata().end())
      return fail(KarLyricErrorCode::InvalidConfiguration);
    output->title.emplace(SourceTitleMetadata{*text.text(),
        {SourceMemberRole::PrimaryMidi, revision, policy, tag->position}});
    // Caller already charged live extraction payload; this adds only compact
    // retained text. Temporary validation text disappears when this call returns.
    if (!budget.charge(output->title->text.bytes().size())) return fail(KarLyricErrorCode::LimitExceeded);
  }
  return {std::move(output), {}};
}
inline bool validKarLyricLimits(const KarLyricLimits& limits) noexcept {
  return limits.sourceBytes > 0U && limits.sourceBytes <= LyricTextLimits::kMaxSourceBytes
      && limits.cues > 0U && limits.cues <= 100000U
      && limits.titleBytes > 0U && limits.titleBytes <= MetadataText::kMaxBytes
      && limits.stagedBytes > 0U && limits.stagedBytes <= MetadataPayloadBudget::kMaxBytes;
}
} // namespace OpenHDK
