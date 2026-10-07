// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 OpenHDK contributors

#include "audio/MediaClockPublication.hpp"
#include "tests/TestCheck.hpp"

#include <atomic>
#include <cstdint>
#include <limits>
#include <thread>
#include <type_traits>

namespace {

using namespace OpenHDK;

constexpr MediaClockPayload kPlaying{
    11U, 101U, MediaClockSource::CompiledTimeline,
    MediaClockPhase::Playing, MediaClockFailure::None};
constexpr MediaClockPayload kFailed{
    22U, 202U, MediaClockSource::CompiledTimeline,
    MediaClockPhase::Failed, MediaClockFailure::RenderFailed};

[[nodiscard]] bool matches(const MediaClockSnapshot& snapshot,
                           const MediaClockPayload& payload) {
    return static_cast<const MediaClockPayload&>(snapshot) == payload;
}

} // namespace

int main() {
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
    static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
    static_assert(std::is_trivially_copyable_v<MediaClockPayload>);
    static_assert(noexcept(std::declval<MediaClockPublicationCell&>().publish(kPlaying)));
    static_assert(noexcept(std::declval<const MediaClockPublicationCell&>().tryRead()));

    MediaClockPublicationCell cell;
    const auto initial = cell.tryRead();
    OPENHDK_FAIL_IF(1, initial.status != MediaClockReadStatus::Snapshot
        || !initial.snapshot.has_value());
    OPENHDK_FAIL_IF(2, initial.snapshot->revision != 0U
        || initial.snapshot->generation != 0U
        || initial.snapshot->mediaMicroseconds != 0U
        || initial.snapshot->source != MediaClockSource::None
        || initial.snapshot->phase != MediaClockPhase::Unavailable
        || initial.snapshot->failure != MediaClockFailure::None);

    OPENHDK_FAIL_IF(3, cell.publish(kPlaying) != MediaClockPublishStatus::Published);
    const auto first = cell.tryRead();
    OPENHDK_FAIL_IF(4, first.status != MediaClockReadStatus::Snapshot
        || !first.snapshot.has_value() || first.snapshot->revision != 2U
        || !matches(*first.snapshot, kPlaying));

    bool observedOdd{};
    const auto hooked = MediaClockPublicationTestAccess::publishWithHook(
        cell, kFailed, [&]() noexcept {
            const auto during = cell.tryRead();
            observedOdd = during.status == MediaClockReadStatus::Unstable
                && !during.snapshot.has_value();
        });
    OPENHDK_FAIL_IF(5, hooked != MediaClockPublishStatus::Published || !observedOdd);
    const auto second = cell.tryRead();
    OPENHDK_FAIL_IF(6, second.status != MediaClockReadStatus::Snapshot
        || !second.snapshot.has_value() || second.snapshot->revision != 4U
        || !matches(*second.snapshot, kFailed));

    PlaybackGenerationCounter generations;
    const auto generation1 = generations.admit();
    const auto generation2 = generations.admit();
    OPENHDK_FAIL_IF(7, generation1 != 1U || generation2 != 2U
        || generations.current() != 2U);

    auto nearGenerationEnd = MediaClockPublicationTestAccess::generationAt(
        std::numeric_limits<std::uint64_t>::max() - 1U);
    const auto lastGeneration = nearGenerationEnd.admit();
    OPENHDK_FAIL_IF(8, lastGeneration != std::numeric_limits<std::uint64_t>::max());
    OPENHDK_FAIL_IF(9, nearGenerationEnd.admit().has_value()
        || nearGenerationEnd.current() != std::numeric_limits<std::uint64_t>::max());

    auto nearSequenceEnd = MediaClockPublicationTestAccess::cellAtSequence(
        std::numeric_limits<std::uint64_t>::max() - 3U);
    OPENHDK_FAIL_IF(10, nearSequenceEnd.publish(kPlaying) != MediaClockPublishStatus::Published);
    const auto lastPublication = nearSequenceEnd.tryRead();
    OPENHDK_FAIL_IF(11, lastPublication.status != MediaClockReadStatus::Snapshot
        || !lastPublication.snapshot.has_value()
        || lastPublication.snapshot->revision != std::numeric_limits<std::uint64_t>::max() - 1U
        || !matches(*lastPublication.snapshot, kPlaying));
    OPENHDK_FAIL_IF(12, nearSequenceEnd.publish(kFailed) != MediaClockPublishStatus::Exhausted);
    const auto exhausted = nearSequenceEnd.tryRead();
    OPENHDK_FAIL_IF(13, exhausted.status != MediaClockReadStatus::Exhausted
        || exhausted.snapshot.has_value());
    OPENHDK_FAIL_IF(14, nearSequenceEnd.publish(kPlaying) != MediaClockPublishStatus::Exhausted);

    MediaClockPublicationCell concurrent;
    OPENHDK_FAIL_IF(15, concurrent.publish(kPlaying) != MediaClockPublishStatus::Published);
    std::atomic<bool> writerDone{};
    std::atomic<bool> readerReady{};
    std::atomic<bool> invalidSnapshot{};
    std::atomic<std::uint32_t> acceptedSnapshots{};

    std::thread reader([&]() {
        readerReady.store(true, std::memory_order_release);
        while (!writerDone.load(std::memory_order_acquire)) {
            const auto read = concurrent.tryRead();
            if (read.status == MediaClockReadStatus::Unstable) continue;
            if (read.status != MediaClockReadStatus::Snapshot || !read.snapshot.has_value()
                || (read.snapshot->revision & 1U) != 0U
                || (!matches(*read.snapshot, kPlaying) && !matches(*read.snapshot, kFailed))) {
                invalidSnapshot.store(true, std::memory_order_release);
                return;
            }
            acceptedSnapshots.fetch_add(1U, std::memory_order_relaxed);
        }
    });

    while (!readerReady.load(std::memory_order_acquire)) std::this_thread::yield();
    for (std::uint32_t iteration = 0U; iteration < 100000U; ++iteration) {
        const auto& payload = (iteration & 1U) == 0U ? kFailed : kPlaying;
        if (concurrent.publish(payload) != MediaClockPublishStatus::Published) {
            invalidSnapshot.store(true, std::memory_order_release);
            break;
        }
        if ((iteration & 255U) == 0U) std::this_thread::yield();
    }
    writerDone.store(true, std::memory_order_release);
    reader.join();

    OPENHDK_FAIL_IF(16, invalidSnapshot.load(std::memory_order_acquire));
    OPENHDK_FAIL_IF(17, acceptedSnapshots.load(std::memory_order_acquire) == 0U);
    const auto finalSnapshot = concurrent.tryRead();
    OPENHDK_FAIL_IF(18, finalSnapshot.status != MediaClockReadStatus::Snapshot
        || !finalSnapshot.snapshot.has_value()
        || !matches(*finalSnapshot.snapshot, kPlaying));

    return 0;
}
