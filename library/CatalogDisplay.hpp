// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 OpenHDK contributors
#pragma once
#include "library/SourceMetadataExtraction.hpp"
#include <new>

namespace OpenHDK {
enum class CatalogDisplayOrigin { UserOverride, SourceTitle, FilenameFallback };
struct CatalogDisplayField {
  std::string text;
  CatalogDisplayOrigin origin;
  std::optional<SourceTitleProvenance> source = std::nullopt;
};
struct CatalogDisplayMetadata {
  CatalogDisplayField title;
  std::optional<CatalogDisplayField> artist = std::nullopt;
};
struct CatalogDisplayResult {
  std::shared_ptr<const CatalogDisplayMetadata> display;
  std::optional<CatalogMetadataError> error;
  bool succeeded() const noexcept { return static_cast<bool>(display) && !error; }
};
inline bool validCatalogMetadataLimits(CatalogMetadataLimits limits) noexcept {
  return limits.textBytes > 0U && limits.textBytes <= MetadataText::kMaxBytes
      && limits.stagedBytes > 0U && limits.stagedBytes <= MetadataPayloadBudget::kMaxBytes;
}
// Pure control-path resolver. Caller charges retained input storage to the same
// ledger first. This helper reserves validation/output coexistence before growth;
// failure leaves the ledger unchanged and returns no partial display object.
inline CatalogDisplayResult resolveCatalogDisplay(std::string_view locator,
    const CatalogSourceMetadata* source, const CatalogUserOverrides* overrides,
    std::size_t textLimit, MetadataPayloadBudget& budget) {
  const auto fail = [](CatalogMetadataErrorCode code, CatalogMetadataField field = CatalogMetadataField::None,
                       std::optional<std::size_t> offset = std::nullopt) {
    return CatalogDisplayResult{nullptr, CatalogMetadataError{code, field, offset}};
  };
  if (textLimit == 0U || textLimit > MetadataText::kMaxBytes)
    return fail(CatalogMetadataErrorCode::InvalidConfiguration);
  auto name = locator.substr(locator.find_last_of('/') == std::string_view::npos
      ? 0U : locator.find_last_of('/') + 1U);
  const auto dot = name.find_last_of('.');
  // A leading dot alone is a filename, not an extension separator.
  if (dot != std::string_view::npos && dot != 0U) name = name.substr(0U, dot);
  auto title = name;
  auto origin = CatalogDisplayOrigin::FilenameFallback;
  std::optional<SourceTitleProvenance> provenance;
  if (overrides && overrides->title) {
    title = overrides->title->bytes(); origin = CatalogDisplayOrigin::UserOverride;
  } else if (source && source->title) {
    title = source->title->text.bytes(); origin = CatalogDisplayOrigin::SourceTitle;
    provenance = source->title->provenance;
  }
  std::optional<std::string_view> artist;
  if (overrides && overrides->artist) artist = overrides->artist->bytes();
  // No source-artist convention is supported. Do not invent one from @T tags.
  auto peak = budget;
  for (auto [text, field] : {std::pair{title, CatalogMetadataField::Title},
                           std::pair{artist.value_or(std::string_view{}), CatalogMetadataField::Artist}}) {
    if (field == CatalogMetadataField::Artist && !artist) continue;
    if (text.size() > textLimit) return fail(CatalogMetadataErrorCode::LimitExceeded, field, textLimit);
    if (!peak.charge(text.size()) || !peak.charge(text.size()))
      return fail(CatalogMetadataErrorCode::LimitExceeded, field);
  }
  try {
    std::shared_ptr<CatalogDisplayMetadata> output;
    {
      const auto validTitle = MetadataText::create(title, textLimit);
      if (!validTitle.succeeded()) return fail(CatalogMetadataErrorCode::InvalidText,
          CatalogMetadataField::Title, validTitle.error()->byteOffset);
      // Release the validation value before validating artist: at most two copies
      // per field coexist, covered conservatively by peak above.
      output = std::make_shared<CatalogDisplayMetadata>(CatalogDisplayMetadata{
          {validTitle.text()->bytes(), origin, provenance}, {}});
    }
    if (artist) {
      const auto validArtist = MetadataText::create(*artist, textLimit);
      if (!validArtist.succeeded()) return fail(CatalogMetadataErrorCode::InvalidText,
          CatalogMetadataField::Artist, validArtist.error()->byteOffset);
      output->artist.emplace(CatalogDisplayField{validArtist.text()->bytes(), CatalogDisplayOrigin::UserOverride, {}});
    }
    auto retained = budget;
    if (!retained.charge(title.size()) || (artist && !retained.charge(artist->size())))
      return fail(CatalogMetadataErrorCode::LimitExceeded);
    budget = retained;
    return {std::move(output), {}};
  } catch (const std::bad_alloc&) { return fail(CatalogMetadataErrorCode::StorageFailure); }
}
} // namespace OpenHDK
