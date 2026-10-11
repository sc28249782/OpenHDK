// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/LibraryCliSession.hpp"
#include <iostream>
#ifndef _WIN32
#include <csignal>
#endif
int main(int argc,char** argv) {
  using namespace OpenHDK;
#ifndef _WIN32
  // Broken pipes become explicit delivery failures instead of terminating before
  // recording the known commit outcome in the session controller.
  std::signal(SIGPIPE,SIG_IGN);
#endif
  try {
    std::vector<std::string_view> args;
    for(int i=1;i<argc;++i) args.emplace_back(argv[i]);
    auto entry=parseLibraryEntry(args);
    if(entry.error) {std::cerr<<"Invalid OpenHDKLibrary entry; use --help.\n";return 64;}
    if(entry.entry->mode==LibraryCliMode::Help) {
      std::cout<<"OpenHDKLibrary --store-dir ABSOLUTE_DIR --store-name PRIMARY create|session\n"
        <<"Commands: help quit roots songs register reattach scan relocate remove overrides prepare\n"
        <<"Experimental Linux/ext4 management; playback remains pending.\n";
      return std::cout?0:74;
    }
    const auto caller=LibraryCliSession::kCallerBytes+entry.entry->parent.size()+entry.entry->primary.size();
    auto opened=entry.entry->mode==LibraryCliMode::Create
      ?DurableLibraryService::createLinux(std::make_unique<SongDiscovery>(),entry.entry->parent,entry.entry->primary,{}, {},caller)
      :DurableLibraryService::openLinux(entry.entry->parent,entry.entry->primary,{}, {},caller);
    if(opened.error) {
      auto output=LibraryCliSession::factoryError(*opened.error);
      if(!output.bytes) return 74;
      std::cout<<*output.bytes<<std::flush;
      return std::cout?LibraryCliSession::factoryExit(*opened.error):74;
    }
    LibraryCliJsonRecord row;row.text("type","session");
    row.text("mode",entry.entry->mode==LibraryCliMode::Create?"create":"session");
    if(opened.receipt) {
      row.text("status","Saved");row.number("revision",opened.receipt->capturedRevision);
      row.number("sequence",opened.receipt->token.sequence);
      if(opened.receipt->cleanupWarning) row.number("cleanup_warning",static_cast<std::uint64_t>(opened.receipt->cleanupWarning->code));
    } else row.text("status","Opened");
    auto greeting=row.finish();
    if(!greeting.bytes) return 74;
    std::cout<<*greeting.bytes<<std::flush;if(!std::cout) return 74;
    LibraryCliSession session(std::move(opened.service));return session.run(std::cin,std::cout);
  } catch(const std::bad_alloc&) {return 74;}
    catch(const std::filesystem::filesystem_error&) {return 1;}
}
