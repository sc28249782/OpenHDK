// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 OpenHDK contributors
#include "audio/AudioDeviceDiagnostics.hpp"
#include "audio/FluidSynthBackend.hpp"
#include <chrono>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <vector>
namespace {
void usage(){std::cout<<"Usage: OpenHDK --midi <file.mid> --soundfont <file.sf2> [--device <index>] [--volume <0-100>] [--mute] [--no-device]\n       OpenHDK --list-devices\n";}
bool indexOf(const std::string&s,std::uint32_t&n){try{std::size_t p{};auto v=std::stoul(s,&p);if(p!=s.size()||v>UINT32_MAX)return false;n=static_cast<std::uint32_t>(v);return true;}catch(...){return false;}}
bool volumeOf(const std::string&s,float&v){try{std::size_t p{};auto x=std::stof(s,&p);if(p!=s.size()||x<0.0F||x>100.0F)return false;v=x/100.0F;return true;}catch(...){return false;}}
}
int main(int argc,char*argv[]){
 std::string midi,sf2;bool output=true,list=false,mute=false;std::optional<std::uint32_t> device;float volume=1.0F;
 for(int i=1;i<argc;++i){const std::string a=argv[i];if(a=="--help"||a=="-h"){usage();return 0;}if(a=="--list-devices"){list=true;continue;}if(a=="--no-device"){output=false;continue;}if(a=="--mute"){mute=true;continue;}if(a=="--device"&&i+1<argc){std::uint32_t v{};if(!indexOf(argv[++i],v)){std::cerr<<"Device index must be a non-negative integer.\n";return 64;}device=v;continue;}if(a=="--volume"&&i+1<argc){if(!volumeOf(argv[++i],volume)){std::cerr<<"Volume must be a number from 0 to 100.\n";return 64;}continue;}if((a=="--midi"||a=="--soundfont")&&i+1<argc){(a=="--midi"?midi:sf2)=argv[++i];continue;}std::cerr<<"Unknown or incomplete argument: "<<a<<"\n";usage();return 64;}
 OpenHDK::AudioBackendStatus status;if(list){const auto ds=OpenHDK::enumerateAudioOutputDevices(status);if(!status){std::cerr<<status.message<<"\n";return 1;}std::cout<<"Playback devices ("<<ds.size()<<"):\n";for(std::size_t i=0;i<ds.size();++i)std::cout<<(ds[i].isDefault?"* ":"  ")<<"["<<i<<"] "<<ds[i].name<<"\n";return 0;}if(!output&&device){std::cerr<<"--device cannot be used with --no-device.\n";return 64;}if(midi.empty()||sf2.empty()){usage();return 64;}
 OpenHDK::FluidSynthBackend backend;if(!backend.initialize({.soundFontPath=sf2,.enableDeviceOutput=output,.outputDeviceIndex=device,.volume=volume,.muted=mute},status)){std::cerr<<"Audio initialization failed: "<<status.message<<"\n";return 1;}if(!backend.playMidiFile(midi,status)){std::cerr<<"MIDI playback failed: "<<status.message<<"\n";return 1;}if(!backend.hasActiveDevice()){std::vector<float>pcm(88200);if(!backend.renderStereo(pcm,status)){std::cerr<<"Headless PCM render failed: "<<status.message<<"\n";return 1;}return 0;}while(backend.isPlaying())std::this_thread::sleep_for(std::chrono::milliseconds(50));
}
