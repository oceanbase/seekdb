#pragma once
#include <cstdint>
namespace oceanbase { namespace palf {
struct Seek1402Capture {
  // kind 1: window(start,end,requested); 2: space(size,end,committed,reuse,capacity,base,upper,ret).
  // kind 3: admission(size,upper,start,id_upper,space_ok,window_checked,window_ok).
  // kind 4: allocator(size,new_id,id_upper,new_lsn,lsn_upper).
  struct Record { int64_t kind; uint64_t value[8]; };
  bool active = false;
  int64_t count = 0;
  int64_t dropped = 0;
  Record records[64];
  void begin() { count = 0; dropped = 0; active = true; }
  void add(int64_t kind, uint64_t a=0, uint64_t b=0, uint64_t c=0,
           uint64_t d=0, uint64_t e=0, uint64_t f=0, uint64_t g=0, uint64_t h=0) {
    if (active) {
      if (count < 64) { records[count++] = {kind, {a,b,c,d,e,f,g,h}}; }
      else { ++dropped; }
    }
  }
};
inline Seek1402Capture &seek1402_capture() {
  static thread_local Seek1402Capture capture;
  return capture;
}
}} // namespace oceanbase::palf
