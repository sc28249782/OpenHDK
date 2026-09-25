// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 OpenHDK contributors
#include "audio/AudioDeviceDiagnostics.hpp"
#include "audio/FluidSynthBackend.hpp"
#include "app/MidiDiagnosticsCli.hpp"
#include "audio/SmfParser.hpp"
#include "audio/SmfTimelineCompiler.hpp"
#include <array>
#include <chrono>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <vector>
namespace {
void usage(){std::cout<<"Usage: OpenHDK --midi <file.mid> --soundfont <file.sf2> [--device <index>] [--volume <0-100>] [--channel-volume <1-16>:<0-100>] [--mute] [--no-device]\n       OpenHDK --midi-diagnostics <file.mid>\n       OpenHDK --list-devices\n";}
bool indexOf(const std::string&s,std::uint32_t&n){try{std::size_t p{};auto v=std::stoul(s,&p);if(p!=s.size()||v>UINT32_MAX)return false;n=static_cast<std::uint32_t>(v);return true;}catch(...){return false;}}
bool volumeOf(const std::string&s,float&v){try{std::size_t p{};auto x=std::stof(s,&p);if(p!=s.size())return false;const auto normalized=x/100.0F;if(!OpenHDK::isNormalizedVolume(normalized))return false;v=normalized;return true;}catch(...){return false;}}
bool channelVolumeOf(const std::string& s, OpenHDK::MidiChannelGains& gains) { const auto delimiter = s.find(':'); if (delimiter == std::string::npos || delimiter == 0U || delimiter == s.size() - 1U || s.find(':', delimiter + 1U) != std::string::npos) return false; try { std::size_t parsed{}; const auto channel = std::stoul(s.substr(0U, delimiter), &parsed); if (parsed != delimiter || channel == 0U || channel > OpenHDK::kMidiChannelCount) return false; float volume{}; if (!volumeOf(s.substr(delimiter + 1U), volume)) return false; gains[channel - 1U] = volume; return true; } catch (...) { return false; } }
}
int main(int argc,char*argv[]){
 std::string midi,sf2,diagnosticMidi;bool output=true,list=false,mute=false,playbackOption=false;std::optional<std::uint32_t> device;float volume=1.0F;OpenHDK::MidiChannelGains channelGains=OpenHDK::defaultMidiChannelGains();
 for(int i=1;i<argc;++i){const std::string a=argv[i];if(a=="--help"||a=="-h"){usage();return 0;}if(a=="--midi-diagnostics"&&i+1<argc){diagnosticMidi=argv[++i];continue;}if(a=="--list-devices"){list=true;continue;}if(a=="--no-device"){output=false;playbackOption=true;continue;}if(a=="--mute"){mute=true;playbackOption=true;continue;}if(a=="--device"&&i+1<argc){std::uint32_t v{};if(!indexOf(argv[++i],v)){std::cerr<<"Device index must be a non-negative integer.\n";return 64;}device=v;playbackOption=true;continue;}if(a=="--volume"&&i+1<argc){if(!volumeOf(argv[++i],volume)){std::cerr<<"Volume must be a finite number from 0 to 100.\n";return 64;}playbackOption=true;continue;}if(a=="--channel-volume"&&i+1<argc){if(!channelVolumeOf(argv[++i],channelGains)){std::cerr<<"Channel volume must be <1-16>:<0-100>.\n";return 64;}playbackOption=true;continue;}if((a=="--midi"||a=="--soundfont")&&i+1<argc){(a=="--midi"?midi:sf2)=argv[++i];playbackOption=true;continue;}std::cerr<<"Unknown or incomplete argument: "<<a<<"\n";usage();return 64;}
 if(!diagnosticMidi.empty()){if(list||playbackOption){std::cerr<<"--midi-diagnostics cannot be combined with playback or device options.\n";return 64;}return OpenHDK::runMidiDiagnostics(diagnosticMidi,std::cout,std::cerr);}
 OpenHDK::AudioBackendStatus status;if(list){const auto ds=OpenHDK::enumerateAudioOutputDevices(status);if(!status){std::cerr<<status.message<<"\n";return 1;}std::cout<<"Playback devices ("<<ds.size()<<"):\n";for(std::size_t i=0;i<ds.size();++i)std::cout<<(ds[i].isDefault?"* ":"  ")<<"["<<i<<"] "<<ds[i].name<<"\n";return 0;}if(!output&&device){std::cerr<<"--device cannot be used with --no-device.\n";return 64;}if(midi.empty()||sf2.empty()){usage();return 64;}
 std::vector<std::uint8_t>midiBytes;if(!OpenHDK::readMidiBytes(midi,midiBytes)){std::cerr<<"MIDI file could not be read: "<<midi<<"\n";return 1;}const auto parsedMidi=OpenHDK::SmfParser::parse(midiBytes);if(!parsedMidi.file()){const auto* error=parsedMidi.error();std::cerr<<"MIDI parse failed at byte "<<error->offset<<": "<<error->message()<<"\n";return 1;}const auto timeline=OpenHDK::SmfTimelineCompiler::compile(*parsedMidi.file());if(!timeline.timeline()){const auto* error=timeline.error();std::cerr<<"MIDI timeline compilation failed: "<<error->message()<<"\n";return 1;}
 OpenHDK::FluidSynthBackend backend;if(!backend.initialize({.soundFontPath=sf2,.enableDeviceOutput=output,.outputDeviceIndex=device,.volume=volume,.muted=mute,.channelGains=channelGains},status)){std::cerr<<"Audio initialization failed: "<<status.message<<"\n";return 1;}if(!backend.playCompiledTimeline(*timeline.timeline(),status)){std::cerr<<"MIDI playback failed: "<<status.message<<"\n";return 1;}if(!backend.hasActiveDevice()){std::array<float,2048>pcm{};while(backend.isPlaying()){if(!backend.renderStereo(pcm,status)){std::cerr<<"Headless PCM render failed: "<<status.message<<"\n";return 1;}}return 0;}while(backend.isPlaying())std::this_thread::sleep_for(std::chrono::milliseconds(50));
}
