#include "../src/range_window.hpp"
#include <cassert>
#include <cstdint>
#include <limits>
#include <set>
#include <string>

int main() {
  using namespace ptvrange;
  assert(kBlock==2ULL*1024*1024);
  assert(block_count(0)==0);
  assert(block_count(1)==1);
  assert(block_count(kBlock)==1);
  assert(block_count(kBlock+1)==2);
  assert(block_for(kBlock*5+17)==5);
  assert(keep(9,10));
  assert(keep(10,10));
  assert(keep(11,10)&&keep(12,10));
  assert(!keep(8,10)&&!keep(13,10));
  assert(choose(0,8,{})==0);
  assert(choose(0,8,{0})==1);
  assert(choose(0,8,{0,1})==2);
  assert(choose(0,8,{0,1,2})==kNone);
  assert(choose(0,8,{0},1)==2);
  assert(choose(1,8,{0},1)==1); // a demanded seek overrides suppressed prefetch
  assert(choose(7,8,{7})==kNone);
  assert(choose(5,8,{5})==6);
  assert(choose(9,8,{})==kNone);
  uint64_t first=0,last=0,total=0;
  assert(parse_content_range("bytes 0-65535/2429873904",first,last,total));
  assert(first==0&&last==65535&&total==2429873904ULL);
  assert(parse_content_range("bytes 2097152-4194303/9000000",first,last,total));
  assert(first==2097152&&last==4194303&&total==9000000);
  assert(!parse_content_range("bytes */100",first,last,total));
  assert(!parse_content_range("bytes 100-10/200",first,last,total));
  assert(!parse_content_range("bytes 0-200/200",first,last,total));
  assert(!parse_content_range("bytes 0-100/0",first,last,total));
  assert(!parse_content_range("bytes 0-100/999999999999999999999999",first,last,total));
  assert(!parse_content_range("Content-Range: bytes 0-100/500",first,last,total));
  assert(!parse_content_range("bytes 0-100/200 trailing",first,last,total));
  return 0;
}
