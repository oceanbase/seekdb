#pragma once
#include <cstdint>
namespace oceanbase { namespace palf {
struct Seek1402Capture {
  // kind 1: window(start,end,requested); 2: space(size,end,committed,reuse,capacity,base,upper,ret).
  // kind 3: admission(size,upper,start,id_upper,space_ok,window_checked,window_ok).
  // kind 4: allocator(size,new_id,id_upper,new_lsn,lsn_upper).
  struct Record { int64_t kind; uint64_t value[8]; };
  bool active = false;
  bool abort_scope = false;
  int64_t count = 0;
  int64_t dropped = 0;
  Record records[8];
  uint64_t sequence[8];
  uint64_t total = 0;
  uint64_t kind_total[4] = {};
  int64_t first[4];
  int64_t last[4];
  void begin() {
    count = 0; dropped = 0; total = 0; active = abort_scope;
    for (int64_t k = 0; k < 4; ++k) {
      kind_total[k] = 0; first[k] = -1; last[k] = -1;
    }
  }
  void add(int64_t kind, uint64_t a=0, uint64_t b=0, uint64_t c=0,
           uint64_t d=0, uint64_t e=0, uint64_t f=0, uint64_t g=0, uint64_t h=0) {
    if (active) {
      ++total;
      if (kind < 1 || kind > 4) { ++dropped; return; }
      const int64_t k = kind - 1;
      ++kind_total[k];
      int64_t slot;
      if (first[k] < 0) { slot = first[k] = count++; }
      else if (last[k] < 0) { slot = last[k] = count++; }
      else { slot = last[k]; ++dropped; }
      records[slot] = {kind, {a,b,c,d,e,f,g,h}};
      sequence[slot] = total - 1;
    }
  }
};
inline Seek1402Capture &seek1402_capture() {
  static thread_local Seek1402Capture capture;
  return capture;
}
class Seek1402AbortScope {
public:
  explicit Seek1402AbortScope(bool abort) : capture_(seek1402_capture()), previous_(capture_.abort_scope) {
    capture_.abort_scope = abort;
  }
  ~Seek1402AbortScope() { capture_.abort_scope = previous_; }
  Seek1402AbortScope(const Seek1402AbortScope &) = delete;
  Seek1402AbortScope &operator=(const Seek1402AbortScope &) = delete;
private:
  Seek1402Capture &capture_;
  bool previous_;
};
}} // namespace oceanbase::palf
