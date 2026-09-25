// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 OpenHDK contributors
#include "audio/FluidSynthBackend.hpp"
#include "audio/PlaybackSession.hpp"
#include "audio/SmfMidiEventDispatcher.hpp"
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fluidsynth.h>
#include <limits>
#include <miniaudio.h>
namespace OpenHDK {
namespace {
constexpr std::size_t kChannels = 2;
constexpr std::uint64_t kMicrosecondsPerSecond = 1000000U;
static_assert(std::atomic<bool>::is_always_lock_free);
void ok(AudioBackendStatus& s) { s = {}; }
void fail(AudioBackendStatus& s, AudioBackendError e, std::string text) { s.error = e; s.message = std::move(text); }
class FluidSynthMidiCommandSink final : public MidiCommandSink {
public:
  FluidSynthMidiCommandSink(fluid_synth_t* synth, MidiChannelGains channelGains,
                            const MidiRuntimeMixer* runtimeMixer)
      : synth_(synth), channelGains_(std::move(channelGains)), runtimeMixer_(runtimeMixer) {
    sourceVolumes_.fill(kMidiDefaultChannelVolume);
    if (runtimeMixer_ != nullptr) {
      mixerSnapshot_ = runtimeMixer_->snapshot();
      mixerRevision_ = mixerSnapshot_.revision;
    }
  }
  void resetChannelVolumes() {
    sourceVolumes_.fill(kMidiDefaultChannelVolume);
    for (std::size_t channel = 0U; channel < kMidiChannelCount; ++channel) {
      sendScaledChannelVolume(static_cast<std::uint8_t>(channel));
    }
  }
  void synchronizeRuntimeMixer() {
    if (runtimeMixer_ == nullptr || runtimeMixer_->revision() == mixerRevision_) return;
    const auto previousSnapshot = mixerSnapshot_;
    mixerSnapshot_ = runtimeMixer_->snapshot();
    mixerRevision_ = mixerSnapshot_.revision;
    for (std::size_t channel = 0U; channel < kMidiChannelCount; ++channel) {
      if (previousSnapshot.outputGain(channel) != 0.0F
          && mixerSnapshot_.outputGain(channel) == 0.0F) {
        fluid_synth_cc(synth_, static_cast<int>(channel), 120, 0);
      }
      sendScaledChannelVolume(static_cast<std::uint8_t>(channel));
    }
  }
  void noteOn(std::uint8_t channel, std::uint8_t note, std::uint8_t velocity) override {
    if (mixerSnapshot_.outputGain(channel) == 0.0F) return;
    fluid_synth_noteon(synth_, channel, note, velocity);
  }
  void noteOff(std::uint8_t channel, std::uint8_t note, std::uint8_t) override { fluid_synth_noteoff(synth_, channel, note); }
  void controller(std::uint8_t channel, std::uint8_t controller, std::uint8_t value) override {
    if (controller == kMidiChannelVolumeController) {
      sourceVolumes_[channel] = value;
      sendScaledChannelVolume(channel);
      return;
    }
    fluid_synth_cc(synth_, channel, controller, value);
    if (controller == kMidiResetAllControllers) {
      sourceVolumes_[channel] = kMidiDefaultChannelVolume;
      sendScaledChannelVolume(channel);
    }
  }
  void programChange(std::uint8_t channel, std::uint8_t program) override { fluid_synth_program_change(synth_, channel, program); }
  void pitchBend(std::uint8_t channel, std::uint16_t value) override { fluid_synth_pitch_bend(synth_, channel, value); }
private:
  void sendScaledChannelVolume(std::uint8_t channel) {
    const auto gain = channelGains_[channel] * mixerSnapshot_.outputGain(channel);
    fluid_synth_cc(synth_, channel, kMidiChannelVolumeController,
                   applyMidiChannelGain(sourceVolumes_[channel], gain));
  }
  fluid_synth_t* synth_;
  MidiChannelGains channelGains_;
  const MidiRuntimeMixer* runtimeMixer_;
  MidiRuntimeMixerSnapshot mixerSnapshot_{};
  std::uint32_t mixerRevision_{};
  std::array<std::uint8_t, kMidiChannelCount> sourceVolumes_{};
};
}
struct FluidSynthBackend::Impl {
  fluid_settings_t* settings{}; fluid_synth_t* synth{}; fluid_player_t* player{}; ma_context context{}; ma_device device{}; PlaybackSession session{}; std::unique_ptr<FluidSynthMidiCommandSink> midiSink{}; MidiRuntimeMixer runtimeMixer{}; MidiChannelGains channelGains{defaultMidiChannelGains()}; float volume{1.0F}; std::uint32_t sampleRate{}; bool muted{}; std::atomic<bool> sessionActive{false}; bool contextInitialized{}; bool deviceInitialized{}; bool initialized{};
  void silenceActiveSounds() {
    if (synth == nullptr) return;
    for (int channel = 0; channel < 16; ++channel) {
      fluid_synth_all_notes_off(synth, channel);
      fluid_synth_all_sounds_off(synth, channel);
    }
  }
  void discardSession() {
    silenceActiveSounds();
    if (session.state() != PlaybackSessionState::Idle) static_cast<void>(session.stop());
    sessionActive.store(false, std::memory_order_release);
  }
  bool eventFrameOffset(const PlaybackSessionRenderResult& renderResult,
                        std::uint64_t eventTimeMicroseconds, std::size_t frames,
                        std::size_t& offset) const {
    if (eventTimeMicroseconds <= renderResult.blockStartMicroseconds()) {
      offset = 0U;
      return true;
    }
    if (eventTimeMicroseconds >= renderResult.blockEndMicroseconds()) {
      offset = frames;
      return true;
    }
    const auto deltaMicroseconds = eventTimeMicroseconds - renderResult.blockStartMicroseconds();
    const auto wholeSeconds = deltaMicroseconds / kMicrosecondsPerSecond;
    const auto remainderMicroseconds = deltaMicroseconds % kMicrosecondsPerSecond;
    if (wholeSeconds > std::numeric_limits<std::uint64_t>::max() / sampleRate) return false;
    const auto wholeFrames = wholeSeconds * sampleRate;
    const auto fractionalNumerator = remainderMicroseconds * sampleRate;
    const auto fractionalFrames = fractionalNumerator / kMicrosecondsPerSecond
        + (fractionalNumerator % kMicrosecondsPerSecond == 0U ? 0U : 1U);
    if (wholeFrames > std::numeric_limits<std::uint64_t>::max() - fractionalFrames) return false;
    const auto unclampedOffset = wholeFrames + fractionalFrames;
    offset = static_cast<std::size_t>(std::min(unclampedOffset, static_cast<std::uint64_t>(frames)));
    return true;
  }
  bool renderSynthFrames(float* samples, std::size_t frames) {
    if (frames > static_cast<std::size_t>(std::numeric_limits<int>::max())) return false;
    return frames == 0U || fluid_synth_write_float(synth, static_cast<int>(frames), samples, 0, 2,
                                                    samples, 1, 2) == FLUID_OK;
  }
  bool renderFrames(float* samples, std::size_t frames) {
    if (synth == nullptr) return false;
    if (sessionActive.load(std::memory_order_acquire)) {
      midiSink->synchronizeRuntimeMixer();
      const auto renderResult = session.render(frames);
      if (!renderResult.succeeded()) {
        discardSession();
        return false;
      }
      const auto events = renderResult.events();
      std::size_t renderedFrames = 0U;
      std::size_t eventIndex = 0U;
      while (eventIndex < events.size()) {
        std::size_t eventOffset{};
        if (!eventFrameOffset(renderResult, events[eventIndex].timeMicroseconds(), frames, eventOffset)
            || eventOffset < renderedFrames
            || !renderSynthFrames(samples + renderedFrames * kChannels, eventOffset - renderedFrames)) {
          discardSession();
          return false;
        }
        std::size_t dispatchEnd = eventIndex + 1U;
        while (dispatchEnd < events.size()) {
          std::size_t nextOffset{};
          if (!eventFrameOffset(renderResult, events[dispatchEnd].timeMicroseconds(), frames, nextOffset)) {
            discardSession();
            return false;
          }
          if (nextOffset != eventOffset) break;
          ++dispatchEnd;
        }
        SmfMidiEventDispatcher::dispatch(events.subspan(eventIndex, dispatchEnd - eventIndex), *midiSink);
        renderedFrames = eventOffset;
        eventIndex = dispatchEnd;
      }
      if (!renderSynthFrames(samples + renderedFrames * kChannels, frames - renderedFrames)) {
        discardSession();
        return false;
      }
      if (session.endOfTimelineReached()) {
        const auto completion = session.completeReleaseTail();
        if (!completion.succeeded()) {
          discardSession();
          return false;
        }
        silenceActiveSounds();
        sessionActive.store(false, std::memory_order_release);
      }
      return true;
    }
    return renderSynthFrames(samples, frames);
  }
  static void callback(ma_device* device, void* output, const void*, ma_uint32 frames) {
    auto* self = static_cast<Impl*>(device->pUserData); auto* samples = static_cast<float*>(output);
    if (self == nullptr || !self->renderFrames(samples, frames))
      std::fill_n(samples, static_cast<std::size_t>(frames) * kChannels, 0.0F);
  }
};
FluidSynthBackend::FluidSynthBackend() : impl_(std::make_unique<Impl>()) {}
FluidSynthBackend::~FluidSynthBackend() { shutdown(); }
AudioBackendInfo FluidSynthBackend::info() const noexcept { return {"fluidsynth-miniaudio", "FluidSynth + miniaudio", AudioCapability::MidiSynthesis | AudioCapability::DeviceOutput | AudioCapability::Mixing}; }
bool FluidSynthBackend::initialize(const AudioBackendConfig& config, AudioBackendStatus& status) {
  shutdown();
  if (config.sampleRate == 0U) { fail(status, AudioBackendError::InvalidConfiguration, "Sample rate must be greater than zero."); return false; }
  if (!isNormalizedVolume(config.volume) || !areNormalizedMidiChannelGains(config.channelGains)) { fail(status, AudioBackendError::InvalidConfiguration, "Volume and every MIDI channel gain must be finite and between 0.0 and 1.0."); return false; }
  if (!std::filesystem::is_regular_file(config.soundFontPath)) { fail(status, AudioBackendError::SoundFontNotFound, "SoundFont was not found: " + config.soundFontPath.string()); return false; }
  impl_->settings = new_fluid_settings();
  if (impl_->settings == nullptr) { fail(status, AudioBackendError::InvalidConfiguration, "FluidSynth could not allocate settings."); return false; }
  fluid_settings_setnum(impl_->settings, "synth.sample-rate", static_cast<double>(config.sampleRate));
  impl_->synth = new_fluid_synth(impl_->settings);
  if (impl_->synth == nullptr) { shutdown(); fail(status, AudioBackendError::InvalidConfiguration, "FluidSynth could not create a synthesizer."); return false; }
  impl_->channelGains = config.channelGains; impl_->runtimeMixer.reset(); impl_->volume = config.volume; impl_->sampleRate = config.sampleRate; impl_->muted = config.muted;
  fluid_synth_set_gain(impl_->synth, config.muted ? 0.0 : static_cast<double>(config.volume));
  if (fluid_synth_sfload(impl_->synth, config.soundFontPath.string().c_str(), 1) == FLUID_FAILED) { shutdown(); fail(status, AudioBackendError::SoundFontLoadFailed, "FluidSynth could not load SoundFont: " + config.soundFontPath.string()); return false; }
  impl_->midiSink = std::make_unique<FluidSynthMidiCommandSink>(impl_->synth, impl_->channelGains, &impl_->runtimeMixer);
  impl_->midiSink->resetChannelVolumes();
  if (config.enableDeviceOutput) {
    const auto contextResult = ma_context_init(nullptr, 0, nullptr, &impl_->context);
    if (contextResult != MA_SUCCESS) { shutdown(); fail(status, AudioBackendError::AudioDeviceUnavailable, "Audio output context could not start: " + std::string(ma_result_description(contextResult))); return false; }
    impl_->contextInitialized = true;
    ma_device_id selectedId{}; ma_device_id* selectedIdPtr = nullptr;
    if (config.outputDeviceIndex.has_value()) {
      ma_device_info* devices{}; ma_uint32 count{};
      const auto enumeration = ma_context_get_devices(&impl_->context, &devices, &count, nullptr, nullptr);
      if (enumeration != MA_SUCCESS) { shutdown(); fail(status, AudioBackendError::AudioDeviceUnavailable, "Audio devices could not be enumerated: " + std::string(ma_result_description(enumeration))); return false; }
      if (*config.outputDeviceIndex >= count) { shutdown(); fail(status, AudioBackendError::AudioDeviceNotFound, "Requested playback device index is not available: " + std::to_string(*config.outputDeviceIndex)); return false; }
      selectedId = devices[*config.outputDeviceIndex].id; selectedIdPtr = &selectedId;
    }
    auto dc = ma_device_config_init(ma_device_type_playback); dc.playback.format = ma_format_f32; dc.playback.channels = 2; dc.playback.pDeviceID = selectedIdPtr; dc.sampleRate = config.sampleRate; dc.dataCallback = &Impl::callback; dc.pUserData = impl_.get();
    const auto result = ma_device_init(&impl_->context, &dc, &impl_->device);
    if (result != MA_SUCCESS) { shutdown(); fail(status, AudioBackendError::AudioDeviceUnavailable, "No usable audio output device is available: " + std::string(ma_result_description(result))); return false; }
    impl_->deviceInitialized = true;
    const auto started = ma_device_start(&impl_->device);
    if (started != MA_SUCCESS) { shutdown(); fail(status, AudioBackendError::AudioDeviceUnavailable, "Audio output device could not start: " + std::string(ma_result_description(started))); return false; }
  }
  impl_->initialized = true; ok(status); return true;
}
bool FluidSynthBackend::playMidiFile(const std::filesystem::path& midi, AudioBackendStatus& status) {
  if (!impl_->initialized || impl_->synth == nullptr) { fail(status, AudioBackendError::InvalidConfiguration, "Initialize the audio backend before MIDI playback."); return false; }
  if (impl_->sessionActive.load(std::memory_order_acquire)) { fail(status, AudioBackendError::MidiPlaybackFailed, "Compiled timeline playback is active."); return false; }
  if (impl_->channelGains != defaultMidiChannelGains() || !impl_->runtimeMixer.isDefault()) { fail(status, AudioBackendError::MidiPlaybackFailed, "MIDI channel mixing requires compiled timeline playback."); return false; }
  if (!std::filesystem::is_regular_file(midi)) { fail(status, AudioBackendError::MidiFileNotFound, "MIDI file was not found: " + midi.string()); return false; }
  if (impl_->player != nullptr) { delete_fluid_player(impl_->player); impl_->player = nullptr; }
  impl_->player = new_fluid_player(impl_->synth);
  if (impl_->player == nullptr || fluid_player_add(impl_->player, midi.string().c_str()) != FLUID_OK || fluid_player_play(impl_->player) != FLUID_OK) {
    if (impl_->player != nullptr) { delete_fluid_player(impl_->player); impl_->player = nullptr; }
    fail(status, AudioBackendError::MidiPlaybackFailed, "FluidSynth could not start MIDI playback: " + midi.string()); return false;
  }
  ok(status); return true;
}
bool FluidSynthBackend::playCompiledTimeline(const SmfTimeline& timeline, AudioBackendStatus& status) {
  if (!impl_->initialized || impl_->synth == nullptr) { fail(status, AudioBackendError::InvalidConfiguration, "Initialize the audio backend before compiled timeline playback."); return false; }
  if (impl_->player != nullptr) { fail(status, AudioBackendError::MidiPlaybackFailed, "FluidSynth file-player playback is active or has not been released."); return false; }
  if (impl_->deviceInitialized && ma_device_stop(&impl_->device) != MA_SUCCESS) {
    fail(status, AudioBackendError::AudioDeviceUnavailable, "Audio output device could not stop for timeline preparation."); return false;
  }
  impl_->discardSession();
  impl_->midiSink->resetChannelVolumes();
  const auto preparation = impl_->session.prepare(timeline, impl_->sampleRate);
  if (!preparation.succeeded() || !impl_->session.play().succeeded()) {
    impl_->discardSession();
    fail(status, AudioBackendError::InvalidConfiguration, "Compiled timeline could not be prepared for playback."); return false;
  }
  impl_->sessionActive.store(true, std::memory_order_release);
  if (impl_->deviceInitialized && ma_device_start(&impl_->device) != MA_SUCCESS) {
    impl_->discardSession();
    fail(status, AudioBackendError::AudioDeviceUnavailable, "Audio output device could not resume after timeline preparation."); return false;
  }
  ok(status); return true;
}
bool FluidSynthBackend::renderStereo(std::span<float> pcm, AudioBackendStatus& status) {
  if (!impl_->initialized || impl_->synth == nullptr) { fail(status, AudioBackendError::InvalidConfiguration, "Initialize the audio backend before rendering PCM."); return false; }
  if (impl_->deviceInitialized) { fail(status, AudioBackendError::InvalidConfiguration, "Headless PCM rendering requires device output to be disabled."); return false; }
  if (pcm.empty() || pcm.size() % kChannels != 0U) { fail(status, AudioBackendError::InvalidConfiguration, "PCM output must contain complete stereo frames."); return false; }
  if (!impl_->renderFrames(pcm.data(), pcm.size() / kChannels)) { std::fill(pcm.begin(), pcm.end(), 0.0F); fail(status, AudioBackendError::RenderFailed, "FluidSynth could not render PCM."); return false; }
  ok(status); return true;
}
bool FluidSynthBackend::setVolume(float value, AudioBackendStatus& status) {
  if (!impl_->initialized || impl_->synth == nullptr) { fail(status, AudioBackendError::InvalidConfiguration, "Initialize the audio backend before changing volume."); return false; }
  if (!isNormalizedVolume(value)) { fail(status, AudioBackendError::InvalidConfiguration, "Volume must be finite and between 0.0 and 1.0."); return false; }
  impl_->volume = value; fluid_synth_set_gain(impl_->synth, impl_->muted ? 0.0 : static_cast<double>(value)); ok(status); return true;
}
bool FluidSynthBackend::setMuted(bool value, AudioBackendStatus& status) {
  if (!impl_->initialized || impl_->synth == nullptr) { fail(status, AudioBackendError::InvalidConfiguration, "Initialize the audio backend before changing mute state."); return false; }
  impl_->muted = value; fluid_synth_set_gain(impl_->synth, value ? 0.0 : static_cast<double>(impl_->volume)); ok(status); return true;
}
bool FluidSynthBackend::setRuntimeChannelGain(std::size_t channel, float gain, AudioBackendStatus& status) {
  if (!impl_->initialized || impl_->synth == nullptr) { fail(status, AudioBackendError::InvalidConfiguration, "Initialize the audio backend before changing MIDI channel mixing."); return false; }
  if (impl_->player != nullptr) { fail(status, AudioBackendError::MidiPlaybackFailed, "Runtime MIDI channel mixing requires compiled timeline playback."); return false; }
  if (!impl_->runtimeMixer.setGain(channel, gain)) { fail(status, AudioBackendError::InvalidConfiguration, "MIDI channel gain requires a channel from 0 to 15 and a finite value from 0.0 to 1.0."); return false; }
  ok(status); return true;
}
bool FluidSynthBackend::setRuntimeChannelMuted(std::size_t channel, bool muted, AudioBackendStatus& status) {
  if (!impl_->initialized || impl_->synth == nullptr) { fail(status, AudioBackendError::InvalidConfiguration, "Initialize the audio backend before changing MIDI channel mixing."); return false; }
  if (impl_->player != nullptr) { fail(status, AudioBackendError::MidiPlaybackFailed, "Runtime MIDI channel mixing requires compiled timeline playback."); return false; }
  if (!impl_->runtimeMixer.setMuted(channel, muted)) { fail(status, AudioBackendError::InvalidConfiguration, "MIDI channel index must be from 0 to 15."); return false; }
  ok(status); return true;
}
bool FluidSynthBackend::setRuntimeChannelSoloed(std::size_t channel, bool soloed, AudioBackendStatus& status) {
  if (!impl_->initialized || impl_->synth == nullptr) { fail(status, AudioBackendError::InvalidConfiguration, "Initialize the audio backend before changing MIDI channel mixing."); return false; }
  if (impl_->player != nullptr) { fail(status, AudioBackendError::MidiPlaybackFailed, "Runtime MIDI channel mixing requires compiled timeline playback."); return false; }
  if (!impl_->runtimeMixer.setSoloed(channel, soloed)) { fail(status, AudioBackendError::InvalidConfiguration, "MIDI channel index must be from 0 to 15."); return false; }
  ok(status); return true;
}
bool FluidSynthBackend::resetRuntimeMixer(AudioBackendStatus& status) {
  if (!impl_->initialized || impl_->synth == nullptr) { fail(status, AudioBackendError::InvalidConfiguration, "Initialize the audio backend before changing MIDI channel mixing."); return false; }
  impl_->runtimeMixer.reset();
  ok(status); return true;
}
float FluidSynthBackend::volume() const noexcept { return impl_->volume; }
bool FluidSynthBackend::isMuted() const noexcept { return impl_->muted; }
bool FluidSynthBackend::isPlaying() const noexcept { return impl_->sessionActive.load(std::memory_order_acquire) || (impl_->player != nullptr && fluid_player_get_status(impl_->player) == FLUID_PLAYER_PLAYING); }
bool FluidSynthBackend::hasActiveDevice() const noexcept { return impl_->deviceInitialized; }
void FluidSynthBackend::shutdown() noexcept {
  if (impl_ == nullptr) return;
  if (impl_->deviceInitialized) { ma_device_uninit(&impl_->device); impl_->deviceInitialized = false; }
  impl_->discardSession();
  if (impl_->player != nullptr) { delete_fluid_player(impl_->player); impl_->player = nullptr; }
  if (impl_->contextInitialized) { ma_context_uninit(&impl_->context); impl_->contextInitialized = false; }
  impl_->midiSink.reset();
  if (impl_->synth != nullptr) { delete_fluid_synth(impl_->synth); impl_->synth = nullptr; }
  if (impl_->settings != nullptr) { delete_fluid_settings(impl_->settings); impl_->settings = nullptr; }
  impl_->sampleRate = 0U; impl_->initialized = false;
}
} // namespace OpenHDK
