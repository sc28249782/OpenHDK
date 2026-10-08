// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 OpenHDK contributors
#pragma once
#include "library/FilesystemSource.hpp"
#include "lyrics/KarLyricExtractor.hpp"
#include <functional>
#include <new>

namespace OpenHDK {
class SongDiscovery;
enum class PreparationErrorCode {
  InvalidConfiguration, NotFound, NotReady, InvalidRoot, AmbiguousPath,
  SourceUnreadable, SourceChanged, InvalidSmf, InvalidLyrics, LimitExceeded,
  Cancelled, StorageFailure
};
enum class PreparationOperation { Resolve, Read, Compile, ExtractLyrics, Verify, Publish };
struct PreparationError {
  PreparationErrorCode code;
  PreparationOperation operation;
  std::string locator;
  std::optional<SmfParseError> parseError{};
  std::optional<SmfTimelineError> timelineError{};
  std::optional<KarLyricError> lyricError{};
  std::string_view message() const noexcept {
    switch(code) {
    case PreparationErrorCode::InvalidConfiguration: return "Invalid song preparation configuration";
    case PreparationErrorCode::NotFound: return "Song was not found in the supplied snapshot";
    case PreparationErrorCode::NotReady: return "Song is not Ready with a verified source revision";
    case PreparationErrorCode::InvalidRoot: return "Registered source root is unavailable";
    case PreparationErrorCode::AmbiguousPath: return "Source path is ambiguous or follows an alias";
    case PreparationErrorCode::SourceUnreadable: return "Song source could not be read";
    case PreparationErrorCode::SourceChanged: return "Song source changed since its recorded revision";
    case PreparationErrorCode::InvalidSmf: return "Song MIDI validation failed";
    case PreparationErrorCode::InvalidLyrics: return "Song lyric validation failed";
    case PreparationErrorCode::LimitExceeded: return "Song preparation exceeded a configured limit";
    case PreparationErrorCode::Cancelled: return "Song preparation was cancelled";
    case PreparationErrorCode::StorageFailure: return "Song preparation allocation failed";
    }
    return "Unknown song preparation error";
  }
};
struct PreparationOptions {
  std::size_t smfBytes = 64U * 1048576U;
  // Absent means inherit the acquired snapshot root's complete policy.
  std::optional<LyricSelectionPolicy> lyricOverride = std::nullopt;
  KarLyricLimits lyricLimits{};
  std::size_t stagedBytes = MetadataPayloadBudget::kMaxBytes;
};
enum class PreparationCheckpoint { AfterRead, AfterExtraction };
struct PreparationControl {
  std::function<bool()> cancelled;
  std::function<void(PreparationCheckpoint)> checkpoint;
};

// Own the original catalog snapshot and both immutable timelines. Rescans,
// catalog destruction, and later file edits cannot replace these buffers.
class PreparedSong {
public:
  const CatalogSong& song() const noexcept { return snapshot_->songs[index_]; }
  std::shared_ptr<const CatalogSnapshot> catalogSnapshot() const noexcept { return snapshot_; }
  const SmfTimeline& midi() const noexcept { return *compiled_.timeline(); }
  std::shared_ptr<const KarLyricTimeline> lyrics() const noexcept { return lyrics_; }
  const PreparationOptions& options() const noexcept { return options_; }
  const LyricSelectionPolicy& effectivePolicy() const noexcept { return effectivePolicy_; }
  // Fresh extraction under effectivePolicy(), distinct from song().metadata,
  // whose selection reflects the root policy recorded in catalogSnapshot().
  const CatalogSourceMetadata& sourceMetadata() const noexcept { return *metadata_; }
  const CatalogDisplayMetadata& displayMetadata() const noexcept { return *display_; }
private:
  friend class SongPreparation;
  PreparedSong(std::shared_ptr<const CatalogSnapshot> snapshot, std::size_t index,
               SmfTimelineCompileResult compiled, std::shared_ptr<const KarLyricTimeline> lyrics,
               PreparationOptions options, LyricSelectionPolicy effectivePolicy,
               std::shared_ptr<const CatalogSourceMetadata> metadata,
               std::shared_ptr<const CatalogDisplayMetadata> display)
      : snapshot_(std::move(snapshot)), index_(index), compiled_(std::move(compiled)),
        lyrics_(std::move(lyrics)), options_(options), effectivePolicy_(effectivePolicy), metadata_(std::move(metadata)), display_(std::move(display)) {}
  std::shared_ptr<const CatalogSnapshot> snapshot_;
  std::size_t index_;
  SmfTimelineCompileResult compiled_;
  std::shared_ptr<const KarLyricTimeline> lyrics_;
  PreparationOptions options_;
  LyricSelectionPolicy effectivePolicy_;
  std::shared_ptr<const CatalogSourceMetadata> metadata_;
  std::shared_ptr<const CatalogDisplayMetadata> display_;
};
struct SongPreparationResult {
  std::shared_ptr<const PreparedSong> prepared;
  std::optional<PreparationError> error;
  bool succeeded() const noexcept { return static_cast<bool>(prepared); }
};

// Only SongDiscovery supplies its registered root. Serialized control-path I/O;
// hooks must not reenter library methods. No audio operation occurs here.
class SongPreparation {
private:
  friend class SongDiscovery;
  static SongPreparationResult prepare(const std::filesystem::path& root,
      std::shared_ptr<const CatalogSnapshot> snapshot, std::size_t index,
      PreparationOptions options, LyricSelectionPolicy effectivePolicy, const PreparationControl& control) {
    auto operation=PreparationOperation::Resolve;
    const auto& song=snapshot->songs[index];
    const auto fail=[&](PreparationErrorCode code) {
      return SongPreparationResult{nullptr,PreparationError{code,operation,song.locator(),{},{},{}}};
    };
    const auto cancelled=[&] { return control.cancelled && control.cancelled(); };
    try {
      if(options.smfBytes==0U || options.smfBytes>64U*1048576U
          || !isValidLyricSelectionPolicy(effectivePolicy) || !validKarLyricLimits(options.lyricLimits)
          || !MetadataPayloadBudget::create(options.stagedBytes))
        return fail(PreparationErrorCode::InvalidConfiguration);
      if(song.state!=CatalogState::Ready || !song.sourceRevision())
        return fail(PreparationErrorCode::NotReady);
      auto budget = *MetadataPayloadBudget::create(options.stagedBytes);
      if (!chargeCatalogPayload(budget, *snapshot)) return fail(PreparationErrorCode::LimitExceeded);
      // Reserve a possible error locator copy while extraction is still live.
      if (!budget.charge(song.locator().size())) return fail(PreparationErrorCode::LimitExceeded);
      if(cancelled()) return fail(PreparationErrorCode::Cancelled);
      std::error_code ec;
      if(std::filesystem::canonical(root,ec)!=root || ec
          || !std::filesystem::is_directory(root,ec) || ec)
        return fail(PreparationErrorCode::InvalidRoot);
      operation=PreparationOperation::Read;
      auto input=LibraryFilesystem::read(root,song.locator(),options.smfBytes);
      if(input.error!=LibraryFilesystem::ReadError::None)
        return fail(input.error==LibraryFilesystem::ReadError::LimitExceeded ? PreparationErrorCode::LimitExceeded :
          input.error==LibraryFilesystem::ReadError::AmbiguousPath ? PreparationErrorCode::AmbiguousPath :
          PreparationErrorCode::SourceUnreadable);
      if(sourceRevision(input.bytes)!=*song.sourceRevision()) return fail(PreparationErrorCode::SourceChanged);
      if(control.checkpoint) control.checkpoint(PreparationCheckpoint::AfterRead);
      if(cancelled()) return fail(PreparationErrorCode::Cancelled);
      operation=PreparationOperation::Compile;
      std::optional<SmfTimelineCompileResult> compiled;
      {
        const auto parsed=SmfParser::parse(input.bytes);
        if(!parsed.succeeded()) {
          auto result=fail(PreparationErrorCode::InvalidSmf);
          result.error->parseError=*parsed.error(); return result;
        }
        compiled.emplace(SmfTimelineCompiler::compile(*parsed.file()));
      } // Temporary parse storage is released before the verification read.
      if(!compiled->succeeded()) {
        auto result=fail(PreparationErrorCode::InvalidSmf);
        result.error->timelineError=*compiled->error(); return result;
      }
      operation=PreparationOperation::ExtractLyrics;
      auto extractionOptions = *karOptionsForPolicy(effectivePolicy, options.lyricLimits);
      extractionOptions.limits.stagedBytes = std::min(extractionOptions.limits.stagedBytes,
          std::max(std::size_t{1U}, budget.remaining() / 2U));
      const auto lyrics=KarLyricExtractor::extract(*compiled->timeline(),extractionOptions);
      if(!lyrics.succeeded()) {
        const auto code=lyrics.error()->code;
        auto result=fail(code==KarLyricErrorCode::LimitExceeded ? PreparationErrorCode::LimitExceeded :
          code==KarLyricErrorCode::InvalidConfiguration ? PreparationErrorCode::InvalidConfiguration :
          PreparationErrorCode::InvalidLyrics);
        result.error->lyricError=*lyrics.error(); return result;
      }
      if (!chargeLyricPayload(budget, *lyrics.timeline())) return fail(PreparationErrorCode::LimitExceeded);
      const auto metadata = compactSourceMetadata(*lyrics.timeline(), *song.sourceRevision(),
          effectivePolicy, options.lyricLimits.titleBytes, budget);
      if (metadata.error) {
        auto result = fail(metadata.error->code == KarLyricErrorCode::LimitExceeded
            ? PreparationErrorCode::LimitExceeded : PreparationErrorCode::InvalidConfiguration);
        result.error->lyricError = *metadata.error; return result;
      }
      const auto display = resolveCatalogDisplay(song.locator(), metadata.metadata.get(), song.overrides.get(),
          MetadataText::kMaxBytes, budget);
      if (!display.succeeded()) return fail(display.error->code == CatalogMetadataErrorCode::StorageFailure
          ? PreparationErrorCode::StorageFailure : display.error->code == CatalogMetadataErrorCode::LimitExceeded
          ? PreparationErrorCode::LimitExceeded : PreparationErrorCode::InvalidConfiguration);
      if(control.checkpoint) control.checkpoint(PreparationCheckpoint::AfterExtraction);
      if(cancelled()) return fail(PreparationErrorCode::Cancelled);
      operation=PreparationOperation::Verify;
      // Release the first raw buffer before rereading. The compiled result owns
      // its event payloads. This check is not an atomic filesystem snapshot.
      std::vector<std::uint8_t>().swap(input.bytes);
      const auto verified=LibraryFilesystem::read(root,song.locator(),options.smfBytes);
      if(verified.error!=LibraryFilesystem::ReadError::None
          || sourceRevision(verified.bytes)!=*song.sourceRevision())
        return fail(PreparationErrorCode::SourceChanged);
      if(cancelled()) return fail(PreparationErrorCode::Cancelled);
      operation=PreparationOperation::Publish;
      auto prepared=std::shared_ptr<const PreparedSong>(new PreparedSong(
          snapshot,index,std::move(*compiled),lyrics.timeline(),options,effectivePolicy,metadata.metadata,display.display));
      return {std::move(prepared),{}};
    } catch(const std::bad_alloc&) { return fail(PreparationErrorCode::StorageFailure); }
      catch(const std::filesystem::filesystem_error&) { return fail(PreparationErrorCode::SourceUnreadable); }
  }
};
} // namespace OpenHDK
