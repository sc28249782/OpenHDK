// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "library/CatalogDisplay.hpp"
#include <algorithm>
#include <compare>
#include <cstdint>
#include <limits>
#include <memory>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace OpenHDK {

// Library-owned identifiers; no path/content conversion is available.
class SongCatalog;
class CatalogCheckpointRestore;
class CatalogCheckpointCapture;
struct CatalogCheckpointProjection;
class RootId {
 public:
  auto operator<=>(const RootId&) const = default;
 private:
  explicit RootId(std::uint64_t value) : value_(value) {}
  std::uint64_t value_;
  friend class SongCatalog;
  friend class CatalogCheckpointCapture;
};
class SongId {
 public:
  auto operator<=>(const SongId&) const = default;
 private:
  explicit SongId(std::uint64_t value) : value_(value) {}
  std::uint64_t value_;
  friend class SongCatalog;
  friend class CatalogCheckpointCapture;
};

enum class CatalogState { Ready, Invalid, Missing, UnsupportedProfile };
enum class CatalogError {
  None, UnknownRoot, InvalidLocator, InvalidCandidate, AmbiguousPath, IncompleteScan,
  LimitExceeded, NotFound, IdExhausted
};
struct CatalogLimits {
  std::size_t candidates = 10000U;
  std::size_t locatorBytes = 4096U;
  // Historical name: bounds all staged locator and metadata payload bytes.
  std::size_t stagedLocatorBytes = 64U * 1048576U;
};
struct CatalogCandidate {
  std::string locator;
  CatalogState state = CatalogState::Ready;
  std::optional<SourceRevision> revision = std::nullopt;
  std::shared_ptr<const CatalogSourceMetadata> metadata = nullptr;
};
struct CatalogSong {
  SongId id;
  RootId root;
  SourceMemberDescriptor member;
  CatalogState state;
  std::shared_ptr<const CatalogSourceMetadata> metadata = nullptr;
  std::shared_ptr<const CatalogUserOverrides> overrides = nullptr;
  const std::string& locator() const noexcept { return member.locator; }
  const std::optional<SourceRevision>& sourceRevision() const noexcept { return member.revision; }
};
struct CatalogRoot {
  RootId id;
  std::uint64_t attachmentGeneration = 1U;
  RootSourcePolicy policy{};
};
struct CatalogSnapshot {
  std::uint64_t revision = 0U;
  std::vector<CatalogSong> songs;
  std::vector<CatalogRoot> roots;
 private:
  // Lifetime identity, not a path/content key or a persistent identifier.
  std::shared_ptr<const unsigned char> origin_;
  friend class SongCatalog;
};

// All coexisting locator copies are charged; shared source/override records are
// counted once by object identity. No additional metadata allowance is granted.
inline bool chargeCatalogPayload(MetadataPayloadBudget& budget, const CatalogSnapshot& snapshot,
                                 unsigned locatorCopies = 1U) {
  std::set<const CatalogSourceMetadata*> metadata;
  std::set<const CatalogUserOverrides*> overrides;
  for (const auto& song : snapshot.songs) {
    for (unsigned i = 0U; i < locatorCopies; ++i)
      if (!budget.charge(song.locator().size())) return false;
    if (!chargeSourceMetadata(budget, song.metadata, metadata)
        || !chargeUserOverrides(budget, song.overrides, overrides)) return false;
  }
  return true;
}

inline bool catalogByteLess(std::string_view a, std::string_view b) noexcept {
  return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(),
      [](char x, char y) { return static_cast<unsigned char>(x) < static_cast<unsigned char>(y); });
}
inline unsigned char catalogAsciiFold(unsigned char c) noexcept {
  return c >= 'A' && c <= 'Z' ? static_cast<unsigned char>(c + ('a' - 'A')) : c;
}
inline bool catalogAliases(std::string_view a, std::string_view b) noexcept {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0U; i < a.size(); ++i) {
    if (catalogAsciiFold(static_cast<unsigned char>(a[i])) !=
        catalogAsciiFold(static_cast<unsigned char>(b[i]))) return false;
  }
  return true;
}
// Strict scalar UTF-8, portable relative components; no normalization.
inline bool isCatalogLocator(std::string_view text) noexcept {
  if (text.empty()) return false;
  std::size_t component = 0U;
  for (std::size_t i = 0U; i <= text.size();) {
    if (i == text.size() || text[i] == '/') {
      const auto part = text.substr(component, i - component);
      if (part.empty() || part == "." || part == "..") return false;
      if (i == text.size()) return true;
      component = ++i;
      continue;
    }
    const auto c = static_cast<unsigned char>(text[i]);
    if (c < 0x80U) {
      if (c < 0x20U || c == 0x7fU || c == '\\' || c == ':') return false;
      ++i;
      continue;
    }
    const unsigned count = c >= 0xc2U && c <= 0xdfU ? 2U :
        c >= 0xe0U && c <= 0xefU ? 3U : c >= 0xf0U && c <= 0xf4U ? 4U : 0U;
    if (count == 0U || text.size() - i < count) return false;
    std::uint32_t scalar = c & (0x7fU >> count);
    for (unsigned n = 1U; n < count; ++n) {
      const auto next = static_cast<unsigned char>(text[i + n]);
      if ((next & 0xc0U) != 0x80U) return false;
      scalar = (scalar << 6U) | (next & 0x3fU);
    }
    const std::uint32_t minimum = count == 2U ? 0x80U : count == 3U ? 0x800U : 0x10000U;
    if (scalar < minimum || scalar > 0x10ffffU || (scalar >= 0xd800U && scalar <= 0xdfffU)) return false;
    i += count;
  }
  return false;
}

// All catalog methods run on one serialized control path. Only already-acquired
// immutable snapshots may be shared with other threads. Snapshots survive later
// commits and catalog destruction.
// This is a logical model: root containment, source validation, exact revision
// validation and filesystem discovery are provided by the discovery wrapper;
// canonical metadata extraction and playback preparation belong to discovery.
// Allocation exceptions propagate; staged mutation preserves the old snapshot.
class SongCatalog {
 public:
  SongCatalog() {
    auto initial = std::make_shared<CatalogSnapshot>();
    initial->origin_ = std::make_shared<const unsigned char>(0U);
    snapshot_ = std::move(initial);
  }
  SongCatalog(const SongCatalog&) = delete;
  SongCatalog& operator=(const SongCatalog&) = delete;

  std::optional<RootId> addRoot(RootSourcePolicy policy = {}, std::size_t alreadyOwned = 0U) {
    if (!isValidRootSourcePolicy(policy)) return std::nullopt;
    if (nextRoot_ == std::numeric_limits<std::uint64_t>::max()) return std::nullopt;
    const RootId root(nextRoot_);
    if (snapshot_->revision == std::numeric_limits<std::uint64_t>::max()) return std::nullopt;
    auto available = MetadataPayloadBudget::create(MetadataPayloadBudget::kMaxBytes, alreadyOwned);
    if (!available) return std::nullopt;
    auto budget = *available;
    if (!chargeCatalogPayload(budget, *snapshot_, 2U)) return std::nullopt;
    auto staged = std::make_shared<CatalogSnapshot>(*snapshot_);
    staged->roots.push_back({root, 1U, policy});
    ++staged->revision;
    snapshot_ = std::move(staged);
    ++nextRoot_;
    return root;
  }
  std::shared_ptr<const CatalogSnapshot> snapshot() const noexcept { return snapshot_; }
  bool ownsSnapshot(const CatalogSnapshot& snapshot) const noexcept {
    return snapshot.origin_ == snapshot_->origin_;
  }

  CatalogError commitScan(RootId root, std::vector<CatalogCandidate> candidates,
                          bool complete, CatalogLimits limits = {}, std::size_t alreadyOwned = 0U) {
    if (!knownRoot(root)) return CatalogError::UnknownRoot;
    if (!complete) return CatalogError::IncompleteScan;
    if (limits.candidates == 0U || limits.candidates > 10000U ||
        limits.locatorBytes == 0U || limits.locatorBytes > 4096U ||
        limits.stagedLocatorBytes == 0U || limits.stagedLocatorBytes > 64U * 1048576U ||
        candidates.size() > limits.candidates) return CatalogError::LimitExceeded;
    auto available = MetadataPayloadBudget::create(limits.stagedLocatorBytes, alreadyOwned);
    if (!available) return CatalogError::LimitExceeded;
    auto budget = *available;
    std::set<const CatalogSourceMetadata*> chargedMetadata;
    std::set<const CatalogUserOverrides*> chargedOverrides;
    for (const auto& song : snapshot_->songs) {
      // Current and staged snapshot own locator copies; metadata is shared.
      if (!budget.charge(song.locator().size()) || !budget.charge(song.locator().size())
          || !chargeSourceMetadata(budget, song.metadata, chargedMetadata)
          || !chargeUserOverrides(budget, song.overrides, chargedOverrides)) return CatalogError::LimitExceeded;
    }
    for (const auto& candidate : candidates) {
      if (!isCatalogLocator(candidate.locator)) return CatalogError::InvalidLocator;
      if (candidate.state == CatalogState::Missing ||
          (candidate.state != CatalogState::Ready && candidate.state != CatalogState::Invalid &&
           candidate.state != CatalogState::UnsupportedProfile)) return CatalogError::InvalidCandidate;
      if (candidate.locator.size() > limits.locatorBytes ||
          !budget.charge(candidate.locator.size()) || !budget.charge(candidate.locator.size())
          || !chargeSourceMetadata(budget, candidate.metadata, chargedMetadata)) return CatalogError::LimitExceeded;
      if (candidate.state != CatalogState::Ready && candidate.metadata
          && (candidate.metadata->title || candidate.metadata->lyrics)) return CatalogError::InvalidCandidate;
    }
    std::sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) {
      return catalogByteLess(a.locator, b.locator);
    });
    // Folded sorting avoids quadratic collision checks for large catalogs.
    std::vector<std::string_view> aliases;
    aliases.reserve(candidates.size());
    for (const auto& candidate : candidates) aliases.push_back(candidate.locator);
    std::sort(aliases.begin(), aliases.end(), [](auto a, auto b) {
      return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(),
          [](char x, char y) { return catalogAsciiFold(static_cast<unsigned char>(x)) <
                                    catalogAsciiFold(static_cast<unsigned char>(y)); });
    });
    for (std::size_t i = 1U; i < aliases.size(); ++i) {
      if (catalogAliases(aliases[i - 1U], aliases[i])) return CatalogError::AmbiguousPath;
    }
    if (snapshot_->revision == std::numeric_limits<std::uint64_t>::max()) return CatalogError::IdExhausted;
    auto staged = std::make_shared<CatalogSnapshot>(*snapshot_);
    auto nextSong = nextSong_;
    for (auto& song : staged->songs) {
      if (song.root == root) {
        song.state = CatalogState::Missing;
        song.member.revision.reset();
        song.metadata.reset();
      }
    }
    // Reserve before creating views; existing locators are never reassigned.
    staged->songs.reserve(staged->songs.size() + candidates.size());
    struct ByteLess {
      bool operator()(std::string_view a, std::string_view b) const noexcept { return catalogByteLess(a, b); }
    };
    struct FoldLess {
      bool operator()(std::string_view a, std::string_view b) const noexcept {
        return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(),
            [](char x, char y) { return catalogAsciiFold(static_cast<unsigned char>(x))
                < catalogAsciiFold(static_cast<unsigned char>(y)); });
      }
    };
    std::map<std::string_view, std::size_t, ByteLess> existing;
    std::map<std::string_view, std::size_t, FoldLess> folded;
    for (std::size_t i = 0U; i < staged->songs.size(); ++i) {
      const auto& song = staged->songs[i];
      if (song.root == root) {
        existing.emplace(song.locator(), i);
        folded.emplace(song.locator(), i);
      }
    }
    for (const auto& candidate : candidates) {
      const auto found = existing.find(candidate.locator);
      if (found != existing.end()) {
        staged->songs[found->second].state = candidate.state;
        staged->songs[found->second].member.revision = candidate.revision;
        staged->songs[found->second].metadata = candidate.metadata;
      } else {
        // Case-only rename requires explicit relocation, including Missing entries.
        if (folded.contains(candidate.locator)) return CatalogError::AmbiguousPath;
        if (nextSong == std::numeric_limits<std::uint64_t>::max()) return CatalogError::IdExhausted;
        staged->songs.push_back({SongId(nextSong++), root,
            {SourceMemberRole::PrimaryMidi, candidate.locator, candidate.revision}, candidate.state, candidate.metadata});
      }
    }
    ++staged->revision;
    snapshot_ = std::move(staged);
    nextSong_ = nextSong;
    return CatalogError::None;
  }

  CatalogError relocate(SongId id, RootId root, std::string locator) {
    if (!knownRoot(root)) return CatalogError::UnknownRoot;
    if (locator.size() > 4096U) return CatalogError::LimitExceeded;
    if (!isCatalogLocator(locator)) return CatalogError::InvalidLocator;
    const auto found = std::find_if(snapshot_->songs.begin(), snapshot_->songs.end(),
        [&](const auto& song) { return song.id == id; });
    if (found == snapshot_->songs.end()) return CatalogError::NotFound;
    if (std::any_of(snapshot_->songs.begin(), snapshot_->songs.end(), [&](const auto& song) {
          return song.id != id && song.root == root && catalogAliases(song.locator(), locator);
        })) return CatalogError::AmbiguousPath;
    if (snapshot_->revision == std::numeric_limits<std::uint64_t>::max()) return CatalogError::IdExhausted;
    auto budget = *MetadataPayloadBudget::create();
    if (!budget.charge(locator.size()) || !chargeCatalogPayload(budget, *snapshot_, 2U))
      return CatalogError::LimitExceeded;
    auto staged = std::make_shared<CatalogSnapshot>(*snapshot_);
    auto& song = staged->songs[static_cast<std::size_t>(found - snapshot_->songs.begin())];
    song.root = root;
    song.member.locator = std::move(locator);
    // The new source must be validated by a later complete scan.
    song.state = CatalogState::Invalid;
    song.member.revision.reset();
    song.metadata.reset();
    ++staged->revision;
    snapshot_ = std::move(staged);
    return CatalogError::None;
  }

  CatalogError remove(SongId id) {
    const auto found = std::find_if(snapshot_->songs.begin(), snapshot_->songs.end(),
        [&](const auto& song) { return song.id == id; });
    if (found == snapshot_->songs.end()) return CatalogError::NotFound;
    if (snapshot_->revision == std::numeric_limits<std::uint64_t>::max()) return CatalogError::IdExhausted;
    auto budget = *MetadataPayloadBudget::create();
    if (!chargeCatalogPayload(budget, *snapshot_, 2U)) return CatalogError::LimitExceeded;
    auto staged = std::make_shared<CatalogSnapshot>(*snapshot_);
    staged->songs.erase(staged->songs.begin() + (found - snapshot_->songs.begin()));
    ++staged->revision;
    snapshot_ = std::move(staged);
    return CatalogError::None;
  }

  // Complete record replacement on the serialized control path. All errors are
  // nonallocating structured values; no source/root/lyric operation occurs here.
  CatalogOverrideResult replaceUserOverrides(SongId id, CatalogOverrideRequest request,
                                            CatalogMetadataLimits limits = {}, std::size_t alreadyOwned = 0U) {
    const auto fail = [](CatalogMetadataErrorCode code, CatalogMetadataField field = CatalogMetadataField::None,
                         std::optional<std::size_t> offset = std::nullopt) {
      return CatalogOverrideResult{{}, CatalogMetadataError{code, field, offset}};
    };
    if (!validCatalogMetadataLimits(limits)) return fail(CatalogMetadataErrorCode::InvalidConfiguration);
    const auto found = std::find_if(snapshot_->songs.begin(), snapshot_->songs.end(),
        [&](const auto& song) { return song.id == id; });
    if (found == snapshot_->songs.end()) return fail(CatalogMetadataErrorCode::NotFound);
    for (auto [text, field] : {std::pair{request.title, CatalogMetadataField::Title},
                             std::pair{request.artist, CatalogMetadataField::Artist}}) {
      if (text && text->empty()) return fail(CatalogMetadataErrorCode::InvalidText, field, 0U);
      if (text && text->size() > limits.textBytes)
        return fail(CatalogMetadataErrorCode::LimitExceeded, field, limits.textBytes);
    }
    const auto equal = [](const std::optional<MetadataText>& old, std::optional<std::string_view> value) {
      return old.has_value() == value.has_value() && (!old || old->bytes() == *value);
    };
    const CatalogUserOverrides empty;
    const auto& previous = found->overrides ? *found->overrides : empty;
    // Equal bytes are already validated. No allocation/publication/revision is
    // needed, including when the revision counter is exhausted.
    if (equal(previous.title, request.title) && equal(previous.artist, request.artist))
      return {CatalogOverrideStatus::Unchanged, {}};
    if (snapshot_->revision == std::numeric_limits<std::uint64_t>::max())
      return fail(CatalogMetadataErrorCode::RevisionExhausted);
    try {
      auto available = MetadataPayloadBudget::create(limits.stagedBytes, alreadyOwned);
      if (!available) return fail(CatalogMetadataErrorCode::LimitExceeded);
      auto budget = *available;
      if (!chargeCatalogPayload(budget, *snapshot_, 2U)) return fail(CatalogMetadataErrorCode::LimitExceeded);
      // Reserve both validation/retained copies per requested field before growth.
      auto peak = budget;
      for (auto text : {request.title, request.artist}) {
        if (text && (!peak.charge(text->size()) || !peak.charge(text->size())))
          return fail(CatalogMetadataErrorCode::LimitExceeded);
      }
      auto record = std::make_shared<CatalogUserOverrides>();
      for (auto [text, field] : {std::pair{request.title, CatalogMetadataField::Title},
                               std::pair{request.artist, CatalogMetadataField::Artist}}) {
        if (!text) continue;
        const auto value = MetadataText::create(*text, limits.textBytes);
        if (!value.succeeded()) return fail(CatalogMetadataErrorCode::InvalidText, field, value.error()->byteOffset);
        (field == CatalogMetadataField::Title ? record->title : record->artist) = *value.text();
      }
      auto staged = std::make_shared<CatalogSnapshot>(*snapshot_);
      auto& song = staged->songs[static_cast<std::size_t>(found - snapshot_->songs.begin())];
      song.overrides = request.title || request.artist ? std::move(record) : nullptr;
      ++staged->revision;
      std::shared_ptr<const CatalogSnapshot> publication = std::move(staged);
      snapshot_.swap(publication); // Single noexcept publication after all allocations.
      return {CatalogOverrideStatus::Updated, {}};
    } catch (const std::bad_alloc&) { return fail(CatalogMetadataErrorCode::StorageFailure); }
  }

  CatalogDisplayResult display(std::shared_ptr<const CatalogSnapshot> snapshot, SongId id,
                               CatalogMetadataLimits limits = {}, std::size_t alreadyOwned = 0U) const {
    const auto fail = [](CatalogMetadataErrorCode code) {
      return CatalogDisplayResult{nullptr, CatalogMetadataError{code, CatalogMetadataField::None, {}}};
    };
    if (!snapshot || !ownsSnapshot(*snapshot) || !validCatalogMetadataLimits(limits))
      return fail(CatalogMetadataErrorCode::InvalidConfiguration);
    const auto found = std::find_if(snapshot->songs.begin(), snapshot->songs.end(),
        [&](const auto& song) { return song.id == id; });
    if (found == snapshot->songs.end()) return fail(CatalogMetadataErrorCode::NotFound);
    try {
      auto available = MetadataPayloadBudget::create(limits.stagedBytes, alreadyOwned);
      if (!available) return fail(CatalogMetadataErrorCode::LimitExceeded);
      auto budget = *available;
      if (!chargeCatalogPayload(budget, *snapshot)) return fail(CatalogMetadataErrorCode::LimitExceeded);
      return resolveCatalogDisplay(found->locator(), found->metadata.get(), found->overrides.get(),
                                   limits.textBytes, budget);
    } catch (const std::bad_alloc&) { return fail(CatalogMetadataErrorCode::StorageFailure); }
  }

 private:
  friend class SongDiscovery;
  friend class CatalogCheckpointRestore;
  friend class CatalogCheckpointCapture;
  // Defined by the validated restore adapter, never exposed as raw-ID construction.
  void restoreCheckpointProjection(const CatalogCheckpointProjection& projection);
#ifdef OPENHDK_ENABLE_TEST_SEAMS
  friend struct RootReattachmentTestAccess;
  friend struct CatalogMetadataTransactionTestAccess;
#endif
  bool knownRoot(RootId root) const noexcept {
    return std::any_of(snapshot_->roots.begin(), snapshot_->roots.end(),
        [&](const auto& item) { return item.id == root; });
  }
  // Discovery stages its path mapping before calling this helper. Neither
  // staging step publishes. The serialized writer commits with noexcept swaps.
  std::shared_ptr<const CatalogSnapshot> stageRootReattachment(RootId root, std::size_t alreadyOwned = 0U) const {
    auto available = MetadataPayloadBudget::create(MetadataPayloadBudget::kMaxBytes, alreadyOwned);
    if (!available) return nullptr;
    auto budget = *available;
    if (!chargeCatalogPayload(budget, *snapshot_, 2U)) return nullptr;
    auto staged = std::make_shared<CatalogSnapshot>(*snapshot_);
    const auto found = std::find_if(staged->roots.begin(), staged->roots.end(),
        [&](const auto& item) { return item.id == root; });
    ++found->attachmentGeneration; // Caller has checked both counter bounds.
    ++staged->revision;
    for (auto& song : staged->songs) {
      if (song.root == root) {
        song.state = CatalogState::Invalid;
        song.member.revision.reset();
        song.metadata.reset();
      }
    }
    return staged;
  }
  std::uint64_t nextRoot_ = 1U;
  std::uint64_t nextSong_ = 1U;
  std::shared_ptr<const CatalogSnapshot> snapshot_;
};
}  // namespace OpenHDK
