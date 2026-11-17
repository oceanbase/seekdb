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
  bool admission_scope = false;
  int64_t count = 0;
  int64_t dropped = 0;
  Record records[8];
  Record first_space;
  bool first_space_present = false;
  bool bind_nonabort_space = false;
  uint64_t space_call_id = 0;
  Record recovery;
  bool recovery_present = false;
  uint64_t recovery_after_observed = 0;
  uint64_t sequence[8];
  uint64_t total = 0;
  uint64_t kind_total[4] = {};
  int64_t first[4];
  int64_t last[4];
  void begin(bool bind_space = false) {
    count = 0; dropped = 0; total = 0; active = abort_scope;
    first_space_present = false; space_call_id = 0; bind_nonabort_space = bind_space;
    recovery_present = false; recovery_after_observed = 0;
    for (int64_t k = 0; k < 4; ++k) {
      kind_total[k] = 0; first[k] = -1; last[k] = -1;
    }
  }
  void first_space_comparison(uint64_t size, uint64_t lsn, uint64_t committed,
                              uint64_t reuse, uint64_t capacity, uint64_t base,
                              uint64_t upper, bool passed) {
    if ((active || bind_nonabort_space) && admission_scope && !first_space_present) {
      first_space = {2, {size,lsn,committed,reuse,capacity,base,upper,passed ? 1UL : 0UL}};
      first_space_present = true;
    }
  }
  void recover_space(uint64_t size, uint64_t end, uint64_t committed,
                     uint64_t reuse, uint64_t capacity, uint64_t base, uint64_t upper) {
    if (active && admission_scope && kind_total[1] > 0 && !recovery_present) {
      recovery = {2, {size,end,committed,reuse,capacity,base,upper,0}};
      recovery_after_observed = total;
      recovery_present = true;
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
class Seek1402AdmissionScope {
public:
  Seek1402AdmissionScope() : capture_(seek1402_capture()), previous_(capture_.admission_scope) {
    capture_.admission_scope = true;
  }
  ~Seek1402AdmissionScope() { capture_.admission_scope = previous_; }
  Seek1402AdmissionScope(const Seek1402AdmissionScope &) = delete;
  Seek1402AdmissionScope &operator=(const Seek1402AdmissionScope &) = delete;
private:
  Seek1402Capture &capture_;
  bool previous_;
};
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
