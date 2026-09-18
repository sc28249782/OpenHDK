// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 OpenHDK contributors
#pragma once

#include "audio/SmfByteReader.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace OpenHDK {

enum class SmfParseErrorCode {
    TruncatedHeader,
    InvalidHeaderChunk,
    InvalidHeaderLength,
    UnsupportedFormat,
    InvalidTrackCount,
    InvalidDivision,
    TruncatedTrackChunk,
    InvalidTrackChunk,
    TrailingData,
};

/// The offset is a zero-based byte index in the supplied input. For invalid
/// bytes or fields, it identifies the first invalid byte. For truncation, it
/// is bytes.size(): the first byte required by the parser but absent from input.
struct SmfParseError {
    SmfParseErrorCode code;
    std::size_t offset;

    [[nodiscard]] constexpr std::string_view message() const {
        switch (code) {
        case SmfParseErrorCode::TruncatedHeader: return "truncated MThd header";
        case SmfParseErrorCode::InvalidHeaderChunk: return "expected MThd header chunk";
        case SmfParseErrorCode::InvalidHeaderLength: return "MThd header length must be 6";
        case SmfParseErrorCode::UnsupportedFormat: return "only SMF formats 0 and 1 are supported";
        case SmfParseErrorCode::InvalidTrackCount: return "SMF track count is invalid for its format";
        case SmfParseErrorCode::InvalidDivision: return "SMF time division is invalid";
        case SmfParseErrorCode::TruncatedTrackChunk: return "truncated MTrk track chunk";
        case SmfParseErrorCode::InvalidTrackChunk: return "expected MTrk track chunk";
        case SmfParseErrorCode::TrailingData: return "data remains after declared MTrk chunks";
        }
        return "unknown SMF parse error";
    }
};

class SmfTrackChunk {
public:
    [[nodiscard]] std::span<const std::uint8_t> bytes() const { return bytes_; }

private:
    friend class SmfParser;
    explicit SmfTrackChunk(std::vector<std::uint8_t> bytes) : bytes_(std::move(bytes)) {}

    std::vector<std::uint8_t> bytes_;
};

class SmfFile {
public:
    [[nodiscard]] std::uint16_t format() const { return format_; }
    [[nodiscard]] std::uint16_t division() const { return division_; }
    [[nodiscard]] std::span<const SmfTrackChunk> tracks() const { return tracks_; }

private:
    friend class SmfParser;

    SmfFile(std::uint16_t format, std::uint16_t division, std::vector<SmfTrackChunk> tracks)
        : format_(format), division_(division), tracks_(std::move(tracks)) {}

    std::uint16_t format_;
    std::uint16_t division_;
    std::vector<SmfTrackChunk> tracks_;
};

class SmfParseResult {
public:
    [[nodiscard]] bool succeeded() const { return file_.has_value(); }
    [[nodiscard]] const SmfFile* file() const {
        return file_ ? &*file_ : nullptr;
    }
    [[nodiscard]] const SmfParseError* error() const {
        return error_ ? &*error_ : nullptr;
    }

private:
    friend class SmfParser;

    explicit SmfParseResult(SmfFile file) : file_(std::move(file)) {}
    explicit SmfParseResult(SmfParseError error) : error_(error) {}

    std::optional<SmfFile> file_;
    std::optional<SmfParseError> error_;
};

class SmfParser {
public:
    [[nodiscard]] static SmfParseResult parse(std::span<const std::uint8_t> bytes) {
        SmfByteReader reader(bytes);
        const auto headerChunk = reader.readBytes(4U);
        if (!headerChunk) return failure(SmfParseErrorCode::TruncatedHeader, bytes.size());
        if (const auto mismatch = firstMismatch(*headerChunk, "MThd")) {
            return failure(SmfParseErrorCode::InvalidHeaderChunk, *mismatch);
        }

        const auto headerLength = reader.readBigEndian32();
        if (!headerLength) return failure(SmfParseErrorCode::TruncatedHeader, bytes.size());
        if (*headerLength != 6U) return failure(SmfParseErrorCode::InvalidHeaderLength, 4U);

        const auto format = reader.readBigEndian16();
        const auto trackCount = reader.readBigEndian16();
        const auto division = reader.readBigEndian16();
        if (!format || !trackCount || !division) {
            return failure(SmfParseErrorCode::TruncatedHeader, bytes.size());
        }
        if (*format > 1U) return failure(SmfParseErrorCode::UnsupportedFormat, 8U);
        if (*trackCount == 0U || (*format == 0U && *trackCount != 1U)) {
            return failure(SmfParseErrorCode::InvalidTrackCount, 10U);
        }
        if (!validDivision(*division)) return failure(SmfParseErrorCode::InvalidDivision, 12U);

        std::vector<SmfTrackChunk> tracks;
        tracks.reserve(std::min<std::size_t>(*trackCount, reader.remaining() / 8U));
        for (std::uint16_t trackIndex = 0; trackIndex < *trackCount; ++trackIndex) {
            const auto trackChunk = reader.readBytes(4U);
            if (!trackChunk) return failure(SmfParseErrorCode::TruncatedTrackChunk, bytes.size());
            if (const auto mismatch = firstMismatch(*trackChunk, "MTrk")) {
                return failure(SmfParseErrorCode::InvalidTrackChunk,
                               reader.offset() - trackChunk->size() + *mismatch);
            }

            const auto trackLength = reader.readBigEndian32();
            if (!trackLength) return failure(SmfParseErrorCode::TruncatedTrackChunk, bytes.size());
            const auto trackBytes = reader.readBytes(*trackLength);
            if (!trackBytes) return failure(SmfParseErrorCode::TruncatedTrackChunk, bytes.size());
            tracks.push_back(SmfTrackChunk(
                std::vector<std::uint8_t>(trackBytes->begin(), trackBytes->end())));
        }
        if (reader.remaining() != 0U) return failure(SmfParseErrorCode::TrailingData, reader.offset());
        return SmfParseResult(SmfFile(*format, *division, std::move(tracks)));
    }

private:
    [[nodiscard]] static constexpr std::optional<std::size_t> firstMismatch(
        std::span<const std::uint8_t> bytes, std::string_view text) {
        if (bytes.size() != text.size()) return std::nullopt;
        for (std::size_t index = 0; index < bytes.size(); ++index) {
            if (bytes[index] != static_cast<std::uint8_t>(text[index])) return index;
        }
        return std::nullopt;
    }

    [[nodiscard]] static constexpr bool validDivision(std::uint16_t division) {
        if ((division & 0x8000U) == 0U) return division != 0U;
        const auto framesPerSecond = static_cast<std::uint8_t>(division >> 8U);
        const auto ticksPerFrame = static_cast<std::uint8_t>(division & 0x00ffU);
        return ticksPerFrame != 0U
            && (framesPerSecond == 0xe8U || framesPerSecond == 0xe7U
                || framesPerSecond == 0xe3U || framesPerSecond == 0xe2U);
    }

    [[nodiscard]] static SmfParseResult failure(SmfParseErrorCode code, std::size_t offset) {
        return SmfParseResult(SmfParseError{code, offset});
    }
};

} // namespace OpenHDK
