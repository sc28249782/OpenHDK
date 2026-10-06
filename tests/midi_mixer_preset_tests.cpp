// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 OpenHDK contributors
#include "audio/MidiMixerPresets.hpp"
#include "tests/TestCheck.hpp"

#include <atomic>
#include <string>
#include <thread>

using OpenHDK::MidiMixerPresetResult;
using OpenHDK::MidiMixerPresets;
using OpenHDK::MidiRuntimeMixer;

int main() {
    MidiRuntimeMixer mixer;
    MidiMixerPresets presets;
    constexpr auto success = MidiMixerPresetResult::Success;
    constexpr auto invalid = MidiMixerPresetResult::InvalidName;
    constexpr auto missing = MidiMixerPresetResult::NotFound;
    OPENHDK_FAIL_IF(1, !presets.names().empty());
    const std::string longest(32U, 'A');
    OPENHDK_FAIL_IF(2, presets.save(longest, mixer) != success);
    OPENHDK_FAIL_IF(3, presets.save("Drums-10_Only", mixer) != success);
    OPENHDK_FAIL_IF(4, presets.save("drums-10_Only", mixer) != success);
    OPENHDK_FAIL_IF(5, presets.names() != std::vector<std::string>{longest, "Drums-10_Only", "drums-10_Only"});
    const auto beforeInvalid = mixer.snapshot();
    for (const std::string& name : {std::string{}, std::string{"1bad"}, std::string{"a b"},
                                  std::string{"a/b"}, std::string{"a.b"}, std::string(33U, 'a'),
                                  std::string{"a\0b", 3U}, std::string{"a\x80", 2U}}) {
        OPENHDK_FAIL_IF(6, presets.save(name, mixer) != invalid);
        OPENHDK_FAIL_IF(7, presets.recall(name, mixer) != invalid);
        OPENHDK_FAIL_IF(8, presets.erase(name) != invalid);
    }
    OPENHDK_FAIL_IF(9, presets.recall("Missing", mixer) != missing);
    OPENHDK_FAIL_IF(10, presets.erase("Missing") != missing);
    OPENHDK_FAIL_IF(11, mixer.snapshot().revision != beforeInvalid.revision
                       || presets.names().size() != 3U);

    // Save every mute bit and a solo bit, then overwrite the same name.
    OPENHDK_FAIL_IF(12, !mixer.setChannelFlags(0xffffU, 0x8000U));
    OPENHDK_FAIL_IF(13, presets.save("All", mixer) != success);
    OPENHDK_FAIL_IF(14, !mixer.setChannelFlags(0x5555U, 0xaaaaU));
    OPENHDK_FAIL_IF(15, presets.save("All", mixer) != success);
    mixer.reset();
    for (std::size_t channel = 0U; channel < OpenHDK::kMidiChannelCount; ++channel) {
        OPENHDK_FAIL_IF(16, !mixer.setGain(channel, static_cast<float>(channel) / 16.0F));
    }
    const auto beforeRecall = mixer.snapshot();
    OPENHDK_FAIL_IF(17, presets.recall("All", mixer) != success);
    const auto recalled = mixer.snapshot();
    OPENHDK_FAIL_IF(18, recalled.mutedChannels != 0x5555U || recalled.soloedChannels != 0xaaaaU);
    OPENHDK_FAIL_IF(19, recalled.revision != beforeRecall.revision + 1U
                       || recalled.gains != beforeRecall.gains);
    OPENHDK_FAIL_IF(20, presets.recall("All", mixer) != success
                       || mixer.revision() != recalled.revision + 1U);
    const auto beforeDelete = mixer.snapshot();
    OPENHDK_FAIL_IF(21, presets.erase("All") != success
                       || presets.recall("All", mixer) != missing
                       || presets.erase("All") != missing);
    OPENHDK_FAIL_IF(22, mixer.revision() != beforeDelete.revision
                       || mixer.snapshot().gains != beforeDelete.gains);
    OPENHDK_FAIL_IF(23, mixer.setChannelFlags(0x10000U, 0U)
                       || mixer.setChannelFlags(0U, 0x10000U)
                       || mixer.revision() != beforeDelete.revision);
    // Existing channel setters must modify only their own packed bit.
    OPENHDK_FAIL_IF(24, !mixer.setMuted(15U, true) || !mixer.setSoloed(0U, true));
    OPENHDK_FAIL_IF(25, mixer.snapshot().mutedChannels != 0xd555U
                       || mixer.snapshot().soloedChannels != 0xaaabU);
    mixer.reset();
    OPENHDK_FAIL_IF(26, !mixer.isDefault() || presets.names().size() != 3U);
    OPENHDK_FAIL_IF(27, !mixer.setChannelFlags(0xffffU, 0xffffU)
                       || presets.save("Full", mixer) != success);
    mixer.reset();
    OPENHDK_FAIL_IF(28, presets.recall("Full", mixer) != success
                       || mixer.snapshot().mutedChannels != 0xffffU
                       || mixer.snapshot().soloedChannels != 0xffffU);

    // One control writer recalls two distinct pairs while a render reader
    // snapshots. Neither mixed pair is a valid published state.
    OPENHDK_FAIL_IF(29, !mixer.setChannelFlags(0x5555U, 0xaaaaU)
                       || presets.save("First", mixer) != success
                       || !mixer.setChannelFlags(0xaaaaU, 0x5555U)
                       || presets.save("Second", mixer) != success);
    std::atomic<bool> start{};
    std::atomic<bool> done{};
    std::atomic<bool> failed{};
    std::thread reader([&] {
        start.store(true, std::memory_order_release);
        do {
            const auto snapshot = mixer.snapshot();
            const bool first = snapshot.mutedChannels == 0x5555U && snapshot.soloedChannels == 0xaaaaU;
            const bool second = snapshot.mutedChannels == 0xaaaaU && snapshot.soloedChannels == 0x5555U;
            if (!first && !second) failed.store(true, std::memory_order_relaxed);
        } while (!done.load(std::memory_order_acquire));
    });
    while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
    for (unsigned iteration = 0U; iteration < 100000U; ++iteration) {
        if (presets.recall(iteration % 2U == 0U ? "First" : "Second", mixer) != success)
            failed.store(true, std::memory_order_relaxed);
    }
    done.store(true, std::memory_order_release);
    reader.join();
    OPENHDK_FAIL_IF(30, failed.load(std::memory_order_relaxed));
    return 0;
}
