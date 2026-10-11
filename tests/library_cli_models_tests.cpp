// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/LibraryCliOutput.hpp"
#include "tests/TestCheck.hpp"
#include <cstdlib>
#include <iostream>
#include <new>

namespace { long remaining=-1; }
void* operator new(std::size_t n) {
  if(remaining==0) throw std::bad_alloc();
  if(remaining>0) --remaining;
  if(void* p=std::malloc(n?n:1U)) return p;
  throw std::bad_alloc();
}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
void* operator new[](std::size_t n) {return ::operator new(n);}
void operator delete[](void* p) noexcept {::operator delete(p);}
void operator delete[](void* p,std::size_t) noexcept {::operator delete(p);}

int main() {
  using namespace OpenHDK;
  const auto bad=[](std::string_view s){auto r=parseLibraryCommand(s);return !r.command && r.error;};
  OPENHDK_FAIL_IF(1,!parseLibraryCommand("help").command || !parseLibraryCommand("quit").command || !parseLibraryCommand("roots").command);
  for(auto s:{"","unknown","help junk","\"help\"","scan","scan 1 extra","scan -1","scan +1","scan 0","remove 18446744073709551615","prepare 18446744073709551616","scan 1x"}) OPENHDK_FAIL_IF(2,!bad(s));
  OPENHDK_FAIL_IF(3,libraryCliSelector("18446744073709551614")!=UINT64_MAX-1U || libraryCliSelector("0001")!=1U);
  OPENHDK_FAIL_IF(4,!parseLibraryCommand("songs 1 0 100").command || !bad("songs 1 0 101") || !bad("songs 1 0 0"));
  auto reg=parseLibraryCommand("register \"/songs/เพลง set\" tis620 0");
  OPENHDK_FAIL_IF(5,!reg.command || reg.command->path!="/songs/เพลง set" || reg.command->policy.lyrics.encoding!=LyricTextEncoding::Tis620 || reg.command->policy.lyrics.trackIndex!=0U);
  OPENHDK_FAIL_IF(6,!parseLibraryCommand("register /songs").command || !parseLibraryCommand("register /songs utf8 auto").command);
  for(auto s:{"register /songs utf8","register /songs latin1 auto","register /songs utf8 -1","register \"\"","reattach 1 \"\""}) OPENHDK_FAIL_IF(7,!bad(s));
  auto maximumTrack=parseLibraryCommand("register /songs utf8 "+std::to_string(SIZE_MAX));
  OPENHDK_FAIL_IF(8,!maximumTrack.command || maximumTrack.command->policy.lyrics.trackIndex!=SIZE_MAX);
  OPENHDK_FAIL_IF(9,!parseLibraryCommand("relocate 1 2 dir/song.kar").command || !bad("relocate 1 2 ../escape.kar"));
  auto clear=parseLibraryCommand("overrides 1 - -");
  OPENHDK_FAIL_IF(10,!clear.command || clear.command->title || clear.command->artist);
  auto literal=parseLibraryCommand("overrides 1 \"-\" \" เพลง e\xcc\x81 \"");
  OPENHDK_FAIL_IF(11,!literal.command || literal.command->title!="-" || literal.command->artist!=" เพลง e\xcc\x81 ");
  OPENHDK_FAIL_IF(12,!bad("overrides 1 \"\" -") || !bad("overrides 1 title"));
  auto quotes=tokenizeLibraryCommand("overrides 1 \"a\\\"b\\\\c\" -");
  OPENHDK_FAIL_IF(13,quotes.error || quotes.tokens[2].bytes!="a\"b\\c");
  for(auto s:{"register \"/bad\\n\"","register \"unterminated","register \"x\"junk","register a\"b","register a\\b"}) OPENHDK_FAIL_IF(14,!bad(s));
  OPENHDK_FAIL_IF(15,!bad(std::string("scan 1\0",7)) || !bad("scan 1\n"));
  // Text limits count bytes; quoted payload excludes delimiters/escape syntax.
  OPENHDK_FAIL_IF(16,!parseLibraryCommand("overrides 1 \""+std::string(4096,'x')+"\" -").command);
  OPENHDK_FAIL_IF(17,!bad("overrides 1 \""+std::string(4097,'x')+"\" -"));
  OPENHDK_FAIL_IF(18,!bad("overrides 1 \""+std::string("\xc0\xaf",2)+"\" -"));
  OPENHDK_FAIL_IF(19,!tokenizeLibraryCommand(std::string(16384,' ')).tokens.empty() || !tokenizeLibraryCommand(std::string(16385,' ')).error);
  OPENHDK_FAIL_IF(20,tokenizeLibraryCommand("1 2 3 4 5 6 7 8").tokens.size()!=8 || !tokenizeLibraryCommand("1 2 3 4 5 6 7 8 9").error);
  LibraryCliLineReader reader;
  for(int i=0;i<16385;++i) OPENHDK_FAIL_IF(21,reader.push('x')!=LibraryCliLineReader::Status::More);
  OPENHDK_FAIL_IF(22,!reader.overflow() || !reader.line().empty() || reader.end()!=LibraryCliLineReader::Status::Rejected);
  OPENHDK_FAIL_IF(23,reader.push('\n')!=LibraryCliLineReader::Status::Rejected);
  reader.reset();for(char c:std::string("roots\r\n")) reader.push(c);
  OPENHDK_FAIL_IF(24,reader.line()!="roots");reader.reset();
  for(char c:std::string("register \"EOF")) reader.push(c);
  OPENHDK_FAIL_IF(25,reader.end()!=LibraryCliLineReader::Status::Line || !bad(reader.line()));
  reader.reset();OPENHDK_FAIL_IF(26,reader.end()!=LibraryCliLineReader::Status::More);
  const std::array<std::string_view,5> entry{"--store-dir","/store","--store-name","catalog","session"};
  OPENHDK_FAIL_IF(27,!parseLibraryEntry(entry).entry || parseLibraryEntry(entry).entry->mode!=LibraryCliMode::Session);
  const std::array<std::string_view,1> help{"--help"};OPENHDK_FAIL_IF(28,!parseLibraryEntry(help).entry);
  for(auto mode:{"create","session"}) {auto a=entry;a[4]=mode;OPENHDK_FAIL_IF(29,!parseLibraryEntry(a).entry);}
  for(auto name:{"../catalog","dir/catalog","a.ohk-lock",".ohk-stage-1",""}) {auto a=entry;a[3]=name;OPENHDK_FAIL_IF(30,!parseLibraryEntry(a).error);}
  {auto a=entry;a[0]="--store-name";OPENHDK_FAIL_IF(31,!parseLibraryEntry(a).error);a=entry;a[1]="relative";OPENHDK_FAIL_IF(32,!parseLibraryEntry(a).error);}
  OPENHDK_FAIL_IF(33,!libraryCliUtf8(" \xef\xbb\xbfเพลง e\xcc\x81 "));
  for(auto s:{std::string("\xc0\xaf",2),std::string("\xed\xa0\x80",3),std::string("\xf4\x90\x80\x80",4),std::string("\xe0\xa0",2)}) OPENHDK_FAIL_IF(34,libraryCliUtf8(s));
  LibraryCliJsonRecord json;
  OPENHDK_FAIL_IF(35,!json.text("title","\"ไทย\"\n\t\\") || !json.number("revision",UINT64_MAX) || !json.boolean("saved",true) || !json.null("artist"));
  const auto rendered=json.finish();
  OPENHDK_FAIL_IF(36,!rendered.bytes || *rendered.bytes!="{\"title\":\"\\\"ไทย\\\"\\u000a\\u0009\\\\\",\"revision\":18446744073709551615,\"saved\":true,\"artist\":null}\n");
  for(std::size_t i=0;i+1<rendered.bytes->size();++i) OPENHDK_FAIL_IF(37,static_cast<unsigned char>((*rendered.bytes)[i])<32U);
  LibraryCliJsonRecord bytes;OPENHDK_FAIL_IF(38,!bytes.nativeText("native",std::string("\xff\0",2)) || *bytes.finish().bytes!="{\"native\":{\"encoding\":\"bytes-hex\",\"value\":\"ff00\"}}\n");
  LibraryCliJsonRecord controls;std::string all;for(int c=0;c<32;++c) all+=static_cast<char>(c);all+=static_cast<char>(127);
  OPENHDK_FAIL_IF(39,!controls.text("control",all) || controls.finish().bytes->find("\\u0000")==std::string::npos || controls.finish().bytes->find("\\u007f")==std::string::npos);
  LibraryCliJsonRecord duplicate;duplicate.null("x");OPENHDK_FAIL_IF(40,duplicate.null("x") || duplicate.finish().bytes || !duplicate.finish().error);
  LibraryCliJsonRecord badkey;OPENHDK_FAIL_IF(41,badkey.null("injected\"key") || badkey.finish().bytes);
  LibraryCliJsonRecord badtext;OPENHDK_FAIL_IF(42,badtext.text("x",std::string("\xff",1)) || badtext.finish().bytes);
  LibraryCliJsonRecord exact(11);exact.text("x","y");OPENHDK_FAIL_IF(43,!exact.finish().bytes || exact.finish().bytes->size()!=10);
  LibraryCliJsonRecord tooSmall(9);OPENHDK_FAIL_IF(44,tooSmall.text("x","y") || tooSmall.finish().bytes);
  LibraryCliJsonRecord empty;OPENHDK_FAIL_IF(45,*empty.finish().bytes!="{}\n");
  LibraryCliOutputBatch batch(20);OPENHDK_FAIL_IF(46,!batch.add(exact) || !batch.add(exact) || batch.finish().bytes->size()!=20);
  OPENHDK_FAIL_IF(47,batch.add(empty) || batch.finish().bytes || !batch.finish().error);
  LibraryCliOutputBatch badbatch(0);OPENHDK_FAIL_IF(48,badbatch.add(empty) || badbatch.finish().bytes);
  for(auto outcome:{LibraryCliCommitOutcome::Saved,LibraryCliCommitOutcome::Uncertain,LibraryCliCommitOutcome::NoChange}) {
    LibraryCliDelivery d{outcome,true};OPENHDK_FAIL_IF(49,d.exitCode()!=74 || d.commit!=outcome || d.automaticRetry());
  }
  // Allocation sweeps require all-or-error results; no partially parsed command
  // or record can escape. Text is allocated before starting injection.
  const std::string command="overrides 1 \""+std::string(200,'x')+"\" \""+std::string(200,'y')+"\"";
  bool parseSuccess=false;std::size_t parseFailures=0;
  for(long n=0;n<100;++n) {remaining=n;auto result=parseLibraryCommand(command);remaining=-1;
    if(result.command){parseSuccess=true;break;}
    OPENHDK_FAIL_IF(50,!result.error || result.error->code!=LibraryCliErrorCode::StorageFailure);++parseFailures;}
  OPENHDK_FAIL_IF(51,!parseSuccess || parseFailures==0);
  bool outputSuccess=false;std::size_t outputFailures=0;const std::string payload(200,'x');
  for(long n=0;n<100;++n) {LibraryCliJsonRecord record;remaining=n;record.text("title",payload);auto result=record.finish();remaining=-1;
    if(result.bytes){outputSuccess=true;break;}
    OPENHDK_FAIL_IF(52,!result.error || result.error->code!=LibraryCliErrorCode::StorageFailure);++outputFailures;}
  OPENHDK_FAIL_IF(53,!outputSuccess || outputFailures==0);
  bool entrySuccess=false;std::size_t entryFailures=0;
  const std::string longParent="/"+std::string(200,'x');auto injectedEntry=entry;injectedEntry[1]=longParent;
  for(long n=0;n<100;++n) {remaining=n;auto result=parseLibraryEntry(injectedEntry);remaining=-1;
    if(result.entry){entrySuccess=true;break;}
    OPENHDK_FAIL_IF(54,!result.error || result.error->code!=LibraryCliErrorCode::StorageFailure);++entryFailures;}
  OPENHDK_FAIL_IF(55,!entrySuccess || entryFailures==0);
  LibraryCliLineReader injectedReader;remaining=0;
  for(int i=0;i<200;++i) { injectedReader.push('x'); }
  remaining=-1;
  OPENHDK_FAIL_IF(56,!injectedReader.error() || injectedReader.error()->code!=LibraryCliErrorCode::StorageFailure
      || !injectedReader.line().empty() || injectedReader.push('\n')!=LibraryCliLineReader::Status::Rejected);
  injectedReader.reset();OPENHDK_FAIL_IF(57,injectedReader.error());
  LibraryCliJsonRecord many;for(unsigned i=0;i<64;++i) OPENHDK_FAIL_IF(58,!many.null("f"+std::to_string(i)));
  OPENHDK_FAIL_IF(59,many.null("more") || many.finish().bytes);
  LibraryCliJsonRecord payloadRecord;payloadRecord.text("title",payload);LibraryCliOutputBatch injectedBatch;
  remaining=0;const bool added=injectedBatch.add(payloadRecord);remaining=-1;
  OPENHDK_FAIL_IF(60,added || injectedBatch.finish().bytes || injectedBatch.finish().error->code!=LibraryCliErrorCode::StorageFailure);
  const std::string malformed="overrides 1 \"a\\\""+std::string("\xff",1)+"\" -";
  auto indexed=parseLibraryCommand(malformed);
  OPENHDK_FAIL_IF(61,indexed.command || !indexed.error || indexed.error->offset!=malformed.find(static_cast<char>(0xff)));
  LibraryCliJsonRecord c1;
  OPENHDK_FAIL_IF(62,!c1.text("control",std::string("\xc2\x9b",2)) || *c1.finish().bytes!="{\"control\":\"\\u009b\"}\n");
  auto whitespace=parseLibraryCommand("overrides 1 \"a\tb\rc\" -");
  OPENHDK_FAIL_IF(63,!whitespace.command || whitespace.command->title!="a\tb\rc");
  // A complete JSON record emitted for an independent standard-library decoder.
  std::cout<<*rendered.bytes;
  return 0;
}
