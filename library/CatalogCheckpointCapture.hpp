// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "library/CatalogCheckpointCodec.hpp"
#include "library/SongDiscovery.hpp"

namespace OpenHDK {
// Acquired by the adapter only. Owns the exact catalog context and detached
// projection together; public callers cannot change this acquired value.
class CapturedCatalogCheckpoint {
 public:
  const CatalogCheckpointProjection& projection() const noexcept { return projection_; }
  std::shared_ptr<const CatalogSnapshot> snapshot() const noexcept { return snapshot_; }
 private:
  CapturedCatalogCheckpoint(std::shared_ptr<const CatalogSnapshot> snapshot,
      CatalogCheckpointProjection projection)
      : snapshot_(std::move(snapshot)), projection_(std::move(projection)) {}
  std::shared_ptr<const CatalogSnapshot> snapshot_;
  CatalogCheckpointProjection projection_;
  friend class CatalogCheckpointCapture;
  friend class DurableLibraryService;
};
struct CatalogCaptureResult {
  std::shared_ptr<const CapturedCatalogCheckpoint> captured;
  std::optional<CheckpointError> error;
  bool succeeded() const noexcept { return captured != nullptr && !error; }
};

class CatalogCheckpointCapture {
 public:
  // Serialized control-path acquisition from one owner. No arbitrary snapshot
  // argument: counters and bindings are captured with the current owner snapshot,
  // never borrowed later from a newer generation. No filesystem probing occurs.
  // sequence=1 is a codec-valid placeholder; the store admits its own sequence.
  static CatalogCaptureResult acquire(const SongDiscovery& owner,
      CatalogCheckpointLimits limits = {}, std::size_t alreadyOwned = 0U) {
    using namespace CheckpointDetail;
    try {
      if (!validLimits(limits)) fail(CheckpointErrorCode::InvalidConfiguration, CheckpointField::None);
      const auto snapshot = owner.catalog_.snapshot();
      if (snapshot->roots.size() > limits.roots || snapshot->songs.size() > limits.songs)
        fail(CheckpointErrorCode::LimitExceeded, CheckpointField::Header);
      if (owner.roots_.size() != snapshot->roots.size())
        fail(CheckpointErrorCode::InvalidCheckpoint, CheckpointField::Generation);
      auto budget = MetadataPayloadBudget::create(limits.stagedBytes, alreadyOwned);
      if (!budget || !chargeCatalogPayload(*budget, *snapshot))
        fail(CheckpointErrorCode::LimitExceeded, CheckpointField::None);
      std::size_t largest = 0U;
      const auto charge = [&](std::size_t n, CheckpointField field) {
        if (!budget->charge(n)) fail(CheckpointErrorCode::LimitExceeded, field);
      };
      const auto output = [&](std::size_t n, CheckpointField field) {
        if (n > limits.textBytes) fail(CheckpointErrorCode::LimitExceeded, field);
        charge(n, field); largest = std::max(largest, n);
      };
      // Preflight all coexisting owner/snapshot payload and output text before
      // projection copies. Metadata records shared in the snapshot are charged
      // once by identity; serialized per-song override copies are charged per row.
      for (const auto& root : owner.roots_) {
        const auto units = root.path.native().size();
        constexpr auto unitBytes = sizeof(std::filesystem::path::value_type);
        if (units > CatalogCheckpointLimits::kBytes / unitBytes)
          fail(CheckpointErrorCode::LimitExceeded, CheckpointField::Hint);
        charge(units * unitBytes, CheckpointField::Hint);
        if (root.savedHint) charge(root.savedHint->size(), CheckpointField::Hint);
        if (root.attached) output(hintBound(root.path), CheckpointField::Hint);
        else if (root.savedHint) output(root.savedHint->size(), CheckpointField::Hint);
      }
      for (const auto& song : snapshot->songs) {
        output(song.locator().size(), CheckpointField::Locator);
        if (song.overrides) {
          if (song.overrides->title) output(song.overrides->title->bytes().size(), CheckpointField::Title);
          if (song.overrides->artist) output(song.overrides->artist->bytes().size(), CheckpointField::Artist);
        }
      }
      // Path UTF-8 conversion and validation temporaries share this allowance.
      // Windows reserves up to three UTF-8 bytes per UTF-16 unit; this deliberately
      // conservative preflight can reject a boundary even if actual UTF-8 fits.
      charge(largest, CheckpointField::None); charge(largest, CheckpointField::None);
      CatalogCheckpointProjection projection;
      projection.catalogRevision = snapshot->revision;
      projection.nextRoot = owner.catalog_.nextRoot_;
      projection.nextSong = owner.catalog_.nextSong_;
      projection.roots.reserve(snapshot->roots.size());
      projection.songs.reserve(snapshot->songs.size());
      for (const auto& root : snapshot->roots) {
        const auto binding = std::find_if(owner.roots_.begin(), owner.roots_.end(),
            [&](const auto& item) { return item.id == root.id; });
        if (binding == owner.roots_.end())
          fail(CheckpointErrorCode::InvalidCheckpoint, CheckpointField::Generation);
        auto hint = binding->attached ? std::optional<std::string>(LibraryFilesystem::utf8(binding->path))
                                      : binding->savedHint;
        projection.roots.push_back({root.id.value_, root.attachmentGeneration, root.policy, std::move(hint)});
      }
      for (const auto& song : snapshot->songs) {
        CheckpointSong row{song.id.value_, song.root.value_, song.member.role, song.locator(), {}, {}};
        if (song.overrides) {
          if (song.overrides->title) row.title = song.overrides->title->bytes();
          if (song.overrides->artist) row.artist = song.overrides->artist->bytes();
        }
        projection.songs.push_back(std::move(row));
      }
      // Enforce wire capacity and complete schema semantics without creating wire.
      (void)shape(projection, limits);
      validate(projection, limits);
      auto acquired = std::shared_ptr<const CapturedCatalogCheckpoint>(
          new CapturedCatalogCheckpoint(snapshot, std::move(projection)));
      return {std::move(acquired), {}};
    } catch (const Failure& e) {
      return {nullptr, CheckpointError{e.code, CheckpointOperation::Capture, e.field, e.record, e.offset}};
    } catch (const std::bad_alloc&) {
      return {nullptr, CheckpointError{CheckpointErrorCode::StorageFailure, CheckpointOperation::Capture, {}, {}, {}}};
    } catch (const std::filesystem::filesystem_error&) {
      return {nullptr, CheckpointError{CheckpointErrorCode::UnsupportedRepresentation, CheckpointOperation::Capture, CheckpointField::Hint, {}, {}}};
    }
  }
 private:
  static std::size_t hintBound(const std::filesystem::path& path) {
    const auto units = path.native().size();
#ifdef _WIN32
    constexpr std::size_t expansion = 3U;
#else
    constexpr std::size_t expansion = 1U;
#endif
    if (units > CatalogCheckpointLimits::kBytes / expansion)
      CheckpointDetail::fail(CheckpointErrorCode::LimitExceeded, CheckpointField::Hint);
    return units * expansion;
  }
};
} // namespace OpenHDK
