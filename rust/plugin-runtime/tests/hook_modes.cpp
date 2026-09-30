// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
// C++ callbacks through the actual Rust host archive. Synthetic operation
// state checks composition/validation, not custom SQL plan construction.
#include "plugin_runtime.h"
#include <cstdlib>
#include <iostream>
#include <vector>
#define CHECK(value) do { if (!(value)) { std::cerr << __LINE__ << ": " #value << '\n'; std::abort(); } } while (0)
struct Operation {
  std::vector<int> events;
  int leaf_error = 0, validations = 0, after_result = 0;
  bool ready = false;
};
struct Callback { Operation *op; int id; int behavior = 0; };
static int32_t leaf(void *context) noexcept {
  auto &op = *static_cast<Operation *>(context);
  op.events.push_back(99); op.ready = op.leaf_error == 0;
  return op.leaf_error;
}
static int32_t validate(void *context) noexcept {
  auto &op = *static_cast<Operation *>(context);
  ++op.validations; op.events.push_back(100);
  return op.ready ? 0 : -700;
}
static void observe(void *context, uint32_t phase, int32_t result) noexcept {
  auto &cb = *static_cast<Callback *>(context);
  cb.op->events.push_back(cb.id + phase);
  if (phase == SEEKDB_RUNTIME_HOOK_AFTER) cb.op->after_result = result;
}
static int32_t invoke(void *context, seekdb_runtime_hook_next_fn next, void *frame) noexcept {
  auto &cb = *static_cast<Callback *>(context);
  cb.op->events.push_back(cb.id);
  if (cb.behavior == 1) { cb.op->ready = true; return 0; } // replacement
  if (cb.behavior == 2) return 0; // omitted result/next
  if (cb.behavior == 3) return -88; // veto
  const int32_t result = next(frame);
  if (cb.behavior == 4) static_cast<void>(next(frame));
  if (cb.behavior == 5) cb.op->ready = false; // post-next invalidation
  cb.op->events.push_back(cb.id + 1);
  return cb.behavior == 6 ? 0 : result; // attempted downstream error suppression
}
static seekdb_runtime_hook_v2_t entry(uint32_t mode, Callback &cb) {
  return {sizeof(seekdb_runtime_hook_v2_t), mode, &cb,
          mode == SEEKDB_RUNTIME_HOOK_OBSERVE ? nullptr : invoke,
          mode == SEEKDB_RUNTIME_HOOK_OBSERVE ? observe : nullptr, {0, 0, 0, 0}};
}
static void run(const std::vector<seekdb_runtime_hook_v2_t> &hooks, Operation &op,
                int expected_bridge, int expected_operation) {
  int32_t result = 123;
  CHECK(seekdb_runtime_hook_run_v2(hooks.data(), hooks.size(), leaf, &op,
      validate, -22, &result) == expected_bridge);
  CHECK(result == expected_operation);
}
int main() {
  {
    Operation op;
    Callback obs{&op, 10}, around{&op, 20}, replacement{&op, 30, 1}, skipped{&op, 40};
    run({entry(SEEKDB_RUNTIME_HOOK_OBSERVE, obs), entry(SEEKDB_RUNTIME_HOOK_AROUND, around),
         entry(SEEKDB_RUNTIME_HOOK_REPLACE, replacement), entry(SEEKDB_RUNTIME_HOOK_AROUND, skipped)},
        op, SEEKDB_RUNTIME_OK, 0);
    CHECK((op.events == std::vector<int>{10, 20, 30, 21, 11, 100}));
    CHECK(op.validations == 1 && op.after_result == 0);
  }
  for (auto mode : {SEEKDB_RUNTIME_HOOK_AROUND, SEEKDB_RUNTIME_HOOK_REPLACE}) {
    for (int behavior = 0; behavior <= 6; ++behavior) {
      Operation op;
      Callback cb{&op, 20, behavior}, obs{&op, 10};
      if (behavior == 6) op.leaf_error = -333;
      const bool invalid = behavior == 4 || (mode == SEEKDB_RUNTIME_HOOK_AROUND && (behavior == 1 || behavior == 2));
      const int expected = invalid ? -22 : behavior == 3 ? -88 : behavior == 6 ? -333 :
                           (behavior == 2 || behavior == 5) ? -700 : 0;
      run({entry(SEEKDB_RUNTIME_HOOK_OBSERVE, obs), entry(mode, cb)}, op,
          invalid ? SEEKDB_RUNTIME_STATE_MISMATCH : SEEKDB_RUNTIME_OK, expected);
      CHECK(op.validations == int(!invalid && behavior != 3 && behavior != 6));
      int calls = 0; for (int event : op.events) calls += event == 99;
      CHECK(calls == int(behavior == 0 || behavior >= 4));
      CHECK(op.after_result == (invalid ? -22 : behavior == 3 ? -88 : behavior == 6 ? -333 : 0));
    }
  }
  // A malformed later entry must prevent even the first observer from running.
  for (int kind = 0; kind < 6; ++kind) {
    Operation op; Callback cb{&op, 10};
    auto bad = entry(SEEKDB_RUNTIME_HOOK_AROUND, cb);
    switch (kind) {
      case 0: bad.struct_size--; break;
      case 1: bad.struct_size++; break;
      case 2: bad.mode = 99; break;
      case 3: bad.invoke = nullptr; break;
      case 4: bad.observe = observe; break;
      case 5: bad.reserved[3] = 1; break;
    }
    run({entry(SEEKDB_RUNTIME_HOOK_OBSERVE, cb), bad}, op, SEEKDB_RUNTIME_INVALID, -22);
    CHECK(op.events.empty() && op.validations == 0);
  }
  {
    Operation op; Callback cb{&op, 10};
    std::vector<seekdb_runtime_hook_v2_t> hooks(64, entry(SEEKDB_RUNTIME_HOOK_OBSERVE, cb));
    run(hooks, op, SEEKDB_RUNTIME_OK, 0);
    CHECK(op.events.size() == 130 && op.validations == 1);
    op = Operation{}; hooks.push_back(hooks.back());
    run(hooks, op, SEEKDB_RUNTIME_INVALID, -22);
    CHECK(op.events.empty());
  }
  {
    Operation op; int32_t result = 123;
    CHECK(seekdb_runtime_hook_run_v2(nullptr, 0, leaf, &op, nullptr, -22, &result)
        == SEEKDB_RUNTIME_INVALID);
    CHECK(op.events.empty() && result == -22);
    run({}, op, SEEKDB_RUNTIME_OK, 0);
    CHECK((op.events == std::vector<int>{99, 100}));
  }
  std::cout << "Rust hook observation/around/replacement and host result validation passed\n";
}
