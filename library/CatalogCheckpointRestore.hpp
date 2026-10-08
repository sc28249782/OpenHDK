// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "library/CatalogCheckpointCodec.hpp"
#include "library/SongDiscovery.hpp"

namespace OpenHDK {
struct CatalogRestoreResult {
  std::unique_ptr<SongDiscovery> owner;
  std::optional<CheckpointError> error;
  bool succeeded() const noexcept { return owner != nullptr && !error; }
};

// Private catalog member: called only after complete projection validation.
// Numeric-ID construction remains inside SongCatalog. All imported values are
// detached from source/Ready authority and get the newly constructed owner's
// private lineage. No previous owner or acquired snapshot is replaced.
inline void SongCatalog::restoreCheckpointProjection(const CatalogCheckpointProjection& p) {
  auto staged = std::make_shared<CatalogSnapshot>();
  staged->origin_ = snapshot_->origin_;
  staged->revision = p.catalogRevision;
  staged->roots.reserve(p.roots.size()); staged->songs.reserve(p.songs.size());
  for (const auto& r : p.roots) staged->roots.push_back({RootId(r.id), r.attachmentGeneration, r.policy});
  for (const auto& r : p.songs) {
    std::shared_ptr<const CatalogUserOverrides> overrides;
    if (r.title || r.artist) {
      auto record = std::make_shared<CatalogUserOverrides>();
      if (r.title) record->title = *MetadataText::create(*r.title).text();
      if (r.artist) record->artist = *MetadataText::create(*r.artist).text();
      overrides = std::move(record);
    }
    staged->songs.push_back({SongId(r.id), RootId(r.root),
        {SourceMemberRole::PrimaryMidi, r.locator, {}}, CatalogState::Invalid, nullptr, std::move(overrides)});
  }
  // Preserve allocator high water, including removed-ID gaps and exhaustion.
  snapshot_ = std::move(staged); nextRoot_ = p.nextRoot; nextSong_ = p.nextSong;
}

class CatalogCheckpointRestore {
 public:
  // Pure factory. Validates again even when given caller-built detached values.
  // Caller-owned projection plus all restored strings and validation/MetadataText
  // copies share one ledger. No filesystem probing, native path conversion,
  // source scan, callback, audio operation or durability acknowledgment occurs.
  static CatalogRestoreResult fromProjection(const CatalogCheckpointProjection& p,
      CatalogCheckpointLimits limits = {}, std::size_t alreadyOwned = 0U) {
    using namespace CheckpointDetail;
    try {
      if (!validLimits(limits)) fail(CheckpointErrorCode::InvalidConfiguration, CheckpointField::None);
      const auto s = shape(p, limits);
      std::size_t total = 0U;
      add(total, alreadyOwned, limits.stagedBytes);
      add(total, s.strings, limits.stagedBytes); // Retained projection input.
      add(total, s.strings, limits.stagedBytes); // New locators, hints and overrides.
      // MetadataText::create may coexist with decoder and result/record copies.
      // Fixed descriptors are count-bounded; this is logical byte accounting.
      for (unsigned copy = 0U; copy < 3U; ++copy) add(total, s.largest, limits.stagedBytes);
      validate(p, limits);
      auto owner = std::make_unique<SongDiscovery>();
      owner->catalog_.restoreCheckpointProjection(p);
      owner->roots_.reserve(p.roots.size());
      const auto snapshot = owner->catalog_.snapshot();
      for (std::size_t i = 0U; i < p.roots.size(); ++i) {
        owner->roots_.push_back({snapshot->roots[i].id, {}, false, p.roots[i].hint});
      }
      return {std::move(owner), {}};
    } catch (const Failure& e) {
      return {nullptr, CheckpointError{e.code, CheckpointOperation::Restore, e.field, e.record, e.offset}};
    } catch (const std::bad_alloc&) {
      return {nullptr, CheckpointError{CheckpointErrorCode::StorageFailure, CheckpointOperation::Restore, {}, {}, {}}};
    }
  }

  // Input bytes are data, never a filename. Decode and restore retain the same
  // operation allowance: the wire remains charged while projection and owner
  // coexist. Decode errors retain their precise Decode operation/offset.
  static CatalogRestoreResult fromBytes(std::span<const std::uint8_t> bytes,
      CatalogCheckpointLimits limits = {}, std::size_t alreadyOwned = 0U) {
    auto decoded = decodeCatalogCheckpoint(bytes, limits, alreadyOwned);
    if (!decoded.succeeded()) return {nullptr, decoded.error};
    std::size_t retained = alreadyOwned;
    try {
      CheckpointDetail::add(retained, bytes.size(), limits.stagedBytes);
    } catch (const CheckpointDetail::Failure& e) {
      return {nullptr, CheckpointError{e.code, CheckpointOperation::Restore, e.field, e.record, e.offset}};
    }
    return fromProjection(*decoded.value, limits, retained);
  }
};
} // namespace OpenHDK
