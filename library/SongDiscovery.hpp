// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "library/SongPreparation.hpp"
#include "library/DirectoryIdentity.hpp"
#include "audio/SmfTimelineCompiler.hpp"
#include <functional>
#include <new>

namespace OpenHDK {
enum class DiscoveryError {
  InvalidRoot, AmbiguousPath, SourceUnreadable, SourceChanged, InvalidSmf,
  LimitExceeded, Cancelled, StorageFailure, RevisionExhausted, InvalidConfiguration, InvalidLyrics
};
enum class DiscoveryOperation { RegisterRoot, Enumerate, Read, Validate, Publish, ReattachRoot, ExtractLyrics };
struct DiscoveryDiagnostic {
  DiscoveryError code;
  DiscoveryOperation operation;
  std::string locator;
  std::optional<SmfParseError> parseError = std::nullopt;
  std::optional<SmfTimelineError> timelineError = std::nullopt;
  std::optional<KarLyricError> lyricError = std::nullopt;
  std::string_view message() const noexcept {
    switch (code) {
      case DiscoveryError::InvalidConfiguration: return "The root policy or library configuration is invalid.";
      case DiscoveryError::InvalidLyrics: return "The selected lyrics failed validation.";
      case DiscoveryError::InvalidRoot: return "The registered root or selected directory is invalid.";
      case DiscoveryError::AmbiguousPath: return "The selected path overlaps or aliases a registered source.";
      case DiscoveryError::SourceUnreadable: return "A source could not be read.";
      case DiscoveryError::SourceChanged: return "The source or directory changed during validation.";
      case DiscoveryError::InvalidSmf: return "The source is not a valid canonical SMF.";
      case DiscoveryError::LimitExceeded: return "A configured library limit was exceeded.";
      case DiscoveryError::Cancelled: return "The library operation was cancelled.";
      case DiscoveryError::StorageFailure: return "Library staging could not allocate storage.";
      case DiscoveryError::RevisionExhausted: return "A root attachment or catalog revision counter is exhausted.";
    }
    return "The library operation failed.";
  }
};
struct DiscoveryResult {
  std::optional<DiscoveryDiagnostic> error;
  std::size_t candidates = 0U;
  std::vector<DiscoveryDiagnostic> diagnostics;
  bool succeeded() const noexcept { return !error.has_value(); }
};
struct DiscoveryLimits {
  CatalogLimits catalog;
  std::size_t depth = 32U;
  std::size_t smfBytes = 64U * 1048576U;
  KarLyricLimits lyrics{};
};
enum class DiscoveryCheckpoint { AfterEnumeration, AfterValidation, BeforePublication, AfterExtraction };
struct DiscoveryControl {
  // Control-path only; hooks permit deterministic cancellation/change tests.
  std::function<bool()> cancelled;
  std::function<void(DiscoveryCheckpoint)> checkpoint;
};
struct RootRegistration {
  std::optional<RootId> root;
  std::optional<DiscoveryDiagnostic> error;
};

enum class RootReattachmentStatus { Updated, Unchanged };
struct RootReattachmentResult {
  std::optional<RootReattachmentStatus> status;
  std::optional<DiscoveryDiagnostic> error;
  bool succeeded() const noexcept { return status.has_value() && !error; }
};
enum class RootReattachmentCheckpoint { AfterValidation, BeforeCatalogStaging, BeforeCommit };
struct RootReattachmentControl {
  // Serialized control-path hooks. No re-entry into this library is permitted.
  std::function<bool()> cancelled;
  std::function<void(RootReattachmentCheckpoint)> checkpoint;
};

class SongDiscovery {
 public:
  std::shared_ptr<const CatalogSnapshot> snapshot() const noexcept { return catalog_.snapshot(); }
  // Reject unknown policy before filesystem access or ID/revision consumption.
  RootRegistration registerRoot(const std::filesystem::path& path, RootSourcePolicy policy = {}) {
    if (!isValidRootSourcePolicy(policy))
      return {{}, diagnostic(DiscoveryError::InvalidConfiguration, DiscoveryOperation::RegisterRoot)};
    try { return registerRootImpl(path, policy); }
    catch (const std::bad_alloc&) { return {{}, diagnostic(DiscoveryError::StorageFailure, DiscoveryOperation::RegisterRoot)}; }
    catch (const std::filesystem::filesystem_error&) { return {{}, diagnostic(DiscoveryError::InvalidRoot, DiscoveryOperation::RegisterRoot)}; }
  }

  DiscoveryResult scan(RootId root, DiscoveryLimits limits = {}, const DiscoveryControl& control = {}) {
    auto operation = DiscoveryOperation::Enumerate;
    try { return scanImpl(root, limits, control, operation); }
    catch (const std::bad_alloc&) { return {diagnostic(DiscoveryError::StorageFailure, operation), 0U, {}}; }
    catch (const std::filesystem::filesystem_error&) { return {diagnostic(DiscoveryError::SourceUnreadable, operation), 0U, {}}; }
  }

  RootReattachmentResult reattachRoot(RootId root, const std::filesystem::path& path,
      const RootReattachmentControl& control = {}) {
    try { return reattachRootImpl(root, path, control); }
    catch (const std::bad_alloc&) { return reattachmentFailure(DiscoveryError::StorageFailure); }
    catch (const std::filesystem::filesystem_error&) { return reattachmentFailure(DiscoveryError::InvalidRoot); }
  }

  // Reject snapshots with foreign or absent catalog lineage before any I/O.
  // Older snapshots remain usable if source bytes still match.
  SongPreparationResult prepare(std::shared_ptr<const CatalogSnapshot> snapshot,
      SongId id, PreparationOptions options = {}, const PreparationControl& control = {}) const {
    const auto failure=[](PreparationErrorCode code) {
      return SongPreparationResult{nullptr,PreparationError{code,PreparationOperation::Resolve,{},{},{},{}}};
    };
    if(!snapshot || !catalog_.ownsSnapshot(*snapshot))
      return failure(PreparationErrorCode::InvalidConfiguration);
    const auto song=std::find_if(snapshot->songs.begin(),snapshot->songs.end(),
        [&](const auto& item) { return item.id==id; });
    if(song==snapshot->songs.end()) return failure(PreparationErrorCode::NotFound);
    const auto root=std::find_if(roots_.begin(),roots_.end(),
        [&](const auto& item) { return item.id==song->root; });
    if(root==roots_.end()) return failure(PreparationErrorCode::InvalidRoot);
    const auto captured = std::find_if(snapshot->roots.begin(), snapshot->roots.end(),
        [&](const auto& item) { return item.id == song->root; });
    const auto current = catalog_.snapshot();
    const auto binding = std::find_if(current->roots.begin(), current->roots.end(),
        [&](const auto& item) { return item.id == song->root; });
    if (captured == snapshot->roots.end() || binding == current->roots.end() ||
        captured->attachmentGeneration != binding->attachmentGeneration)
      return failure(PreparationErrorCode::SourceChanged);
    const auto effective = options.lyricOverride.value_or(captured->policy.lyrics);
    if (!isValidRootSourcePolicy(captured->policy) || !isValidLyricSelectionPolicy(effective))
      return failure(PreparationErrorCode::InvalidConfiguration);
    return SongPreparation::prepare(root->path,snapshot,
        static_cast<std::size_t>(song-snapshot->songs.begin()),options,effective,control);
  }

 private:
#ifdef OPENHDK_ENABLE_TEST_SEAMS
  friend struct RootReattachmentTestAccess;
#endif
  static RootReattachmentResult reattachmentFailure(DiscoveryError code) {
    return {{}, diagnostic(code, DiscoveryOperation::ReattachRoot)};
  }
  std::optional<DiscoveryError> validateReattachmentTarget(RootId root,
      const std::filesystem::path& canonical) const {
    namespace fs = std::filesystem;
#ifdef _WIN32
    if (LibraryFilesystem::windowsNetworkPath(LibraryFilesystem::utf8(canonical)))
      return DiscoveryError::InvalidRoot;
#endif
    for (const auto& entry : roots_) {
      if (entry.id != root && (LibraryFilesystem::contains(entry.path, canonical) ||
          LibraryFilesystem::contains(canonical, entry.path))) return DiscoveryError::AmbiguousPath;
      std::error_code ec;
      const bool present = fs::exists(entry.path, ec);
      if (ec) return DiscoveryError::InvalidRoot;
      if (present && entry.path != canonical) {
        const bool equivalent = fs::equivalent(entry.path, canonical, ec);
        if (ec) return DiscoveryError::InvalidRoot;
        if (equivalent) return DiscoveryError::AmbiguousPath;
      }
    }
    return {};
  }
  RootReattachmentResult reattachRootImpl(RootId root, const std::filesystem::path& path,
      const RootReattachmentControl& control) {
    namespace fs = std::filesystem;
    const auto found = std::find_if(roots_.begin(), roots_.end(),
        [&](const auto& item) { return item.id == root; });
    if (found == roots_.end()) return reattachmentFailure(DiscoveryError::InvalidRoot);
    const auto isCancelled = [&] { return control.cancelled && control.cancelled(); };
    const auto at = [&](RootReattachmentCheckpoint point) {
      if (control.checkpoint) control.checkpoint(point);
    };
    if (isCancelled()) return reattachmentFailure(DiscoveryError::Cancelled);
#ifdef _WIN32
    if (LibraryFilesystem::windowsNetworkPath(LibraryFilesystem::utf8(path)))
      return reattachmentFailure(DiscoveryError::InvalidRoot);
#endif
    std::error_code ec;
    auto target = fs::canonical(path, ec);
    if (ec || !fs::is_directory(target, ec) || ec)
      return reattachmentFailure(DiscoveryError::InvalidRoot);
    if (auto error = validateReattachmentTarget(root, target)) return reattachmentFailure(*error);
    LibraryFilesystem::DirectoryHandle original(target);
    if (!original.identity()) return reattachmentFailure(DiscoveryError::InvalidRoot);
    if (target == found->path) return {RootReattachmentStatus::Unchanged, {}};
    const auto current = catalog_.snapshot();
    const auto binding = std::find_if(current->roots.begin(), current->roots.end(),
        [&](const auto& item) { return item.id == root; });
    if (binding == current->roots.end()) return reattachmentFailure(DiscoveryError::InvalidRoot);
    if (binding->attachmentGeneration == std::numeric_limits<std::uint64_t>::max() ||
        current->revision == std::numeric_limits<std::uint64_t>::max())
      return reattachmentFailure(DiscoveryError::RevisionExhausted);
    at(RootReattachmentCheckpoint::AfterValidation);
    at(RootReattachmentCheckpoint::BeforeCatalogStaging);
    auto stagedCatalog = catalog_.stageRootReattachment(root);
    at(RootReattachmentCheckpoint::BeforeCommit);
    // Hold the original native handle while reopening: its identity cannot be
    // recycled if an external actor deletes/replaces the directory during staging.
    const auto verified = fs::canonical(target, ec);
    if (ec || verified != target) return reattachmentFailure(DiscoveryError::SourceChanged);
    LibraryFilesystem::DirectoryHandle final(target);
    if (!final.identity() || final.identity() != original.identity())
      return reattachmentFailure(DiscoveryError::SourceChanged);
    if (auto error = validateReattachmentTarget(root, target)) return reattachmentFailure(*error);
    if (isCancelled()) return reattachmentFailure(DiscoveryError::Cancelled);
    // All allocation, hooks and I/O precede these two non-throwing mutations.
    static_assert(noexcept(found->path.swap(target)));
    found->path.swap(target);
    catalog_.snapshot_.swap(stagedCatalog);
    return {RootReattachmentStatus::Updated, {}};
  }

  RootRegistration registerRootImpl(const std::filesystem::path& path, RootSourcePolicy policy) {
    namespace fs = std::filesystem;
    std::error_code ec;
    auto canonical = fs::canonical(path, ec);
    if (ec || !fs::is_directory(canonical, ec) || ec) return {{}, diagnostic(DiscoveryError::InvalidRoot, DiscoveryOperation::RegisterRoot)};
#ifdef _WIN32
    // UNC/network roots are outside the local-directory discovery slice.
    if (LibraryFilesystem::windowsNetworkPath(LibraryFilesystem::utf8(canonical)))
      return {{}, diagnostic(DiscoveryError::InvalidRoot, DiscoveryOperation::RegisterRoot)};
#endif
    for (const auto& entry : roots_) {
      if (LibraryFilesystem::contains(entry.path, canonical) || LibraryFilesystem::contains(canonical, entry.path) ||
          fs::equivalent(entry.path, canonical, ec)) return {{}, diagnostic(DiscoveryError::AmbiguousPath, DiscoveryOperation::RegisterRoot)};
      if (ec) return {{}, diagnostic(DiscoveryError::InvalidRoot, DiscoveryOperation::RegisterRoot)};
    }
    roots_.reserve(roots_.size() + 1U);
    const auto id = catalog_.addRoot(policy);
    if (!id) return {{}, diagnostic(DiscoveryError::StorageFailure, DiscoveryOperation::RegisterRoot)};
    roots_.push_back({*id, std::move(canonical)});
    return {id, {}};
  }

  struct Registered { RootId id; std::filesystem::path path; };
  static DiscoveryDiagnostic diagnostic(DiscoveryError code, DiscoveryOperation operation, std::string locator = {}) {
    return {code, operation, std::move(locator), {}, {}};
  }
  static bool cancelled(const DiscoveryControl& control) { return control.cancelled && control.cancelled(); }
  static void checkpoint(const DiscoveryControl& control, DiscoveryCheckpoint point) {
    if (control.checkpoint) control.checkpoint(point);
  }
  static bool validLimits(const DiscoveryLimits& limits) noexcept {
    return limits.depth > 0U && limits.depth <= 32U && limits.smfBytes > 0U && limits.smfBytes <= 64U * 1048576U &&
      limits.catalog.candidates > 0U && limits.catalog.candidates <= 10000U &&
      limits.catalog.locatorBytes > 0U && limits.catalog.locatorBytes <= 4096U &&
      limits.catalog.stagedLocatorBytes > 0U && limits.catalog.stagedLocatorBytes <= 64U * 1048576U && validKarLyricLimits(limits.lyrics);
  }
  static std::optional<DiscoveryDiagnostic> collect(const std::filesystem::path& root,
      DiscoveryLimits limits, const DiscoveryControl& control, std::vector<std::string>& locators) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (fs::canonical(root, ec) != root || ec || !fs::is_directory(root, ec) || ec)
      return diagnostic(DiscoveryError::InvalidRoot, DiscoveryOperation::Enumerate);
    fs::recursive_directory_iterator it(root, fs::directory_options::none, ec), end;
    if (ec) return diagnostic(DiscoveryError::SourceUnreadable, DiscoveryOperation::Enumerate);
    std::size_t ownedBytes = 0U;
    while (it != end) {
      if (cancelled(control)) return diagnostic(DiscoveryError::Cancelled, DiscoveryOperation::Enumerate);
      const auto path = it->path();
      const bool skip = LibraryFilesystem::reparse(path, ec);
      if (ec) return diagnostic(DiscoveryError::SourceChanged, DiscoveryOperation::Enumerate);
      if (skip) {
        it.disable_recursion_pending();
      } else {
        const auto status = it->symlink_status(ec);
        if (ec) return diagnostic(DiscoveryError::SourceChanged, DiscoveryOperation::Enumerate);
        if (fs::is_directory(status) && static_cast<std::size_t>(it.depth()) + 1U > limits.depth)
          return diagnostic(DiscoveryError::LimitExceeded, DiscoveryOperation::Enumerate);
        auto extension = LibraryFilesystem::utf8(path.extension());
        for (auto& c : extension) c = static_cast<char>(catalogAsciiFold(static_cast<unsigned char>(c)));
        if (fs::is_regular_file(status) && (extension == ".mid" || extension == ".midi" || extension == ".kar")) {
          auto locator = LibraryFilesystem::utf8(path.lexically_relative(root));
          if (!isCatalogLocator(locator)) return diagnostic(DiscoveryError::AmbiguousPath, DiscoveryOperation::Enumerate);
          const auto resolved = fs::canonical(path, ec);
          if (ec) return diagnostic(DiscoveryError::SourceChanged, DiscoveryOperation::Enumerate, locator);
          if (resolved != path || !LibraryFilesystem::contains(root, resolved))
            return diagnostic(DiscoveryError::AmbiguousPath, DiscoveryOperation::Enumerate, locator);
          if (locators.size() >= limits.catalog.candidates || locator.size() > limits.catalog.locatorBytes ||
              locator.size() > limits.catalog.stagedLocatorBytes - ownedBytes)
            return diagnostic(DiscoveryError::LimitExceeded, DiscoveryOperation::Enumerate, locator);
          ownedBytes += locator.size();
          locators.push_back(std::move(locator));
        }
      }
      it.increment(ec);
      if (ec) return diagnostic(DiscoveryError::SourceUnreadable, DiscoveryOperation::Enumerate);
    }
    std::sort(locators.begin(), locators.end(), catalogByteLess);
    auto folded = locators;
    for (auto& name : folded) for (auto& c : name) c = static_cast<char>(catalogAsciiFold(static_cast<unsigned char>(c)));
    std::sort(folded.begin(), folded.end(), catalogByteLess);
    for (std::size_t i = 1U; i < folded.size(); ++i) {
      if (folded[i] == folded[i - 1U]) return diagnostic(DiscoveryError::AmbiguousPath, DiscoveryOperation::Enumerate);
    }
    return {};
  }

  DiscoveryResult scanImpl(RootId root, DiscoveryLimits limits, const DiscoveryControl& control, DiscoveryOperation& operation) {
    const auto found = std::find_if(roots_.begin(), roots_.end(), [&](const auto& item) { return item.id == root; });
    if (found == roots_.end()) return {diagnostic(DiscoveryError::InvalidRoot, DiscoveryOperation::Enumerate), 0U, {}};
    if (!validLimits(limits)) return {diagnostic(DiscoveryError::LimitExceeded, DiscoveryOperation::Enumerate), 0U, {}};
    // Copy the path: control hooks must not invalidate a vector element reference.
    const auto path = found->path;
    const auto captured = catalog_.snapshot();
    const auto rootRecord = std::find_if(captured->roots.begin(), captured->roots.end(),
        [&](const auto& item) { return item.id == root; });
    if (rootRecord == captured->roots.end() || !isValidRootSourcePolicy(rootRecord->policy))
      return {diagnostic(DiscoveryError::InvalidConfiguration, DiscoveryOperation::Enumerate), 0U, {}};
    const auto policy = rootRecord->policy.lyrics;
    auto budget = *MetadataPayloadBudget::create(limits.catalog.stagedLocatorBytes);
    std::set<const CatalogSourceMetadata*> chargedMetadata;
    for (const auto& song : captured->songs) {
      // Current + staged locator copies; immutable compact records are shared.
      if (!budget.charge(song.locator().size()) || !budget.charge(song.locator().size())
          || !chargeSourceMetadata(budget, song.metadata, chargedMetadata))
        return {diagnostic(DiscoveryError::LimitExceeded, DiscoveryOperation::Enumerate), 0U, {}};
    }
    // Five incoming copies cover enumeration/candidates, diagnostics, and either
    // verification + fold-sort or verification + catalog publication. Catalog
    // lookup maps now use stable views, not extra owned path strings.
    auto enumerationLimits = limits;
    enumerationLimits.catalog.stagedLocatorBytes = budget.remaining() / 5U;
    std::vector<std::string> locators;
    DiscoveryResult result;
    if (auto error = collect(path, enumerationLimits, control, locators)) { result.error = std::move(error); return result; }
    for (const auto& locator : locators) {
      for (unsigned copy = 0U; copy < 5U; ++copy) {
        if (!budget.charge(locator.size()))
          return {diagnostic(DiscoveryError::LimitExceeded, DiscoveryOperation::Enumerate), 0U, {}};
      }
    }
    result.candidates = locators.size();
    checkpoint(control, DiscoveryCheckpoint::AfterEnumeration);
    std::vector<CatalogCandidate> candidates;
    candidates.reserve(locators.size());
    for (const auto& locator : locators) {
      if (cancelled(control)) { result.error = diagnostic(DiscoveryError::Cancelled, DiscoveryOperation::Read); return result; }
      operation = DiscoveryOperation::Read;
      auto input = LibraryFilesystem::read(path, locator, limits.smfBytes);
      if (input.error == LibraryFilesystem::ReadError::LimitExceeded || input.error == LibraryFilesystem::ReadError::AmbiguousPath) {
        result.error = diagnostic(input.error == LibraryFilesystem::ReadError::LimitExceeded ? DiscoveryError::LimitExceeded : DiscoveryError::AmbiguousPath, DiscoveryOperation::Read, locator); return result;
      }
      CatalogCandidate candidate{locator, CatalogState::Ready, {}};
      if (input.error != LibraryFilesystem::ReadError::None) {
        // Disappearance during preparation aborts; permission failure is diagnostic-only.
        std::error_code ec;
        if (!std::filesystem::exists(path / LibraryFilesystem::fromUtf8(locator), ec) || ec) {
          result.error = diagnostic(DiscoveryError::SourceChanged, DiscoveryOperation::Read, locator); return result;
        }
        candidate.state = CatalogState::Invalid;
        result.diagnostics.push_back(diagnostic(DiscoveryError::SourceUnreadable, DiscoveryOperation::Read, locator));
      } else {
        candidate.revision = sourceRevision(input.bytes);
        operation = DiscoveryOperation::Validate;
        const auto parsed = SmfParser::parse(input.bytes);
        if (!parsed.succeeded()) {
          candidate.state = CatalogState::Invalid;
          auto error = diagnostic(DiscoveryError::InvalidSmf, DiscoveryOperation::Validate, locator);
          error.parseError = *parsed.error(); result.diagnostics.push_back(std::move(error));
        } else {
          const auto compiled = SmfTimelineCompiler::compile(*parsed.file());
          if (!compiled.succeeded()) {
            candidate.state = CatalogState::Invalid;
            auto error = diagnostic(DiscoveryError::InvalidSmf, DiscoveryOperation::Validate, locator);
            error.timelineError = *compiled.error(); result.diagnostics.push_back(std::move(error));
          } else {
            operation = DiscoveryOperation::ExtractLyrics;
            auto options = *karOptionsForPolicy(policy, limits.lyrics);
            // Extraction can temporarily hold both decoded text and its owned
            // copy. Reserve twice its staged ceiling within the existing budget.
            options.limits.stagedBytes = std::min(options.limits.stagedBytes,
                std::max(std::size_t{1U}, budget.remaining() / 2U));
            const auto lyrics = KarLyricExtractor::extract(*compiled.timeline(), options);
            if (!lyrics.succeeded()) {
              auto error = diagnostic(lyrics.error()->code == KarLyricErrorCode::LimitExceeded
                  ? DiscoveryError::LimitExceeded : DiscoveryError::InvalidLyrics, operation, locator);
              error.lyricError = *lyrics.error();
              if (error.code == DiscoveryError::LimitExceeded) { result.error = std::move(error); return result; }
              candidate.state = CatalogState::Invalid;
              auto invalid = std::make_shared<CatalogSourceMetadata>();
              invalid->kind = VerifiedSourceKind::CanonicalSmf;
              candidate.metadata = std::move(invalid);
              result.diagnostics.push_back(std::move(error));
            } else {
              auto live = budget;
              if (!chargeLyricPayload(live, *lyrics.timeline())) {
                result.error = diagnostic(DiscoveryError::LimitExceeded, operation, locator); return result;
              }
              const auto compact = compactSourceMetadata(*lyrics.timeline(), *candidate.revision,
                  policy, limits.lyrics.titleBytes, live);
              if (compact.error) {
                result.error = diagnostic(compact.error->code == KarLyricErrorCode::LimitExceeded
                    ? DiscoveryError::LimitExceeded : DiscoveryError::InvalidConfiguration, operation, locator);
                result.error->lyricError = *compact.error; return result;
              }
              candidate.metadata = compact.metadata;
              if (!chargeSourceMetadata(budget, candidate.metadata, chargedMetadata)) {
                result.error = diagnostic(DiscoveryError::LimitExceeded, operation, locator); return result;
              }
            }
            checkpoint(control, DiscoveryCheckpoint::AfterExtraction);
            if (cancelled(control)) { result.error = diagnostic(DiscoveryError::Cancelled, operation, locator); return result; }
          }
        }
      }
      candidates.push_back(std::move(candidate));
      // input/parse/compiled storage is released before the next source.
    }
    operation = DiscoveryOperation::Publish;
    checkpoint(control, DiscoveryCheckpoint::AfterValidation);
    checkpoint(control, DiscoveryCheckpoint::BeforePublication);
    // Re-enumerate and re-read before committing anything. This is a consistency
    // check, not a filesystem-wide snapshot or a guarantee against ongoing edits.
    std::vector<std::string> current;
    if (auto error = collect(path, enumerationLimits, control, current)) { result.error = std::move(error); return result; }
    if (current != locators) { result.error = diagnostic(DiscoveryError::SourceChanged, DiscoveryOperation::Publish); return result; }
    for (const auto& candidate : candidates) {
      if (cancelled(control)) { result.error = diagnostic(DiscoveryError::Cancelled, DiscoveryOperation::Publish); return result; }
      const auto input = LibraryFilesystem::read(path, candidate.locator, limits.smfBytes);
      const bool unreadable = input.error == LibraryFilesystem::ReadError::Unreadable;
      if ((candidate.revision && (input.error != LibraryFilesystem::ReadError::None || sourceRevision(input.bytes) != *candidate.revision)) ||
          (!candidate.revision && !unreadable)) {
        result.error = diagnostic(DiscoveryError::SourceChanged, DiscoveryOperation::Publish, candidate.locator); return result;
      }
    }
    if (cancelled(control)) { result.error = diagnostic(DiscoveryError::Cancelled, DiscoveryOperation::Publish); return result; }
    const auto committed = catalog_.commitScan(root, std::move(candidates), true, limits.catalog);
    if (committed != CatalogError::None) {
      result.error = diagnostic(committed == CatalogError::AmbiguousPath ? DiscoveryError::AmbiguousPath :
          committed == CatalogError::LimitExceeded ? DiscoveryError::LimitExceeded : DiscoveryError::StorageFailure, DiscoveryOperation::Publish);
    }
    return result;
  }
  SongCatalog catalog_;
  std::vector<Registered> roots_;
};
}  // namespace OpenHDK
