// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 OpenHDK contributors
#include "audio/SmfParser.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <span>
#include <type_traits>

namespace {

using OpenHDK::SmfParseErrorCode;

template <std::size_t Size>
bool expectsSuccess(const std::array<std::uint8_t, Size>& fixture, std::uint16_t format,
                    std::uint16_t division, std::size_t tracks) {
    const auto result = OpenHDK::SmfParser::parse(fixture);
    const auto* file = result.file();
    if (!result.succeeded() || !file || result.error()) return false;
    return file->format() == format && file->division() == division
        && file->tracks().size() == tracks;
}

template <std::size_t Size>
bool expectsError(const std::array<std::uint8_t, Size>& fixture, SmfParseErrorCode expected,
                  std::size_t expectedOffset) {
    const auto result = OpenHDK::SmfParser::parse(fixture);
    const auto* error = result.error();
    return !result.succeeded() && !result.file() && error && error->code == expected
        && error->offset == expectedOffset && !error->message().empty();
}

} // namespace

int main() {
    static_assert(!std::is_default_constructible_v<OpenHDK::SmfFile>);
    static_assert(!std::is_default_constructible_v<OpenHDK::SmfParseResult>);

    constexpr std::array<std::uint8_t, 26> format0{
        'M','T','h','d', 0,0,0,6, 0,0, 0,1, 0,96,
        'M','T','r','k', 0,0,0,4, 0,0xff,0x2f,0};
    if (!expectsSuccess(format0, 0U, 96U, 1U)) return 1;
    auto mutableFormat0 = format0;
    const auto ownedResult = OpenHDK::SmfParser::parse(mutableFormat0);
    mutableFormat0[22] = 0x7fU;
    if (!ownedResult.file() || ownedResult.file()->tracks()[0].bytes()[0] != 0U) return 7;

    constexpr std::array<std::uint8_t, 34> format1{
        'M','T','h','d', 0,0,0,6, 0,1, 0,2, 1,0xe0,
        'M','T','r','k', 0,0,0,2, 0,0xff,
        'M','T','r','k', 0,0,0,2, 0,0xff};
    if (!expectsSuccess(format1, 1U, 480U, 2U)) return 2;

    constexpr std::array<std::uint8_t, 7> truncatedHeader{'M','T','h','d', 0,0,0};
    if (!expectsError(truncatedHeader, SmfParseErrorCode::TruncatedHeader, 7U)) return 3;

    constexpr std::array<std::uint8_t, 8> invalidHeaderLength{
        'M','T','h','d', 0,0,0,5};
    if (!expectsError(invalidHeaderLength, SmfParseErrorCode::InvalidHeaderLength, 4U)) return 8;

    constexpr std::array<std::uint8_t, 4> invalidMThdId{'M','T','x','d'};
    if (!expectsError(invalidMThdId, SmfParseErrorCode::InvalidHeaderChunk, 2U)) return 19;

    constexpr std::array<std::uint8_t, 14> unsupportedFormat{
        'M','T','h','d', 0,0,0,6, 0,2, 0,1, 0,96};
    if (!expectsError(unsupportedFormat, SmfParseErrorCode::UnsupportedFormat, 8U)) return 4;

    constexpr std::array<std::uint8_t, 14> invalidDivision{
        'M','T','h','d', 0,0,0,6, 0,0, 0,1, 0,0};
    if (!expectsError(invalidDivision, SmfParseErrorCode::InvalidDivision, 12U)) return 5;

    constexpr std::array<std::uint8_t, 22> truncatedTrack{
        'M','T','h','d', 0,0,0,6, 0,0, 0,1, 0,96,
        'M','T','r','k', 0,0,0,1};
    if (!expectsError(truncatedTrack, SmfParseErrorCode::TruncatedTrackChunk, 22U)) return 6;

    constexpr std::array<std::uint8_t, 3> partialMThdId{'M','T','h'};
    if (!expectsError(partialMThdId, SmfParseErrorCode::TruncatedHeader, 3U)) return 9;

    constexpr std::array<std::uint8_t, 6> partialMThdLength{'M','T','h','d', 0,0};
    if (!expectsError(partialMThdLength, SmfParseErrorCode::TruncatedHeader, 6U)) return 10;

    constexpr std::array<std::uint8_t, 12> partialMThdFields{
        'M','T','h','d', 0,0,0,6, 0,0, 0,1};
    if (!expectsError(partialMThdFields, SmfParseErrorCode::TruncatedHeader, 12U)) return 11;

    constexpr std::array<std::uint8_t, 9> partialMThdFormat{
        'M','T','h','d', 0,0,0,6, 0};
    if (!expectsError(partialMThdFormat, SmfParseErrorCode::TruncatedHeader, 9U)) return 20;

    constexpr std::array<std::uint8_t, 11> partialMThdTrackCount{
        'M','T','h','d', 0,0,0,6, 0,0, 0};
    if (!expectsError(partialMThdTrackCount, SmfParseErrorCode::TruncatedHeader, 11U)) return 21;

    constexpr std::array<std::uint8_t, 13> partialMThdDivision{
        'M','T','h','d', 0,0,0,6, 0,0, 0,1, 0};
    if (!expectsError(partialMThdDivision, SmfParseErrorCode::TruncatedHeader, 13U)) return 22;

    constexpr std::array<std::uint8_t, 17> partialMTrkId{
        'M','T','h','d', 0,0,0,6, 0,0, 0,1, 0,96, 'M','T','r'};
    if (!expectsError(partialMTrkId, SmfParseErrorCode::TruncatedTrackChunk, 17U)) return 12;

    constexpr std::array<std::uint8_t, 20> partialMTrkLength{
        'M','T','h','d', 0,0,0,6, 0,0, 0,1, 0,96, 'M','T','r','k', 0,0};
    if (!expectsError(partialMTrkLength, SmfParseErrorCode::TruncatedTrackChunk, 20U)) return 13;

    constexpr std::array<std::uint8_t, 23> truncatedPayload{
        'M','T','h','d', 0,0,0,6, 0,0, 0,1, 0,96,
        'M','T','r','k', 0,0,0,2, 0};
    if (!expectsError(truncatedPayload, SmfParseErrorCode::TruncatedTrackChunk, 23U)) return 14;

    constexpr std::array<std::uint8_t, 22> invalidMTrkId{
        'M','T','h','d', 0,0,0,6, 0,0, 0,1, 0,96,
        'M','T','x','k', 0,0,0,0};
    if (!expectsError(invalidMTrkId, SmfParseErrorCode::InvalidTrackChunk, 16U)) return 15;

    constexpr std::array<std::uint8_t, 14> format0TwoTracks{
        'M','T','h','d', 0,0,0,6, 0,0, 0,2, 0,96};
    if (!expectsError(format0TwoTracks, SmfParseErrorCode::InvalidTrackCount, 10U)) return 16;

    constexpr std::array<std::uint8_t, 14> format1ZeroTracks{
        'M','T','h','d', 0,0,0,6, 0,1, 0,0, 0,96};
    if (!expectsError(format1ZeroTracks, SmfParseErrorCode::InvalidTrackCount, 10U)) return 17;

    constexpr std::array<std::uint8_t, 27> trailingData{
        'M','T','h','d', 0,0,0,6, 0,0, 0,1, 0,96,
        'M','T','r','k', 0,0,0,4, 0,0xff,0x2f,0, 0};
    if (!expectsError(trailingData, SmfParseErrorCode::TrailingData, 26U)) return 18;

    std::cout << "SMF parser fixtures passed\n";
    return 0;
}
