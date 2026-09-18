// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 OpenHDK contributors
#pragma once

#include "audio/SmfParser.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace OpenHDK {

enum class SmfMidiEventKind {
    NoteOff,
    NoteOn,
    Controller,
    ProgramChange,
    PitchBend,
    Tempo,
    EndOfTrack,
    Meta,
    SysEx,
};

enum class SmfTrackDecodeErrorCode {
    TruncatedEvent,
    MalformedVlq,
    TickOverflow,
    MissingRunningStatus,
    UnsupportedChannelEvent,
    InvalidChannelData,
    InvalidMetaLength,
    InvalidSystemEvent,
    MissingEndOfTrack,
    TrailingDataAfterEndOfTrack,
};

/// The offset is a zero-based byte index in the supplied track payload. For an
/// invalid byte or field it identifies its first byte. For truncation, it is
/// payload.size(): the first byte required by the decoder but absent from input.
/// A successful result contains exactly one End-of-Track event, which is the
/// final event and consumes the payload. Bytes after End-of-Track are reported
/// at their first byte; a missing End-of-Track is reported at payload.size().
struct SmfTrackDecodeError {
    SmfTrackDecodeErrorCode code;
    std::size_t offset;

    [[nodiscard]] constexpr std::string_view message() const {
        switch (code) {
        case SmfTrackDecodeErrorCode::TruncatedEvent: return "truncated track event";
        case SmfTrackDecodeErrorCode::MalformedVlq: return "malformed variable-length quantity";
        case SmfTrackDecodeErrorCode::TickOverflow: return "absolute tick position overflows";
        case SmfTrackDecodeErrorCode::MissingRunningStatus: return "channel data has no running status";
        case SmfTrackDecodeErrorCode::UnsupportedChannelEvent: return "unsupported channel event";
        case SmfTrackDecodeErrorCode::InvalidChannelData: return "channel event data byte has status bit set";
        case SmfTrackDecodeErrorCode::InvalidMetaLength: return "meta event has an invalid data length";
        case SmfTrackDecodeErrorCode::InvalidSystemEvent: return "unsupported system event";
        case SmfTrackDecodeErrorCode::MissingEndOfTrack: return "track is missing End-of-Track";
        case SmfTrackDecodeErrorCode::TrailingDataAfterEndOfTrack:
            return "data remains after End-of-Track";
        }
        return "unknown track decode error";
    }
};

class SmfMidiEvent {
public:
    [[nodiscard]] std::uint64_t tick() const { return tick_; }
    [[nodiscard]] SmfMidiEventKind kind() const { return kind_; }
    [[nodiscard]] std::optional<std::uint8_t> channel() const { return channel_; }
    [[nodiscard]] std::optional<std::uint8_t> metaType() const { return metaType_; }
    [[nodiscard]] std::optional<std::uint8_t> systemStatus() const { return systemStatus_; }
    [[nodiscard]] std::span<const std::uint8_t> data() const { return data_; }

private:
    friend class SmfTrackEventDecoder;

    SmfMidiEvent(std::uint64_t tick, SmfMidiEventKind kind,
                 std::optional<std::uint8_t> channel, std::optional<std::uint8_t> metaType,
                 std::optional<std::uint8_t> systemStatus, std::vector<std::uint8_t> data)
        : tick_(tick), kind_(kind), channel_(channel), metaType_(metaType),
          systemStatus_(systemStatus), data_(std::move(data)) {}

    std::uint64_t tick_;
    SmfMidiEventKind kind_;
    std::optional<std::uint8_t> channel_;
    std::optional<std::uint8_t> metaType_;
    std::optional<std::uint8_t> systemStatus_;
    std::vector<std::uint8_t> data_;
};

class SmfTrackEventList {
public:
    [[nodiscard]] std::span<const SmfMidiEvent> events() const { return events_; }

private:
    friend class SmfTrackEventDecoder;

    explicit SmfTrackEventList(std::vector<SmfMidiEvent> events) : events_(std::move(events)) {}

    std::vector<SmfMidiEvent> events_;
};

class SmfTrackDecodeResult {
public:
    [[nodiscard]] bool succeeded() const { return events_.has_value(); }
    [[nodiscard]] const SmfTrackEventList* events() const {
        return events_ ? &*events_ : nullptr;
    }
    [[nodiscard]] const SmfTrackDecodeError* error() const {
        return error_ ? &*error_ : nullptr;
    }

private:
    friend class SmfTrackEventDecoder;

    explicit SmfTrackDecodeResult(SmfTrackEventList events) : events_(std::move(events)) {}
    explicit SmfTrackDecodeResult(SmfTrackDecodeError error) : error_(error) {}

    std::optional<SmfTrackEventList> events_;
    std::optional<SmfTrackDecodeError> error_;
};

class SmfTrackEventDecoder {
public:
    [[nodiscard]] static SmfTrackDecodeResult decode(const SmfTrackChunk& track) {
        return decode(track.bytes());
    }

    [[nodiscard]] static SmfTrackDecodeResult decode(std::span<const std::uint8_t> payload) {
        SmfByteReader reader(payload);
        std::vector<SmfMidiEvent> events;
        std::uint64_t tick = 0U;
        std::optional<std::uint8_t> runningStatus;

        while (reader.remaining() != 0U) {
            const auto deltaOffset = reader.offset();
            const auto delta = readVlq(reader, payload);
            if (delta.status != VlqStatus::Success) return vlqFailure(delta);
            if (tick > std::numeric_limits<std::uint64_t>::max() - delta.value) {
                return failure(SmfTrackDecodeErrorCode::TickOverflow, deltaOffset);
            }
            tick += delta.value;

            const auto eventByte = reader.readByte();
            if (!eventByte) return failure(SmfTrackDecodeErrorCode::TruncatedEvent, payload.size());

            std::uint8_t status = *eventByte;
            std::vector<std::uint8_t> data;
            if (status < 0x80U) {
                if (!runningStatus) {
                    return failure(SmfTrackDecodeErrorCode::MissingRunningStatus, reader.offset() - 1U);
                }
                status = *runningStatus;
                data.push_back(*eventByte);
            } else if (status <= 0xefU) {
                runningStatus = status;
            } else {
                runningStatus.reset();
                if (status == 0xffU) {
                    const auto meta = decodeMeta(reader, payload, tick, events);
                    if (meta.error) {
                        return failure(meta.error->code, meta.error->offset);
                    }
                    if (meta.endOfTrack) {
                        if (reader.remaining() != 0U) {
                            return failure(SmfTrackDecodeErrorCode::TrailingDataAfterEndOfTrack,
                                           reader.offset());
                        }
                        return SmfTrackDecodeResult(SmfTrackEventList(std::move(events)));
                    }
                    continue;
                }
                if (status == 0xf0U || status == 0xf7U) {
                    if (const auto error = decodeSysEx(reader, payload, tick, status, events)) {
                        return failure(error->code, error->offset);
                    }
                    continue;
                }
                return failure(SmfTrackDecodeErrorCode::InvalidSystemEvent, reader.offset() - 1U);
            }

            const auto kind = channelKind(status);
            if (!kind) {
                return failure(SmfTrackDecodeErrorCode::UnsupportedChannelEvent,
                               reader.offset() - data.size() - 1U);
            }
            const auto expectedDataBytes = channelDataLength(status);
            while (data.size() < expectedDataBytes) {
                const auto dataByte = reader.readByte();
                if (!dataByte) return failure(SmfTrackDecodeErrorCode::TruncatedEvent, payload.size());
                if ((*dataByte & 0x80U) != 0U) {
                    return failure(SmfTrackDecodeErrorCode::InvalidChannelData, reader.offset() - 1U);
                }
                data.push_back(*dataByte);
            }
            events.push_back(SmfMidiEvent(tick, *kind, static_cast<std::uint8_t>(status & 0x0fU),
                                          std::nullopt, std::nullopt, std::move(data)));
        }
        return failure(SmfTrackDecodeErrorCode::MissingEndOfTrack, payload.size());
    }

private:
    enum class VlqStatus { Success, Truncated, Malformed };

    struct VlqResult {
        VlqStatus status;
        std::uint32_t value;
        std::size_t offset;
    };

    struct MetaDecodeResult {
        std::optional<SmfTrackDecodeError> error;
        bool endOfTrack;
    };

    [[nodiscard]] static VlqResult readVlq(SmfByteReader& reader,
                                            std::span<const std::uint8_t> payload) {
        std::uint32_t value = 0U;
        for (std::size_t count = 0U; count < 4U; ++count) {
            const auto byte = reader.readByte();
            if (!byte) return {VlqStatus::Truncated, 0U, payload.size()};
            value = (value << 7U) | (*byte & 0x7fU);
            if ((*byte & 0x80U) == 0U) return {VlqStatus::Success, value, 0U};
        }
        return {VlqStatus::Malformed, 0U, reader.offset() - 1U};
    }

    [[nodiscard]] static SmfTrackDecodeResult vlqFailure(const VlqResult& result) {
        return failure(result.status == VlqStatus::Malformed ? SmfTrackDecodeErrorCode::MalformedVlq
                                                              : SmfTrackDecodeErrorCode::TruncatedEvent,
                       result.offset);
    }

    [[nodiscard]] static MetaDecodeResult decodeMeta(
        SmfByteReader& reader, std::span<const std::uint8_t> payload, std::uint64_t tick,
        std::vector<SmfMidiEvent>& events) {
        const auto metaType = reader.readByte();
        if (!metaType) {
            return {SmfTrackDecodeError{SmfTrackDecodeErrorCode::TruncatedEvent, payload.size()}, false};
        }
        const auto lengthOffset = reader.offset();
        const auto length = readVlq(reader, payload);
        if (length.status != VlqStatus::Success) {
            return {SmfTrackDecodeError{length.status == VlqStatus::Malformed
                                            ? SmfTrackDecodeErrorCode::MalformedVlq
                                            : SmfTrackDecodeErrorCode::TruncatedEvent,
                                        length.offset}, false};
        }
        if ((*metaType == 0x51U && length.value != 3U)
            || (*metaType == 0x2fU && length.value != 0U)) {
            return {SmfTrackDecodeError{SmfTrackDecodeErrorCode::InvalidMetaLength, lengthOffset}, false};
        }
        const auto data = reader.readBytes(length.value);
        if (!data) {
            return {SmfTrackDecodeError{SmfTrackDecodeErrorCode::TruncatedEvent, payload.size()}, false};
        }
        const auto kind = *metaType == 0x51U ? SmfMidiEventKind::Tempo
                        : *metaType == 0x2fU ? SmfMidiEventKind::EndOfTrack
                                             : SmfMidiEventKind::Meta;
        events.push_back(SmfMidiEvent(tick, kind, std::nullopt, *metaType, std::nullopt,
                                      std::vector<std::uint8_t>(data->begin(), data->end())));
        return {std::nullopt, *metaType == 0x2fU};
    }

    [[nodiscard]] static std::optional<SmfTrackDecodeError> decodeSysEx(
        SmfByteReader& reader, std::span<const std::uint8_t> payload, std::uint64_t tick,
        std::uint8_t status, std::vector<SmfMidiEvent>& events) {
        const auto length = readVlq(reader, payload);
        if (length.status != VlqStatus::Success) {
            return SmfTrackDecodeError{length.status == VlqStatus::Malformed
                                           ? SmfTrackDecodeErrorCode::MalformedVlq
                                           : SmfTrackDecodeErrorCode::TruncatedEvent,
                                       length.offset};
        }
        const auto data = reader.readBytes(length.value);
        if (!data) return SmfTrackDecodeError{SmfTrackDecodeErrorCode::TruncatedEvent, payload.size()};
        events.push_back(SmfMidiEvent(tick, SmfMidiEventKind::SysEx, std::nullopt, std::nullopt,
                                      status, std::vector<std::uint8_t>(data->begin(), data->end())));
        return std::nullopt;
    }

    [[nodiscard]] static std::optional<SmfMidiEventKind> channelKind(std::uint8_t status) {
        switch (status & 0xf0U) {
        case 0x80U: return SmfMidiEventKind::NoteOff;
        case 0x90U: return SmfMidiEventKind::NoteOn;
        case 0xb0U: return SmfMidiEventKind::Controller;
        case 0xc0U: return SmfMidiEventKind::ProgramChange;
        case 0xe0U: return SmfMidiEventKind::PitchBend;
        default: return std::nullopt;
        }
    }

    [[nodiscard]] static std::size_t channelDataLength(std::uint8_t status) {
        return (status & 0xf0U) == 0xc0U ? 1U : 2U;
    }

    [[nodiscard]] static SmfTrackDecodeResult failure(SmfTrackDecodeErrorCode code,
                                                        std::size_t offset) {
        return SmfTrackDecodeResult(SmfTrackDecodeError{code, offset});
    }
};

} // namespace OpenHDK
