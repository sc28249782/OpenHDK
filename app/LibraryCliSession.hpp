// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "app/LibraryCliOutput.hpp"
#include "library/DurableLibraryService.hpp"
#include <functional>
#include <istream>
#include <ostream>

namespace OpenHDK {
struct LibraryCliResponse {
  LibraryCliOutputResult output;
  LibraryCliCommitOutcome commit = LibraryCliCommitOutcome::Unknown;
  int exit = 0;
  bool terminal = false;
};
// Owns one admitted service for a serialized management session. No provider,
// expectation, projection, mutable owner or arbitrary typed ID enters dispatch.
// An optional mutation-admission guard is evaluated at the dispatch boundary:
// when it returns false, mutating verbs (register, reattach, scan, relocate,
// remove, overrides) are rejected with a structured PlaybackActive error while
// query verbs (roots, songs, prepare, help, quit) remain available. The absent
// guard always admits, keeping the parser-only entry unchanged.
class LibraryCliSession {
public:
  explicit LibraryCliSession(std::unique_ptr<DurableLibraryService> service,
      std::function<bool()> mutationAllowed = {})
      : service_(std::move(service)), mutationAllowed_(std::move(mutationAllowed)) {}
  // Conservative peak reserve for input/token/command copies, record/batch/final
  // output coexistence and escaped display temporaries. This shares the service
  // allowance; these logical bytes are not a total process-memory bound.
  static constexpr std::size_t kCallerBytes = 3U*LibraryCliOutputBatch::kMaxBytes
      + 8U*kLibraryCliLineBytes + 4U*LibraryCliJsonRecord::kMaxBytes;
  LibraryCliCommitOutcome lastCommit() const noexcept { return lastCommit_; }
  // Read-only service access for the audio-enabled entry's preparation path.
  [[nodiscard]] DurableLibraryService& service() { return *service_; }
  LibraryCliResponse dispatch(std::string_view line) {
    lastCommit_=LibraryCliCommitOutcome::Unknown;
    try {
      if(!service_ || service_->state()!=DurableServiceState::Ready)
        return localError("RecoveryRequired",76,true);
      auto parsed=parseLibraryCommand(line);
      if(parsed.error) return grammarError(*parsed.error);
      const auto& c=*parsed.command;
      if(!playbackAllowsMutation_() && isMutatingVerb(c.verb))
        return localError("PlaybackActive");
      auto snapshot=service_->snapshot();
      const auto root=std::find_if(snapshot->roots.begin(),snapshot->roots.end(),
          [&](const auto& r){return c.root && r.id.diagnosticValue()==*c.root;});
      const auto song=std::find_if(snapshot->songs.begin(),snapshot->songs.end(),
          [&](const auto& s){return c.song && s.id.diagnosticValue()==*c.song;});
      if(c.root && root==snapshot->roots.end()) return localError("UnknownRoot");
      if(c.song && song==snapshot->songs.end()) return localError("NotFound");
      LibraryCliOutputBatch batch;
      LibraryCliJsonRecord record;
      if(c.verb==LibraryCliVerb::Help) {
        record.text("type","help");
        record.text("commands","help quit roots songs register reattach scan relocate remove overrides prepare");
      } else if(c.verb==LibraryCliVerb::Quit) {
        record.text("type","quit");batch.add(record);return {batch.finish(),lastCommit_,0,true};
      } else if(c.verb==LibraryCliVerb::Roots || c.verb==LibraryCliVerb::Songs) {
        auto budget=service_->queryBudget(kCallerBytes);
        if(!budget) return localError("LimitExceeded");
        record.text("type","page");record.number("revision",snapshot->revision);
        if(c.verb==LibraryCliVerb::Roots) {
          record.number("count",snapshot->roots.size());batch.add(record);
          for(const auto& r:snapshot->roots) {
            LibraryCliJsonRecord row;row.text("type","root");row.number("id",r.id.diagnosticValue());
            row.number("generation",r.attachmentGeneration);
            const auto attached=service_->rootAttached(r.id);
            if(!attached) return localError("RecoveryRequired",76,true);
            row.boolean("attached",*attached);
            row.text("encoding",r.policy.lyrics.encoding==LyricTextEncoding::Utf8?"utf8":"tis620");
            if(r.policy.lyrics.trackIndex) row.number("track",*r.policy.lyrics.trackIndex);else row.null("track");
            if(!batch.add(row)) return outputError(batch.finish());
          }
        } else {
          std::size_t total=0,emitted=0;
          for(const auto& s:snapshot->songs) if(s.root==root->id) ++total;
          record.number("root",root->id.diagnosticValue());record.number("offset",c.offset);
          record.number("total",total);record.number("count",c.offset<total?std::min(c.count,total-c.offset):0U);
          batch.add(record);std::size_t index=0;
          for(const auto& s:snapshot->songs) {
            if(s.root!=root->id) continue;
            if(index++<c.offset) continue;
            if(emitted++==c.count) break;
            auto display=resolveCatalogDisplay(s.locator(),s.metadata.get(),s.overrides.get(),4096U,*budget);
            if(!display.succeeded()) {
              lastCommit_=LibraryCliCommitOutcome::Rejected;
              LibraryCliJsonRecord error;error.text("type","error");error.text("family","metadata");
              error.number("code",static_cast<std::uint64_t>(display.error->code));
              error.number("field",static_cast<std::uint64_t>(display.error->field));
              error.nativeText("locator",s.locator());
              if(display.error->byteOffset) error.number("offset",*display.error->byteOffset);
              return {error.finish(),lastCommit_,1,false};
            }
            LibraryCliJsonRecord row;row.text("type","song");row.number("id",s.id.diagnosticValue());
            row.number("root",s.root.diagnosticValue());row.number("state",static_cast<std::uint64_t>(s.state));
            row.nativeText("locator",s.locator());row.text("title",display.display->title.text);
            row.number("title_origin",static_cast<std::uint64_t>(display.display->title.origin));
            if(display.display->artist) row.text("artist",display.display->artist->text);else row.null("artist");
            if(!batch.add(row)) return outputError(batch.finish());
          }
        }
        return complete(batch);
      } else if(c.verb==LibraryCliVerb::Prepare) {
        auto prepared=service_->prepare(song->id,{},{},kCallerBytes);
        if(prepared.error) return serviceError(*prepared.error);
        const auto& p=*prepared.prepared;
        record.text("type","prepared");record.number("song",p.song().id.diagnosticValue());
        record.number("revision",p.catalogSnapshot()->revision);
        record.number("event_count",p.midi().events().size());
        record.number("last_event_us",p.midi().events().empty()?0U:p.midi().events().back().timeMicroseconds());
        record.number("cue_count",p.lyrics()->cues().size());
        record.number("lyric_profile",static_cast<std::uint64_t>(p.lyrics()->profile()));
        if(p.lyrics()->trackIndex()) record.number("track",*p.lyrics()->trackIndex());else record.null("track");
        record.text("encoding",p.effectivePolicy().encoding==LyricTextEncoding::Utf8?"utf8":"tis620");
        record.number("source_bytes",p.song().sourceRevision()->byteCount);
        record.text("title",p.displayMetadata().title.text);
        record.number("title_origin",static_cast<std::uint64_t>(p.displayMetadata().title.origin));
        if(p.displayMetadata().artist) record.text("artist",p.displayMetadata().artist->text);else record.null("artist");
        batch.add(record);return complete(batch); // Query, no commit receipt or playback.
      }
      else if(c.verb==LibraryCliVerb::Register) {
        auto r=service_->registerRoot(std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(c.path.data()),c.path.size())),c.policy,{},kCallerBytes);
        if(r.error) return serviceError(*r.error);
        acknowledge(record,*r.receipt,false);record.number("root",r.root->diagnosticValue());
      } else if(c.verb==LibraryCliVerb::Reattach) {
        auto r=service_->reattachRoot(root->id,std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(c.path.data()),c.path.size())),{},kCallerBytes);
        if(r.error) return serviceError(*r.error);
        acknowledge(record,*r.receipt,r.status==RootReattachmentStatus::Unchanged);
      } else if(c.verb==LibraryCliVerb::Scan) {
        auto r=service_->scan(root->id,{},{},{},kCallerBytes);
        if(r.error) return serviceError(*r.error);
        acknowledge(record,*r.receipt,false);record.number("candidates",r.candidates);
        record.number("diagnostics",r.diagnostics.size());batch.add(record);
        for(const auto& d:r.diagnostics) {
          LibraryCliJsonRecord row;row.text("type","diagnostic");discovery(row,d);
          if(!batch.add(row)) return outputError(batch.finish());
        }
        return complete(batch);
      } else if(c.verb==LibraryCliVerb::Overrides) {
        CatalogOverrideRequest request;
        if(c.title) request.title=*c.title;
        if(c.artist) request.artist=*c.artist;
        auto r=service_->replaceUserOverrides(song->id,request,{},kCallerBytes);
        if(r.error) return serviceError(*r.error);
        acknowledge(record,*r.receipt,r.status==DurableOverrideStatus::NoChange);
      } else {
        auto r=c.verb==LibraryCliVerb::Remove?service_->removeSong(song->id,{},kCallerBytes)
          :service_->relocateSong(song->id,root->id,c.locator,{},kCallerBytes);
        if(r.error) return serviceError(*r.error);
        acknowledge(record,*r.receipt,false);
      }
      batch.add(record);return complete(batch);
    } catch(const std::bad_alloc&) {return localError("StorageFailure");}
      catch(const std::filesystem::filesystem_error&) {return localError("InvalidPath");}
  }
  LibraryCliResponse grammarError(LibraryCliError error) {
    lastCommit_=LibraryCliCommitOutcome::Rejected;
    LibraryCliJsonRecord row;row.text("type","error");row.text("family","grammar");
    row.number("code",static_cast<std::uint64_t>(error.code));row.number("offset",error.offset);
    return {row.finish(),LibraryCliCommitOutcome::Rejected,1,false};
  }
  static int factoryExit(const DurableServiceError& e) noexcept {
    if(e.store && e.store->code==StoreErrorCode::Busy) return 75;
    if(e.store && e.store->code==StoreErrorCode::UnsupportedStorage) return 69;
    return e.code==DurableServiceErrorCode::RecoveryRequired?76:1;
  }
  static LibraryCliOutputResult factoryError(const DurableServiceError& e) {
    LibraryCliJsonRecord row;row.text("type","error");fields(row,e);return row.finish();
  }
  // Complete batches are constructed before any delivery. A failed stream ends
  // dispatch; known commit state survives and no mutation is automatically retried.
  int run(std::istream& input,std::ostream& output) {
    int ordinary=0;LibraryCliLineReader reader;
    const auto deliver=[&](LibraryCliResponse r) {
      if(!r.output.bytes) return 74;
      try {output.write(r.output.bytes->data(),static_cast<std::streamsize>(r.output.bytes->size()));output.flush();}
      catch(const std::ios_base::failure&) {return 74;}
      if(!output) return 74;
      if(r.exit) ordinary=r.exit;
      return r.terminal?(r.exit?r.exit:ordinary):-1;
    };
    try {
      char byte{};
      while(input.get(byte)) {
        const auto status=reader.push(byte);
        if(status==LibraryCliLineReader::Status::More) continue;
        auto response=status==LibraryCliLineReader::Status::Rejected?grammarError(*reader.error()):dispatch(reader.line());
        const auto code=deliver(std::move(response));reader.reset();if(code>=0) return finish(code);
      }
      if(input.bad() || (!input.eof() && input.fail())) return finish(74);
      const auto status=reader.end();
      if(status!=LibraryCliLineReader::Status::More) {
        auto response=status==LibraryCliLineReader::Status::Rejected?grammarError(*reader.error()):dispatch(reader.line());
        const auto code=deliver(std::move(response));if(code>=0) return finish(code);
      }
      return finish(ordinary);
    } catch(const std::ios_base::failure&) {return finish(74);}
      catch(const std::bad_alloc&) {return finish(74);}
  }
private:
  int finish(int code) noexcept {if(service_) service_->close();return code;}
  // Mutation/play exclusion boundary (OHK-CLI-030 step 5): the guard is the
  // controller's libraryMutationAllowed() in the audio-enabled entry; absent
  // here it admits unconditionally.
  [[nodiscard]] bool playbackAllowsMutation_() const noexcept {
    return !mutationAllowed_ || mutationAllowed_();
  }
  [[nodiscard]] static bool isMutatingVerb(LibraryCliVerb verb) noexcept {
    return verb==LibraryCliVerb::Register || verb==LibraryCliVerb::Reattach
        || verb==LibraryCliVerb::Scan || verb==LibraryCliVerb::Relocate
        || verb==LibraryCliVerb::Remove || verb==LibraryCliVerb::Overrides;
  }
  static void discovery(LibraryCliJsonRecord& row,const DiscoveryDiagnostic& d) {
    row.number("discovery_code",static_cast<std::uint64_t>(d.code));
    row.number("discovery_operation",static_cast<std::uint64_t>(d.operation));row.nativeText("locator",d.locator);
    nested(row,d.parseError,d.timelineError,d.lyricError);
  }
  static void nested(LibraryCliJsonRecord& row,const std::optional<SmfParseError>& parse,
      const std::optional<SmfTimelineError>& timeline,const std::optional<KarLyricError>& lyric) {
    if(parse) {
      row.number("parse_code",static_cast<std::uint64_t>(parse->code));
      row.number("parse_offset",parse->offset);
    }
    if(timeline) {
      row.number("timeline_code",static_cast<std::uint64_t>(timeline->code()));
      if(timeline->trackIndex()) row.number("timeline_track",*timeline->trackIndex());
      if(const auto* track=timeline->trackDecodeError()) {
        row.number("track_code",static_cast<std::uint64_t>(track->code));row.number("track_offset",track->offset);
      }
    }
    if(lyric) {
      row.number("lyric_code",static_cast<std::uint64_t>(lyric->code));
      if(lyric->payloadByteOffset) row.number("lyric_offset",*lyric->payloadByteOffset);
      if(lyric->position) {
        row.number("lyric_track",lyric->position->trackIndex);
        row.number("lyric_event",lyric->position->sourceIndex);
      }
    }
  }
  static void fields(LibraryCliJsonRecord& row,const DurableServiceError& e) {
    row.text("family","service");row.number("code",static_cast<std::uint64_t>(e.code));
    row.number("operation",static_cast<std::uint64_t>(e.operation));
    if(e.root) row.number("root",e.root->diagnosticValue());
    if(e.catalog) row.number("catalog_code",static_cast<std::uint64_t>(*e.catalog));
    if(e.metadata) {
      row.number("metadata_code",static_cast<std::uint64_t>(e.metadata->code));
      row.number("field",static_cast<std::uint64_t>(e.metadata->field));
      if(e.metadata->byteOffset) row.number("offset",*e.metadata->byteOffset);
    }
    const auto checkpoint=e.checkpoint?e.checkpoint:(e.store?e.store->checkpoint:std::nullopt);
    if(checkpoint) {
      row.number("checkpoint_code",static_cast<std::uint64_t>(checkpoint->code));
      row.number("checkpoint_operation",static_cast<std::uint64_t>(checkpoint->operation));
      row.number("checkpoint_field",static_cast<std::uint64_t>(checkpoint->field));
      if(checkpoint->byteOffset) row.number("checkpoint_offset",*checkpoint->byteOffset);
      if(checkpoint->record) row.number("checkpoint_record",*checkpoint->record);
    }
    if(e.store) {
      row.number("store_code",static_cast<std::uint64_t>(e.store->code));
      row.number("store_operation",static_cast<std::uint64_t>(e.store->operation));
      row.number("store_outcome",static_cast<std::uint64_t>(e.store->outcome));
      row.text("native_error",std::to_string(e.store->nativeError));
    }
    if(e.binding) {
      row.number("binding_code",static_cast<std::uint64_t>(e.binding->code));
      row.number("binding_operation",static_cast<std::uint64_t>(e.binding->operation));
      if(e.binding->providerError) row.text("binding_native_error",std::to_string(e.binding->providerError->nativeError));
      if(e.binding->rootIndex) row.number("root_index",*e.binding->rootIndex);
    }
    if(e.discovery) discovery(row,*e.discovery);
    if(e.preparation) {
      row.number("preparation_code",static_cast<std::uint64_t>(e.preparation->code));
      row.number("preparation_operation",static_cast<std::uint64_t>(e.preparation->operation));
      row.nativeText("locator",e.preparation->locator);
      nested(row,e.preparation->parseError,e.preparation->timelineError,e.preparation->lyricError);
    }
    if(e.cleanupWarning) row.number("cleanup_warning",static_cast<std::uint64_t>(e.cleanupWarning->code));
  }
  LibraryCliResponse serviceError(const DurableServiceError& e) {
    lastCommit_=(e.store && e.store->outcome==StoreOutcome::Uncertain)
      || e.code==DurableServiceErrorCode::ProtocolFault?LibraryCliCommitOutcome::Uncertain:LibraryCliCommitOutcome::Rejected;
    LibraryCliJsonRecord row;row.text("type","error");fields(row,e);
    row.number("commit",static_cast<std::uint64_t>(lastCommit_));
    const bool terminal=service_->state()==DurableServiceState::RecoveryRequired;
    return {row.finish(),lastCommit_,terminal?76:1,terminal};
  }
  void acknowledge(LibraryCliJsonRecord& row,const DurableCommitReceipt& r,bool unchanged) {
    // Record known commit before any potentially allocating response formatting.
    lastCommit_=unchanged?LibraryCliCommitOutcome::NoChange:LibraryCliCommitOutcome::Saved;
    row.text("type","commit");row.text("status",unchanged?"NoChange":"Saved");
    row.number("revision",r.capturedRevision);row.number("sequence",r.token.sequence);
    if(r.cleanupWarning) row.number("cleanup_warning",static_cast<std::uint64_t>(r.cleanupWarning->code));
  }
  LibraryCliResponse localError(std::string_view error,int exit=1,bool terminal=false) {
    if(lastCommit_==LibraryCliCommitOutcome::Unknown) lastCommit_=LibraryCliCommitOutcome::Rejected;
    if(lastCommit_==LibraryCliCommitOutcome::Saved || lastCommit_==LibraryCliCommitOutcome::NoChange) {
      exit=74;terminal=true; // Formatting failure after a known acknowledgment.
    }
    LibraryCliJsonRecord row;row.text("type","error");row.text("error",error);
    row.number("commit",static_cast<std::uint64_t>(lastCommit_));
    return {row.finish(),lastCommit_,exit,terminal};
  }
  LibraryCliResponse outputError(LibraryCliOutputResult output) {
    const auto error=output.error;
    auto r=localError(error && error->code==LibraryCliErrorCode::StorageFailure?"StorageFailure":"LimitExceeded");
    // A complete small error retains the known commit; no partial page is emitted.
    return r;
  }
  LibraryCliResponse complete(const LibraryCliOutputBatch& batch) {
    auto output=batch.finish();if(output.error) return outputError(std::move(output));
    return {std::move(output),lastCommit_,0,false};
  }
  std::unique_ptr<DurableLibraryService> service_;
  std::function<bool()> mutationAllowed_;
  LibraryCliCommitOutcome lastCommit_=LibraryCliCommitOutcome::Unknown;
};
} // namespace OpenHDK
