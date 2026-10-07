// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "library/FilesystemSource.hpp"
#include "audio/SmfTimelineCompiler.hpp"
#include <functional>
#include <new>

namespace OpenHDK {
enum class DiscoveryError {
  InvalidRoot, AmbiguousPath, SourceUnreadable, SourceChanged, InvalidSmf,
  LimitExceeded, Cancelled, StorageFailure
};
enum class DiscoveryOperation { RegisterRoot, Enumerate, Read, Validate, Publish };
struct DiscoveryDiagnostic {
  DiscoveryError code;
  DiscoveryOperation operation;
  std::string locator;
  std::optional<SmfParseError> parseError = std::nullopt;
  std::optional<SmfTimelineError> timelineError = std::nullopt;
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
};
enum class DiscoveryCheckpoint { AfterEnumeration, AfterValidation, BeforePublication };
struct DiscoveryControl {
  // Control-path only; hooks permit deterministic cancellation/change tests.
  std::function<bool()> cancelled;
  std::function<void(DiscoveryCheckpoint)> checkpoint;
};
struct RootRegistration {
  std::optional<RootId> root;
  std::optional<DiscoveryDiagnostic> error;
};

class SongDiscovery {
 public:
  std::shared_ptr<const CatalogSnapshot> snapshot() const noexcept { return catalog_.snapshot(); }
  // This slice supports SMF/KAR discovery only; it does not extract lyrics.
  RootRegistration registerRoot(const std::filesystem::path& path) {
    try { return registerRootImpl(path); }
    catch (const std::bad_alloc&) { return {{}, diagnostic(DiscoveryError::StorageFailure, DiscoveryOperation::RegisterRoot)}; }
    catch (const std::filesystem::filesystem_error&) { return {{}, diagnostic(DiscoveryError::InvalidRoot, DiscoveryOperation::RegisterRoot)}; }
  }

  DiscoveryResult scan(RootId root, DiscoveryLimits limits = {}, const DiscoveryControl& control = {}) {
    auto operation = DiscoveryOperation::Enumerate;
    try { return scanImpl(root, limits, control, operation); }
    catch (const std::bad_alloc&) { return {diagnostic(DiscoveryError::StorageFailure, operation), 0U, {}}; }
    catch (const std::filesystem::filesystem_error&) { return {diagnostic(DiscoveryError::SourceUnreadable, operation), 0U, {}}; }
  }

 private:
  RootRegistration registerRootImpl(const std::filesystem::path& path) {
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
    const auto id = catalog_.addRoot();
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
      limits.catalog.stagedLocatorBytes > 0U && limits.catalog.stagedLocatorBytes <= 64U * 1048576U;
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
    // Reserve the existing staged snapshot plus four concurrent path copies:
    // enumeration, candidate descriptors, re-enumeration/fold sort, publication.
    // Fixed-size revisions and temporary SMF parse buffers are separate bounds.
    std::size_t existingBytes = 0U;
    for (const auto& song : catalog_.snapshot()->songs) {
      if (song.locator.size() > limits.catalog.stagedLocatorBytes - existingBytes)
        return {diagnostic(DiscoveryError::LimitExceeded, DiscoveryOperation::Enumerate), 0U, {}};
      existingBytes += song.locator.size();
    }
    auto enumerationLimits = limits;
    enumerationLimits.catalog.stagedLocatorBytes = (limits.catalog.stagedLocatorBytes - existingBytes) / 4U;
    std::vector<std::string> locators;
    DiscoveryResult result;
    if (auto error = collect(path, enumerationLimits, control, locators)) { result.error = std::move(error); return result; }
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
