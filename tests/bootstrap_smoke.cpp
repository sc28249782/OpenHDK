// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 OpenHDK contributors
#include "TestCheck.hpp"
#include "lyrics/LyricClockObserver.hpp"
#include "audio/FluidSynthBackend.hpp"
#include "audio/SmfParser.hpp"
#include "audio/SmfTimelineCompiler.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <thread>
#include <vector>
namespace OpenHDK {
struct FluidSynthBackendTestAccess {
  [[nodiscard]] static bool renderFrames(FluidSynthBackend& backend, float* samples,
                                         std::size_t frames) noexcept {
    return backend.renderFramesForTesting(samples, frames);
  }
};
}
namespace {
bool isLimitedPcm(std::span<const float> samples) {
  constexpr float ceilingTolerance = 1.0e-6F;
  return std::all_of(samples.begin(), samples.end(), [](float sample) {
    return std::isfinite(sample) && std::abs(sample) <= 0.98F + ceilingTolerance;
  });
}
std::filesystem::path writeMidi() {
  constexpr std::array<unsigned char, 45> bytes{
    'M','T','h','d',0,0,0,6,0,0,0,1,1,0xE0, 'M','T','r','k',0,0,0,0x17,
    0,0xFF,0x51,3,7,0xA1,0x20, 0,0xC0,0, 0,0x90,0x3C,0x64,
    0x83,0x60,0x80,0x3C,0, 0,0xFF,0x2F,0};
  const auto path = std::filesystem::temp_directory_path() / "openhdk-poc-middle-c.mid";
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  return path;
}
}
int main() {
  constexpr std::array<std::uint8_t, 45> compiledMidi{
    'M','T','h','d',0,0,0,6,0,0,0,1,1,0xE0, 'M','T','r','k',0,0,0,0x17,
    0,0xFF,0x51,3,7,0xA1,0x20, 0,0xC0,0, 0,0x90,0x3C,0x64,
    0x83,0x60,0x80,0x3C,0, 0,0xFF,0x2F,0};
  const auto parsedMidi = OpenHDK::SmfParser::parse(compiledMidi);
  OPENHDK_FAIL_IF(16, !parsedMidi.file());
  const auto compiledTimeline = OpenHDK::SmfTimelineCompiler::compile(*parsedMidi.file());
  OPENHDK_FAIL_IF(17, !compiledTimeline.timeline());
  constexpr std::array<std::uint8_t, 29> distantEndMidi{
    'M','T','h','d',0,0,0,6,0,0,0,1,1,0xE0,
    'M','T','r','k',0,0,0,7,0xFF,0xFF,0xFF,0x7F,0xFF,0x2F,0};
  const auto distantEndParsed = OpenHDK::SmfParser::parse(distantEndMidi);
  OPENHDK_FAIL_IF(69, !distantEndParsed.file());
  const auto distantEndTimeline = OpenHDK::SmfTimelineCompiler::compile(*distantEndParsed.file());
  OPENHDK_FAIL_IF(70, !distantEndTimeline.timeline());
  OpenHDK::FluidSynthBackend backend; OpenHDK::AudioBackendStatus status;
  const auto initialClock = backend.mediaClock();
  OPENHDK_FAIL_IF(61, initialClock.status != OpenHDK::MediaClockReadStatus::Snapshot
                      || !initialClock.snapshot.has_value()
                      || initialClock.snapshot->generation != 0U
                      || initialClock.snapshot->source != OpenHDK::MediaClockSource::None
                      || initialClock.snapshot->phase != OpenHDK::MediaClockPhase::Unavailable);
  OpenHDK::AudioBackendPlaybackStart rejectedStart{999U};
  OPENHDK_FAIL_IF(67, backend.playCompiledTimeline(*compiledTimeline.timeline(), rejectedStart, status)
                      || rejectedStart.generation != 0U);
  const auto rejectedClock = backend.mediaClock();
  OPENHDK_FAIL_IF(68, rejectedClock.status != OpenHDK::MediaClockReadStatus::Snapshot
                      || !rejectedClock.snapshot.has_value()
                      || rejectedClock.snapshot->revision != initialClock.snapshot->revision
                      || rejectedClock.snapshot->generation != 0U
                      || rejectedClock.snapshot->source != OpenHDK::MediaClockSource::None
                      || rejectedClock.snapshot->phase != OpenHDK::MediaClockPhase::Unavailable);
  std::vector<std::string> presetNames{"unchanged"};
  OPENHDK_FAIL_IF(48, backend.saveRuntimeMixerPreset("Default", status)
                      || backend.recallRuntimeMixerPreset("Default", status)
                      || backend.deleteRuntimeMixerPreset("Default", status)
                      || backend.listRuntimeMixerPresets(presetNames, status)
                      || presetNames != std::vector<std::string>{"unchanged"});
  OPENHDK_FAIL_IF(42, backend.initialize({.soundFontPath = "does-not-exist.sf2", .enableDeviceOutput = false,
                          .velocityCurve = static_cast<OpenHDK::MidiVelocityCurve>(999)}, status)
      || status.error != OpenHDK::AudioBackendError::InvalidConfiguration);
  OPENHDK_FAIL_IF(1, backend.info().id != "fluidsynth-miniaudio");
  OPENHDK_FAIL_IF(13, backend.initialize({.soundFontPath = "does-not-exist.sf2", .enableDeviceOutput = false,
                          .volume = std::numeric_limits<float>::quiet_NaN()}, status)
      || status.error != OpenHDK::AudioBackendError::InvalidConfiguration);
  OPENHDK_FAIL_IF(14, backend.initialize({.soundFontPath = "does-not-exist.sf2", .enableDeviceOutput = false,
                          .volume = std::numeric_limits<float>::infinity()}, status)
      || status.error != OpenHDK::AudioBackendError::InvalidConfiguration);
  OPENHDK_FAIL_IF(15, backend.initialize({.soundFontPath = "does-not-exist.sf2", .enableDeviceOutput = false,
                          .volume = -std::numeric_limits<float>::infinity()}, status)
      || status.error != OpenHDK::AudioBackendError::InvalidConfiguration);
  OPENHDK_FAIL_IF(7, backend.initialize({.soundFontPath = "does-not-exist.sf2", .enableDeviceOutput = false, .volume = 1.1F}, status)
      || status.error != OpenHDK::AudioBackendError::InvalidConfiguration);
  auto invalidChannelGains = OpenHDK::defaultMidiChannelGains();
  invalidChannelGains[0] = std::numeric_limits<float>::infinity();
  OPENHDK_FAIL_IF(22, backend.initialize({.soundFontPath = "does-not-exist.sf2", .enableDeviceOutput = false,
                          .channelGains = invalidChannelGains}, status)
      || status.error != OpenHDK::AudioBackendError::InvalidConfiguration);
  OPENHDK_FAIL_IF(2, backend.initialize({.soundFontPath = "does-not-exist.sf2", .enableDeviceOutput = false}, status)
      || status.error != OpenHDK::AudioBackendError::SoundFontNotFound);
  const auto midi = writeMidi();
  OPENHDK_FAIL_IF(3, !backend.initialize({.soundFontPath = OPENHDK_TEST_SOUNDFONT_PATH, .enableDeviceOutput = false,
                           .volume = 0.0F}, status) || backend.hasActiveDevice());
  OPENHDK_FAIL_IF(8, !backend.setVolume(1.0F, status) || backend.volume() != 1.0F
      || !backend.setVolume(0.0F, status) || backend.volume() != 0.0F
      || !backend.setVolume(0.5F, status) || backend.volume() != 0.5F
      || !backend.setMuted(true, status) || !backend.isMuted());
  OPENHDK_FAIL_IF(9, !backend.setMuted(false, status) || backend.isMuted() || backend.setVolume(1.1F, status)
      || backend.setVolume(std::numeric_limits<float>::quiet_NaN(), status)
      || backend.setVolume(std::numeric_limits<float>::infinity(), status)
      || backend.setVolume(-std::numeric_limits<float>::infinity(), status));
  OPENHDK_FAIL_IF(23, !backend.setRuntimeChannelGain(0U, 0.5F, status)
      || !backend.setRuntimeChannelMuted(0U, false, status)
      || !backend.setRuntimeChannelSoloed(0U, true, status)
      || !backend.setRuntimeChannelSoloed(0U, false, status)
      || backend.setRuntimeChannelGain(16U, 0.5F, status)
      || backend.setRuntimeChannelGain(0U, std::numeric_limits<float>::infinity(), status)
      || backend.setRuntimeChannelMuted(16U, true, status)
      || backend.setRuntimeChannelSoloed(16U, true, status));
  OpenHDK::AudioBackendPlaybackStart firstStart;
  OPENHDK_FAIL_IF(18, !backend.playCompiledTimeline(*compiledTimeline.timeline(), firstStart, status)
                      || !backend.isPlaying() || firstStart.generation == 0U);
  const auto firstStartClock = backend.mediaClock();
  OPENHDK_FAIL_IF(62, firstStartClock.status != OpenHDK::MediaClockReadStatus::Snapshot
                      || !firstStartClock.snapshot.has_value()
                      || firstStartClock.snapshot->generation != firstStart.generation
                      || firstStartClock.snapshot->mediaMicroseconds != 0U
                      || firstStartClock.snapshot->source != OpenHDK::MediaClockSource::CompiledTimeline
                      || firstStartClock.snapshot->phase != OpenHDK::MediaClockPhase::Playing);
  const auto clockLyrics = OpenHDK::KarLyricExtractor::extract(*compiledTimeline.timeline());
  OpenHDK::LyricClockObserver clockObserver;
  OPENHDK_FAIL_IF(76, !clockLyrics.succeeded()
      || !clockObserver.bind(clockLyrics.timeline(), firstStart)
      || clockObserver.poll(backend.mediaClock()).status != OpenHDK::LyricClockPollStatus::Advanced);
  OPENHDK_FAIL_IF(19, backend.playMidiFile(midi, status) || status.error != OpenHDK::AudioBackendError::MidiPlaybackFailed);
  std::array<float, 256> compiledPcm{};
  OPENHDK_FAIL_IF(24, !backend.setRuntimeChannelMuted(0U, true, status));
  OPENHDK_FAIL_IF(27, !backend.renderStereo(compiledPcm, status));
  const auto firstBlockClock = backend.mediaClock();
  OPENHDK_FAIL_IF(63, firstBlockClock.status != OpenHDK::MediaClockReadStatus::Snapshot
                      || !firstBlockClock.snapshot.has_value()
                      || firstBlockClock.snapshot->generation != firstStart.generation
                      || firstBlockClock.snapshot->mediaMicroseconds == 0U
                      || firstBlockClock.snapshot->phase != OpenHDK::MediaClockPhase::Playing);
  OPENHDK_FAIL_IF(77, clockObserver.poll(backend.mediaClock()).status
      != OpenHDK::LyricClockPollStatus::Advanced
      || clockObserver.mediaPositionMicroseconds() != firstBlockClock.snapshot->mediaMicroseconds);
  OPENHDK_FAIL_IF(36, !isLimitedPcm(compiledPcm));
  constexpr float runtimeMuteSilenceThreshold = 1.0e-6F;
  OPENHDK_FAIL_IF(28, std::any_of(compiledPcm.begin(), compiledPcm.end(), [runtimeMuteSilenceThreshold](float sample) {
        return std::abs(sample) > runtimeMuteSilenceThreshold;
      }));
  OPENHDK_FAIL_IF(29, !backend.setRuntimeChannelMuted(0U, false, status));
  for (std::size_t block = 0U; block < 1024U && backend.isPlaying(); ++block) {
    OPENHDK_FAIL_IF(20, !backend.renderStereo(compiledPcm, status));
    OPENHDK_FAIL_IF(36, !isLimitedPcm(compiledPcm));
  }
  OPENHDK_FAIL_IF(21, backend.isPlaying());
  const auto firstFinishedClock = backend.mediaClock();
  OPENHDK_FAIL_IF(64, firstFinishedClock.status != OpenHDK::MediaClockReadStatus::Snapshot
                      || !firstFinishedClock.snapshot.has_value()
                      || firstFinishedClock.snapshot->generation != firstStart.generation
                      || firstFinishedClock.snapshot->mediaMicroseconds < 500000U
                      || firstFinishedClock.snapshot->phase != OpenHDK::MediaClockPhase::Finished);
  OPENHDK_FAIL_IF(78, clockObserver.poll(backend.mediaClock()).status
      != OpenHDK::LyricClockPollStatus::Advanced
      || clockObserver.mediaPositionMicroseconds() != firstFinishedClock.snapshot->mediaMicroseconds);
  clockObserver.stop(); // Clear lyrics BEFORE the replacement request.
  OpenHDK::AudioBackendPlaybackStart failedRenderStart;
  OPENHDK_FAIL_IF(71, !backend.playCompiledTimeline(*distantEndTimeline.timeline(),
                                                    failedRenderStart, status)
                      || !backend.renderStereo(compiledPcm, status));
  const auto beforeFailedRender = backend.mediaClock();
  OPENHDK_FAIL_IF(72, beforeFailedRender.status != OpenHDK::MediaClockReadStatus::Snapshot
                      || !beforeFailedRender.snapshot.has_value()
                      || beforeFailedRender.snapshot->generation != failedRenderStart.generation
                      || beforeFailedRender.snapshot->mediaMicroseconds == 0U
                      || beforeFailedRender.snapshot->phase != OpenHDK::MediaClockPhase::Playing
                      || OpenHDK::FluidSynthBackendTestAccess::renderFrames(
                          backend, compiledPcm.data(),
                          static_cast<std::size_t>(std::numeric_limits<int>::max()) + 1U));
  const auto failedRenderClock = backend.mediaClock();
  OPENHDK_FAIL_IF(73, backend.isPlaying()
                      || failedRenderClock.status != OpenHDK::MediaClockReadStatus::Snapshot
                      || !failedRenderClock.snapshot.has_value()
                      || failedRenderClock.snapshot->generation != failedRenderStart.generation
                      || failedRenderClock.snapshot->mediaMicroseconds
                          != beforeFailedRender.snapshot->mediaMicroseconds
                      || failedRenderClock.snapshot->phase != OpenHDK::MediaClockPhase::Failed
                      || failedRenderClock.snapshot->failure != OpenHDK::MediaClockFailure::RenderFailed);
  OpenHDK::AudioBackendPlaybackStart stoppedStart;
  OPENHDK_FAIL_IF(74, !backend.playCompiledTimeline(*distantEndTimeline.timeline(),
                                                    stoppedStart, status)
                      || !backend.stopPlayback(status) || backend.isPlaying());
  const auto stoppedClock = backend.mediaClock();
  OPENHDK_FAIL_IF(75, stoppedClock.status != OpenHDK::MediaClockReadStatus::Snapshot
                      || !stoppedClock.snapshot.has_value()
                      || stoppedClock.snapshot->generation != stoppedStart.generation
                      || stoppedClock.snapshot->mediaMicroseconds != 0U
                      || stoppedClock.snapshot->source != OpenHDK::MediaClockSource::CompiledTimeline
                      || stoppedClock.snapshot->phase != OpenHDK::MediaClockPhase::Stopped
                      || stoppedClock.snapshot->failure != OpenHDK::MediaClockFailure::None);
  OpenHDK::AudioBackendPlaybackStart secondStart;
  OPENHDK_FAIL_IF(30, !backend.playCompiledTimeline(*compiledTimeline.timeline(), secondStart, status)
                      || !backend.isPlaying()
                      || secondStart.generation <= stoppedStart.generation);
  bool compiledHeard{};
  for (std::size_t block = 0U; block < 1024U && backend.isPlaying(); ++block) {
    OPENHDK_FAIL_IF(20, !backend.renderStereo(compiledPcm, status));
    compiledHeard = compiledHeard || std::any_of(compiledPcm.begin(), compiledPcm.end(), [](float x) { return x > 0.0001F || x < -0.0001F; });
    OPENHDK_FAIL_IF(36, !isLimitedPcm(compiledPcm));
  }
  OPENHDK_FAIL_IF(21, backend.isPlaying() || !compiledHeard);
  OPENHDK_FAIL_IF(26, !backend.resetRuntimeMixer(status));
  OPENHDK_FAIL_IF(49, !backend.saveRuntimeMixerPreset("Default", status));
  OPENHDK_FAIL_IF(4, !backend.playMidiFile(midi, status));
  const auto legacyClock = backend.mediaClock();
  OPENHDK_FAIL_IF(65, legacyClock.status != OpenHDK::MediaClockReadStatus::Snapshot
                      || !legacyClock.snapshot.has_value()
                      || legacyClock.snapshot->generation != 0U
                      || legacyClock.snapshot->mediaMicroseconds != 0U
                      || legacyClock.snapshot->source != OpenHDK::MediaClockSource::LegacyPlayer
                      || legacyClock.snapshot->phase != OpenHDK::MediaClockPhase::Unavailable);
  OPENHDK_FAIL_IF(25, backend.setRuntimeChannelMuted(0U, true, status)
      || status.error != OpenHDK::AudioBackendError::MidiPlaybackFailed);
  OPENHDK_FAIL_IF(50, backend.recallRuntimeMixerPreset("Default", status)
                      || status.error != OpenHDK::AudioBackendError::MidiPlaybackFailed);
  std::this_thread::sleep_for(std::chrono::milliseconds(25));
  std::vector<float> legacyPcm(4096U);
  OPENHDK_FAIL_IF(5, !backend.renderStereo(legacyPcm, status));
  OPENHDK_FAIL_IF(37, !isLimitedPcm(legacyPcm));
  const bool heardLegacy = std::any_of(legacyPcm.begin(), legacyPcm.end(), [](float x) { return x > 0.0001F || x < -0.0001F; });
  for (std::size_t block = 0U; block < 1024U && backend.isPlaying(); ++block) {
    OPENHDK_FAIL_IF(31, !backend.renderStereo(legacyPcm, status));
    OPENHDK_FAIL_IF(37, !isLimitedPcm(legacyPcm));
  }
  OPENHDK_FAIL_IF(32, !heardLegacy || backend.isPlaying());
  OpenHDK::AudioBackendPlaybackStart thirdStart;
  OPENHDK_FAIL_IF(33, !backend.playCompiledTimeline(*compiledTimeline.timeline(), thirdStart, status)
                      || !backend.isPlaying() || thirdStart.generation <= secondStart.generation);
  for (std::size_t block = 0U; block < 1024U && backend.isPlaying(); ++block) {
    OPENHDK_FAIL_IF(34, !backend.renderStereo(compiledPcm, status));
    OPENHDK_FAIL_IF(36, !isLimitedPcm(compiledPcm));
  }
  const bool completedCompiledAfterLegacy = !backend.isPlaying();
  // A dense, maximum-velocity chord stresses the shared output boundary at
  // maximum supported master gain without changing the runtime SoundFont.
  std::vector<std::uint8_t> chordTrack{0U, 0xc0U, 0U, 0U, 0xb0U, 7U, 127U};
  for (std::uint8_t note = 36U; note < 84U; ++note) {
    chordTrack.insert(chordTrack.end(), {0U, 0x90U, note, 127U});
  }
  chordTrack.insert(chordTrack.end(), {0x83U, 0x60U, 0xb0U, 120U, 0U,
                                      0U, 0xffU, 0x2fU, 0U});
  std::vector<std::uint8_t> chordMidi{
    'M','T','h','d',0,0,0,6,0,0,0,1,1,0xe0,'M','T','r','k'};
  for (const unsigned shift : {24U, 16U, 8U, 0U}) {
    chordMidi.push_back(static_cast<std::uint8_t>(chordTrack.size() >> shift));
  }
  chordMidi.insert(chordMidi.end(), chordTrack.begin(), chordTrack.end());
  const auto chordParsed = OpenHDK::SmfParser::parse(chordMidi);
  OPENHDK_FAIL_IF(38, !chordParsed.file());
  const auto chordTimeline = OpenHDK::SmfTimelineCompiler::compile(*chordParsed.file());
  OPENHDK_FAIL_IF(39, !chordTimeline.timeline() || !backend.setVolume(1.0F, status)
                    || !backend.playCompiledTimeline(*chordTimeline.timeline(), status));
  bool chordHeard{};
  for (std::size_t block = 0U; block < 1024U && backend.isPlaying(); ++block) {
    OPENHDK_FAIL_IF(40, !backend.renderStereo(compiledPcm, status)
                      || !isLimitedPcm(compiledPcm));
    chordHeard = chordHeard || std::any_of(compiledPcm.begin(), compiledPcm.end(), [](float sample) {
      return std::abs(sample) > 0.0001F;
    });
  }
  OPENHDK_FAIL_IF(41, backend.isPlaying() || !chordHeard);
  for (const auto curve : {OpenHDK::MidiVelocityCurve::Soft, OpenHDK::MidiVelocityCurve::Hard}) {
    OPENHDK_FAIL_IF(43, !backend.initialize({.soundFontPath = OPENHDK_TEST_SOUNDFONT_PATH,
                            .enableDeviceOutput = false, .velocityCurve = curve}, status));
    OPENHDK_FAIL_IF(44, backend.playMidiFile(midi, status)
                        || status.error != OpenHDK::AudioBackendError::MidiPlaybackFailed);
    OPENHDK_FAIL_IF(45, !backend.playCompiledTimeline(*compiledTimeline.timeline(), status));
    bool curveHeard{};
    for (std::size_t block = 0U; block < 1024U && backend.isPlaying(); ++block) {
      OPENHDK_FAIL_IF(46, !backend.renderStereo(compiledPcm, status) || !isLimitedPcm(compiledPcm));
      curveHeard = curveHeard || std::any_of(compiledPcm.begin(), compiledPcm.end(), [](float sample) {
        return std::abs(sample) > 0.0001F;
      });
    }
    OPENHDK_FAIL_IF(47, backend.isPlaying() || !curveHeard);
  }
  // CC7 changes while a saved mute preset is active, before a later note-on.
  // Recall must not clear source automation or prevent subsequent notes.
  const std::vector<std::uint8_t> presetTrack{
    0U,0xc0U,0U, 0U,0xb0U,7U,40U, 0U,0x90U,60U,100U,
    96U,0xb0U,7U,80U, 96U,0x90U,62U,100U,
    0x82U,0x20U,0xb0U,120U,0U, 0U,0xffU,0x2fU,0U};
  std::vector<std::uint8_t> presetMidi{
    'M','T','h','d',0,0,0,6,0,0,0,1,1,0xe0,'M','T','r','k'};
  for (const unsigned shift : {24U, 16U, 8U, 0U}) {
    presetMidi.push_back(static_cast<std::uint8_t>(presetTrack.size() >> shift));
  }
  presetMidi.insert(presetMidi.end(), presetTrack.begin(), presetTrack.end());
  const auto presetParsed = OpenHDK::SmfParser::parse(presetMidi);
  OPENHDK_FAIL_IF(51, !presetParsed.file());
  const auto presetTimeline = OpenHDK::SmfTimelineCompiler::compile(*presetParsed.file());
  OPENHDK_FAIL_IF(52, !presetTimeline.timeline());
  OPENHDK_FAIL_IF(53, !backend.initialize({.soundFontPath = OPENHDK_TEST_SOUNDFONT_PATH,
                              .enableDeviceOutput = false}, status)
                      || !backend.setRuntimeChannelGain(0U, 0.5F, status)
                      || !backend.saveRuntimeMixerPreset("Audible", status)
                      || !backend.setRuntimeChannelMuted(0U, true, status)
                      || !backend.saveRuntimeMixerPreset("Quiet", status)
                      || !backend.resetRuntimeMixer(status)
                      || !backend.setRuntimeChannelGain(0U, 0.5F, status)
                      || !backend.recallRuntimeMixerPreset("Quiet", status)
                      || !backend.playCompiledTimeline(*presetTimeline.timeline(), status));
  for (unsigned block = 0U; block < 40U; ++block) {
    OPENHDK_FAIL_IF(54, !backend.renderStereo(compiledPcm, status)
                        || std::any_of(compiledPcm.begin(), compiledPcm.end(), [](float sample) {
                          return !std::isfinite(sample) || std::abs(sample) > 1.0e-6F;
                        }));
  }
  OPENHDK_FAIL_IF(55, !backend.recallRuntimeMixerPreset("Audible", status));
  bool presetHeard{};
  for (std::size_t block = 0U; block < 1024U && backend.isPlaying(); ++block) {
    OPENHDK_FAIL_IF(56, !backend.renderStereo(compiledPcm, status) || !isLimitedPcm(compiledPcm));
    presetHeard = presetHeard || std::any_of(compiledPcm.begin(), compiledPcm.end(), [](float sample) {
      return std::abs(sample) > 0.0001F;
    });
  }
  OPENHDK_FAIL_IF(57, backend.isPlaying() || !presetHeard
                      || backend.saveRuntimeMixerPreset("1bad", status)
                      || backend.recallRuntimeMixerPreset("Missing", status)
                      || backend.deleteRuntimeMixerPreset("Missing", status));
  OPENHDK_FAIL_IF(58, !backend.listRuntimeMixerPresets(presetNames, status)
                      || presetNames != std::vector<std::string>{"Audible", "Default", "Quiet"});
  OPENHDK_FAIL_IF(59, !backend.deleteRuntimeMixerPreset("Quiet", status)
                      || backend.recallRuntimeMixerPreset("Quiet", status));
  backend.shutdown();
  OPENHDK_FAIL_IF(60, backend.recallRuntimeMixerPreset("Audible", status)
                      || !backend.initialize({.soundFontPath = OPENHDK_TEST_SOUNDFONT_PATH,
                                .enableDeviceOutput = false}, status)
                      || !backend.listRuntimeMixerPresets(presetNames, status)
                      || presetNames != std::vector<std::string>{"Audible", "Default"});
  backend.shutdown(); std::filesystem::remove(midi);
  const auto shutdownClock = backend.mediaClock();
  OPENHDK_FAIL_IF(66, shutdownClock.status != OpenHDK::MediaClockReadStatus::Snapshot
                      || !shutdownClock.snapshot.has_value()
                      || shutdownClock.snapshot->generation != 0U
                      || shutdownClock.snapshot->mediaMicroseconds != 0U
                      || shutdownClock.snapshot->source != OpenHDK::MediaClockSource::None
                      || shutdownClock.snapshot->phase != OpenHDK::MediaClockPhase::Unavailable);
  OPENHDK_FAIL_IF(35, !completedCompiledAfterLegacy);
  return 0;
}
