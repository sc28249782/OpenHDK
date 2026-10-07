// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 OpenHDK contributors
#pragma once

#include "audio/SmfTimelineCompiler.hpp"
#include "lyrics/LyricTextDecoder.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace OpenHDK {

enum class LyricDisplayKind { Text, LineBreak, ParagraphBreak };
struct LyricDisplayOperation {
  LyricDisplayKind kind;
  std::size_t byteOffset = 0U;
  std::size_t byteLength = 0U;
  bool operator==(const LyricDisplayOperation&) const = default;
};
struct LyricSourcePosition {
  std::uint64_t tick;
  std::uint64_t timeMicroseconds;
  std::size_t trackIndex;
  std::size_t sourceIndex;
  bool operator==(const LyricSourcePosition&) const = default;
};
struct KarLyricCue {
  LyricSourcePosition position;
  std::vector<std::uint8_t> raw;
  std::string decoded;
  // Text ranges address decoded bytes; breaks have zero length at their source
  // marker/newline offset. No views/pointers into a moved string are retained.
  std::vector<LyricDisplayOperation> operations;
};
struct KarLyricMetadata {
  LyricSourcePosition position;
  std::vector<std::uint8_t> raw;
  std::string decoded;
};
enum class KarLyricProfile { NoLyrics, LyricMeta, NamedText };
class KarLyricExtractor;
class KarLyricTimeline {
public:
  KarLyricProfile profile() const noexcept { return profile_; }
  std::optional<std::size_t> trackIndex() const noexcept { return track_; }
  std::span<const KarLyricCue> cues() const noexcept { return cues_; }
  std::span<const KarLyricMetadata> metadata() const noexcept { return metadata_; }
  const std::optional<std::string>& title() const noexcept { return title_; }
private:
  friend class KarLyricExtractor;
  KarLyricProfile profile_ = KarLyricProfile::NoLyrics;
  std::optional<std::size_t> track_;
  std::vector<KarLyricCue> cues_;
  std::vector<KarLyricMetadata> metadata_;
  std::optional<std::string> title_;
};

enum class KarLyricErrorCode {
  InvalidConfiguration, InvalidLyricTrack, AmbiguousLyricTrack, InvalidText, LimitExceeded
};
struct KarLyricError {
  KarLyricErrorCode code;
  std::optional<LyricSourcePosition> position = std::nullopt;
  std::optional<std::size_t> payloadByteOffset = std::nullopt;
};
struct KarLyricLimits {
  std::size_t sourceBytes = LyricTextLimits::kMaxSourceBytes;
  std::size_t cues = 100000U;
  std::size_t titleBytes = 4096U;
  std::size_t stagedBytes = LyricTextLimits::kMaxDecodedBytes;
};
struct KarLyricOptions {
  LyricTextEncoding encoding = LyricTextEncoding::Utf8;
  std::optional<std::size_t> trackIndex = std::nullopt;
  KarLyricLimits limits{};
};
class KarLyricExtractResult {
public:
  bool succeeded() const noexcept { return static_cast<bool>(timeline_); }
  std::shared_ptr<const KarLyricTimeline> timeline() const noexcept { return timeline_; }
  const std::optional<KarLyricError>& error() const noexcept { return error_; }
private:
  friend class KarLyricExtractor;
  explicit KarLyricExtractResult(std::shared_ptr<const KarLyricTimeline> timeline)
      : timeline_(std::move(timeline)) {}
  explicit KarLyricExtractResult(KarLyricError error) : error_(error) {}
  std::shared_ptr<const KarLyricTimeline> timeline_;
  std::optional<KarLyricError> error_;
};

// Allocating control-path operation on an already validated, compiled SMF.
// No I/O, source mutation, MIDI dispatch, or audio/consumer integration.
// Allocation exceptions propagate without returning a partial timeline.
class KarLyricExtractor {
public:
  static KarLyricExtractResult extract(const SmfTimeline& source, KarLyricOptions options = {}) {
    const auto& limits = options.limits;
    if ((options.encoding != LyricTextEncoding::Utf8 && options.encoding != LyricTextEncoding::Tis620)
        || limits.sourceBytes == 0U || limits.sourceBytes > LyricTextLimits::kMaxSourceBytes
        || limits.cues == 0U || limits.cues > 100000U
        || limits.titleBytes == 0U || limits.titleBytes > 4096U
        || limits.stagedBytes == 0U || limits.stagedBytes > LyricTextLimits::kMaxDecodedBytes) {
      return failure(KarLyricErrorCode::InvalidConfiguration);
    }
    // Every successfully compiled track contributes End-of-Track, including
    // an otherwise empty track. Track indices are contiguous compiler indices.
    std::size_t trackCount = 0U;
    for (const auto& e : source.events()) trackCount = std::max(trackCount, e.trackIndex() + 1U);
    if (options.trackIndex && *options.trackIndex >= trackCount) {
      return failure(KarLyricErrorCode::InvalidLyricTrack);
    }
    struct TrackFlags { bool lyric = false; bool named = false; bool text = false; };
    std::vector<TrackFlags> flags(trackCount);
    for (const auto& e : source.events()) {
      if (options.trackIndex && e.trackIndex() != *options.trackIndex) continue;
      const auto type = e.event().metaType();
      const auto bytes = e.event().data();
      if (type == 0x05U && !bytes.empty()) flags[e.trackIndex()].lyric = true;
      if (type == 0x03U && namedTrack(bytes)) flags[e.trackIndex()].named = true;
      if (type == 0x01U && !bytes.empty() && bytes.front() != '@') flags[e.trackIndex()].text = true;
    }
    auto output = std::shared_ptr<KarLyricTimeline>(new KarLyricTimeline);
    for (const bool lyricPass : {true, false}) {
      for (std::size_t i = 0U; i < flags.size(); ++i) {
        if (!(lyricPass ? flags[i].lyric : flags[i].named && flags[i].text)) continue;
        if (output->track_) return failure(KarLyricErrorCode::AmbiguousLyricTrack);
        output->track_ = i;
      }
      if (output->track_) {
        output->profile_ = lyricPass ? KarLyricProfile::LyricMeta : KarLyricProfile::NamedText;
        break;
      }
    }
    if (!output->track_) return KarLyricExtractResult(std::move(output));
    const auto selectedType = output->profile_ == KarLyricProfile::LyricMeta ? 0x05U : 0x01U;
    std::size_t sourceBytes = 0U;
    std::size_t stagedBytes = 0U;
    const auto charge = [&](std::size_t bytes) {
      if (bytes > limits.stagedBytes - stagedBytes) return false;
      stagedBytes += bytes;
      return true;
    };
    for (const auto& e : source.events()) {
      const auto bytes = e.event().data();
      if (e.trackIndex() != *output->track_ || e.event().metaType() != selectedType || bytes.empty()) continue;
      const LyricSourcePosition position{e.tick(), e.timeMicroseconds(), e.trackIndex(), e.sourceIndex()};
      const bool metadata = output->profile_ == KarLyricProfile::NamedText && bytes.front() == '@';
      if (bytes.size() > limits.sourceBytes - sourceBytes) {
        return failure(KarLyricErrorCode::LimitExceeded, position, limits.sourceBytes - sourceBytes);
      }
      sourceBytes += bytes.size();
      if (!metadata && output->cues_.size() >= limits.cues) {
        return failure(KarLyricErrorCode::LimitExceeded, position);
      }
      // Charge owned raw and fixed record storage before decoding/copying.
      if (!charge(bytes.size()) || !charge(metadata ? sizeof(KarLyricMetadata) : sizeof(KarLyricCue))
          || stagedBytes == limits.stagedBytes) {
        return failure(KarLyricErrorCode::LimitExceeded, position);
      }
      auto decoded = decodeLyricText(bytes, options.encoding,
                                    {limits.sourceBytes, limits.stagedBytes - stagedBytes});
      if (!decoded.succeeded()) {
        const auto code = decoded.error()->code == LyricTextErrorCode::InvalidText
                        ? KarLyricErrorCode::InvalidText : KarLyricErrorCode::LimitExceeded;
        return failure(code, position, decoded.error()->byteOffset);
      }
      if (!charge(decoded.text()->size())) return failure(KarLyricErrorCode::LimitExceeded, position);
      if (metadata) {
        const auto& text = *decoded.text();
        if (!output->title_ && text.starts_with("@T") && text.size() > 2U) {
          if (text.size() - 2U > limits.titleBytes || !charge(text.size() - 2U)) {
            return failure(KarLyricErrorCode::LimitExceeded, position, 2U);
          }
          output->title_ = text.substr(2U);
        }
        output->metadata_.push_back({position, {bytes.begin(), bytes.end()}, *decoded.text()});
        continue;
      }
      KarLyricCue cue{position, {}, *decoded.text(), {}};
      // The decoder result is copied into retained storage; its temporary
      // output is bounded per call and is released after this event.
      std::size_t cursor = 0U;
      const auto operation = [&](LyricDisplayKind kind, std::size_t offset, std::size_t length = 0U) {
        if (!charge(sizeof(LyricDisplayOperation))) return false;
        cue.operations.push_back({kind, offset, length});
        return true;
      };
      if (output->profile_ == KarLyricProfile::NamedText) {
        while (cursor < cue.decoded.size() && (cue.decoded[cursor] == '/' || cue.decoded[cursor] == '\\')) {
          if (!operation(cue.decoded[cursor] == '/' ? LyricDisplayKind::LineBreak
                                                   : LyricDisplayKind::ParagraphBreak, cursor)) {
            return failure(KarLyricErrorCode::LimitExceeded, position);
          }
          ++cursor;
        }
      }
      while (cursor < cue.decoded.size()) {
        const auto start = cursor;
        if (cue.decoded[cursor] == '\r' || cue.decoded[cursor] == '\n') {
          if (!operation(LyricDisplayKind::LineBreak, cursor)) return failure(KarLyricErrorCode::LimitExceeded, position);
          const bool cr = cue.decoded[cursor++] == '\r';
          if (cr && cursor < cue.decoded.size() && cue.decoded[cursor] == '\n') ++cursor;
        } else {
          while (cursor < cue.decoded.size() && cue.decoded[cursor] != '\r' && cue.decoded[cursor] != '\n') ++cursor;
          if (!operation(LyricDisplayKind::Text, start, cursor - start)) return failure(KarLyricErrorCode::LimitExceeded, position);
        }
      }
      cue.raw.assign(bytes.begin(), bytes.end());
      output->cues_.push_back(std::move(cue));
    }
    return KarLyricExtractResult(std::move(output));
  }
private:
  static KarLyricExtractResult failure(KarLyricErrorCode code,
      std::optional<LyricSourcePosition> position = std::nullopt,
      std::optional<std::size_t> offset = std::nullopt) {
    return KarLyricExtractResult(KarLyricError{code, position, offset});
  }
  static bool namedTrack(std::span<const std::uint8_t> bytes) noexcept {
    while (!bytes.empty() && (bytes.front() == ' ' || bytes.front() == '\t')) bytes = bytes.subspan(1U);
    while (!bytes.empty() && (bytes.back() == ' ' || bytes.back() == '\t')) bytes = bytes.first(bytes.size() - 1U);
    for (const std::string_view name : {"words", "lyrics"}) {
      if (bytes.size() != name.size()) continue;
      bool equal = true;
      for (std::size_t i = 0U; i < bytes.size(); ++i) {
        const auto c = bytes[i] >= 'A' && bytes[i] <= 'Z' ? bytes[i] + ('a' - 'A') : bytes[i];
        if (c != name[i]) equal = false;
      }
      if (equal) return true;
    }
    return false;
  }
};

} // namespace OpenHDK
