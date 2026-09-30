// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
#include "share/plugin/ob_plugin_registry.h"
#include "lib/ob_errno.h"
#include "plugin_runtime.h"
#include "nio.h"
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <thread>

using namespace oceanbase::share::plugin;
using namespace oceanbase::common;

#define CHECK(expr) do { if (!(expr)) { \
  std::cerr << __LINE__ << ": " << #expr << std::endl; std::abort(); \
} } while (false)

static void test_input_state()
{
  static_assert(sizeof(seekdb_runtime_input_edge) == 8);
  static_assert(offsetof(seekdb_runtime_input_edge, source) == 0);
  static_assert(offsetof(seekdb_runtime_input_edge, target) == 4);
  static_assert(sizeof(seekdb_runtime_input_effect) == 24);
  static_assert(offsetof(seekdb_runtime_input_effect, rows) == 0);
  static_assert(offsetof(seekdb_runtime_input_effect, bindings) == 8);
  static_assert(offsetof(seekdb_runtime_input_effect, ticket) == 16);
  // A fan-in followed by a dependent input, through the linked Rust archive.
  seekdb_runtime_input_edge edges[] = {{0, 2}, {1, 2}, {2, 3}};
  seekdb_runtime_input_state *state = nullptr;
  CHECK(seekdb_runtime_input_state_create(4, edges, 3, &state) == SEEKDB_RUNTIME_OK);
  CHECK(state != nullptr);
  edges[0] = {3, 0}; // create must own the graph, not this caller's array.
  auto begin = [&](uint32_t op, uint32_t input, uint64_t rows, uint64_t bindings) {
    seekdb_runtime_input_effect effect{};
    CHECK(seekdb_runtime_input_state_begin(state, op, input, &effect) == SEEKDB_RUNTIME_OK);
    CHECK(effect.rows == rows && effect.bindings == bindings && effect.ticket != 0);
    return effect.ticket;
  };
  auto finish = [&](uint64_t ticket, uint32_t outcome) {
    seekdb_runtime_input_effect effect{99, 99, 99};
    CHECK(seekdb_runtime_input_state_finish(state, ticket, outcome, &effect) == SEEKDB_RUNTIME_OK);
    CHECK(effect.rows == 0 && effect.bindings == 0 && effect.ticket == 0);
  };
  finish(begin(SEEKDB_RUNTIME_INPUT_READ, 0, 13, 12), SEEKDB_RUNTIME_INPUT_ROW);
  seekdb_runtime_input_effect effect{};
  CHECK(seekdb_runtime_input_state_begin(state, SEEKDB_RUNTIME_INPUT_BIND, 2, &effect) == SEEKDB_RUNTIME_STATE_MISMATCH);
  CHECK(effect.rows == 15 && effect.bindings == 15 && effect.ticket == 0);
  CHECK(seekdb_runtime_input_state_reset(state, 1, &effect) == SEEKDB_RUNTIME_OK);
  CHECK(effect.rows == 15 && effect.bindings == 15);
  finish(begin(SEEKDB_RUNTIME_INPUT_READ, 0, 13, 12), SEEKDB_RUNTIME_INPUT_ROW);
  finish(begin(SEEKDB_RUNTIME_INPUT_READ, 1, 14, 12), SEEKDB_RUNTIME_INPUT_ROW);
  finish(begin(SEEKDB_RUNTIME_INPUT_BIND, 2, 12, 12), SEEKDB_RUNTIME_INPUT_DONE);
  finish(begin(SEEKDB_RUNTIME_INPUT_READ, 2, 12, 8), SEEKDB_RUNTIME_INPUT_ROW);
  finish(begin(SEEKDB_RUNTIME_INPUT_BIND, 3, 8, 8), SEEKDB_RUNTIME_INPUT_DONE);
  finish(begin(SEEKDB_RUNTIME_INPUT_READ, 3, 8, 0), SEEKDB_RUNTIME_INPUT_ROW);
  // Rescanning 2 preserves its incoming binding, but revokes 3's environment.
  finish(begin(SEEKDB_RUNTIME_INPUT_RESCAN, 2, 12, 8), SEEKDB_RUNTIME_INPUT_DONE);
  finish(begin(SEEKDB_RUNTIME_INPUT_READ, 2, 12, 8), SEEKDB_RUNTIME_INPUT_ROW);
  finish(begin(SEEKDB_RUNTIME_INPUT_BIND, 3, 8, 8), SEEKDB_RUNTIME_INPUT_DONE);
  const auto stale = begin(SEEKDB_RUNTIME_INPUT_READ, 3, 8, 0);
  CHECK(seekdb_runtime_input_state_reset(state, 1, &effect) == SEEKDB_RUNTIME_OK);
  CHECK(seekdb_runtime_input_state_finish(state, stale, SEEKDB_RUNTIME_INPUT_ROW, &effect) == SEEKDB_RUNTIME_STATE_MISMATCH);
  CHECK(effect.rows == 15 && effect.bindings == 15 && effect.ticket == 0);
  CHECK(seekdb_runtime_input_state_begin(state, SEEKDB_RUNTIME_INPUT_READ, 0, &effect) == SEEKDB_RUNTIME_STATE_MISMATCH);
  CHECK(seekdb_runtime_input_state_reset(state, 1, &effect) == SEEKDB_RUNTIME_OK);
  const auto ticket = begin(SEEKDB_RUNTIME_INPUT_READ, 0, 13, 12);
  CHECK(ticket > stale); // reset must not permit an old completion to match.
  CHECK(seekdb_runtime_input_state_finish(state, ticket, SEEKDB_RUNTIME_INPUT_ERROR, &effect) == SEEKDB_RUNTIME_OK);
  CHECK(effect.rows == 15 && effect.bindings == 15 && effect.ticket == 0);
  CHECK(seekdb_runtime_input_state_begin(state, SEEKDB_RUNTIME_INPUT_READ, 0, &effect) == SEEKDB_RUNTIME_STATE_MISMATCH);
  seekdb_runtime_input_state_destroy(state);
  state = nullptr;
  const seekdb_runtime_input_edge cycle[] = {{0, 1}, {1, 0}};
  CHECK(seekdb_runtime_input_state_create(2, cycle, 2, &state) == SEEKDB_RUNTIME_DEPENDENCY_CYCLE);
  CHECK(state == nullptr);
  CHECK(seekdb_runtime_input_state_create(65, nullptr, 0, &state) == SEEKDB_RUNTIME_LIMIT);
  CHECK(state == nullptr);
  seekdb_runtime_input_state_destroy(state);
}

static std::shared_ptr<ObPluginGeneration> initializing(uint64_t version)
{
  auto g = std::make_shared<ObPluginGeneration>("test.plugin", version);
  CHECK(g->state() == ObPluginState::DISCOVERED);
  CHECK(g->transition_to(ObPluginState::VALIDATED) == OB_SUCCESS);
  CHECK(g->transition_to(ObPluginState::LOADED) == OB_SUCCESS);
  CHECK(g->transition_to(ObPluginState::INITIALIZING) == OB_SUCCESS);
  return g;
}

int main()
{
  test_input_state();
  // Match the catalog's sorted-name ordinals: ai=0, embedding=1, gis=2.
  // Two service requirements form one module-ordering edge; self-service is
  // legal for startup and does not make the module depend on its own loading.
  static_assert(sizeof(seekdb_runtime_dependency_edge_t) == 2 * sizeof(uint32_t));
  const seekdb_runtime_dependency_edge_t edges[] = {{1, 0}, {1, 0}, {1, 1}};
  uint32_t order[] = {99, 99, 99};
  uint32_t blocked = 123;
  CHECK(seekdb_runtime_dependency_plan(3, edges, 3, 1, order, 3, &blocked) == SEEKDB_RUNTIME_OK);
  CHECK(order[0] == 1 && order[1] == 0 && order[2] == 2 && blocked == UINT32_MAX);
  const seekdb_runtime_dependency_edge_t cycle[] = {{1, 2}, {2, 1}, {1, 0}};
  CHECK(seekdb_runtime_dependency_plan(3, cycle, 3, 1, order, 3, &blocked) == SEEKDB_RUNTIME_DEPENDENCY_CYCLE);
  CHECK(blocked == 0); // downstream of the cycle, not a claimed cycle member
  CHECK(order[0] == 1 && order[1] == 0 && order[2] == 2); // no partial plan
  CHECK(seekdb_runtime_dependency_plan(3, edges, 3, 1, order, 2, &blocked) == SEEKDB_RUNTIME_INVALID);
  CHECK(blocked == UINT32_MAX);

  // Both components must survive aggregate archive linking. The intentionally
  // invalid network ABI returns before binding any port or starting threads.
  int32_t nio_error = 0;
  CHECK(nio_start(nullptr, 0, nullptr, 0, 0, 0, nullptr, 0, &nio_error, 0) == nullptr);
  CHECK(nio_error == NIO_START_EABI);

  // Check every state through the actual C ABI, not only matching two C++ enums.
  auto *raw = seekdb_runtime_generation_create();
  CHECK(raw != nullptr);
  CHECK(seekdb_runtime_generation_state(raw) == SEEKDB_RUNTIME_DISCOVERED);
  CHECK(seekdb_runtime_generation_transition(raw, SEEKDB_RUNTIME_VALIDATED) == SEEKDB_RUNTIME_OK);
  CHECK(seekdb_runtime_generation_state(raw) == SEEKDB_RUNTIME_VALIDATED);
  CHECK(seekdb_runtime_generation_transition(raw, SEEKDB_RUNTIME_LOADED) == SEEKDB_RUNTIME_OK);
  CHECK(seekdb_runtime_generation_state(raw) == SEEKDB_RUNTIME_LOADED);
  CHECK(seekdb_runtime_generation_transition(raw, SEEKDB_RUNTIME_INITIALIZING) == SEEKDB_RUNTIME_OK);
  CHECK(seekdb_runtime_generation_state(raw) == SEEKDB_RUNTIME_INITIALIZING);
  CHECK(seekdb_runtime_generation_reserve(raw) == SEEKDB_RUNTIME_OK);
  CHECK(seekdb_runtime_generation_reserve(raw) == SEEKDB_RUNTIME_BUSY);
  CHECK(seekdb_runtime_generation_promote(raw) == SEEKDB_RUNTIME_OK);
  CHECK(seekdb_runtime_generation_state(raw) == SEEKDB_RUNTIME_ACTIVE);
  CHECK(seekdb_runtime_generation_transition(raw, 255) == SEEKDB_RUNTIME_INVALID);
  CHECK(seekdb_runtime_generation_acquire(raw) == 1);
  CHECK(seekdb_runtime_generation_quiesce(raw) == SEEKDB_RUNTIME_OK);
  CHECK(seekdb_runtime_generation_state(raw) == SEEKDB_RUNTIME_QUIESCING);
  CHECK(seekdb_runtime_generation_drain(raw, 0) == SEEKDB_RUNTIME_TIMEOUT);
  CHECK(seekdb_runtime_generation_release(raw) == SEEKDB_RUNTIME_OK);
  CHECK(seekdb_runtime_generation_transition(raw, SEEKDB_RUNTIME_FAILED) == SEEKDB_RUNTIME_OK);
  CHECK(seekdb_runtime_generation_state(raw) == SEEKDB_RUNTIME_FAILED);
  CHECK(seekdb_runtime_generation_quiesce(raw) == SEEKDB_RUNTIME_OK);
  CHECK(seekdb_runtime_generation_transition(raw, SEEKDB_RUNTIME_BLOCKED) == SEEKDB_RUNTIME_OK);
  CHECK(seekdb_runtime_generation_state(raw) == SEEKDB_RUNTIME_BLOCKED);
  CHECK(seekdb_runtime_generation_terminal_stop(raw) == SEEKDB_RUNTIME_OK);
  CHECK(seekdb_runtime_generation_state(raw) == SEEKDB_RUNTIME_STOPPED);
  seekdb_runtime_generation_destroy(raw);

  ObPluginServiceRegistry registry;
  auto g = initializing(1);
  const int service = 42;
  ObPluginRegistration registration;
  CHECK(registry.begin_registration(g, registration) == OB_SUCCESS);
  CHECK(registration.add_service("test.call", 1, 0, &service) == OB_SUCCESS);
  ObPluginActivationCandidate candidate;
  CHECK(registration.prepare(candidate) == OB_SUCCESS);
  CHECK(g->transition_to(ObPluginState::FAILED) == OB_EAGAIN);
  ObPluginLease lease;
  CHECK(registry.acquire("test.call", 1, 0, lease) == OB_ENTRY_NOT_EXIST);
  candidate.promote();
  CHECK(g->state() == ObPluginState::ACTIVE);
  CHECK(registry.acquire("test.call", 1, 0, lease) == OB_SUCCESS);
  CHECK(lease.service() == &service);
  CHECK(g->lease_count() == 1);
  ObPluginLease moved(std::move(lease));
  CHECK(!lease.is_valid());
  CHECK(g->lease_count() == 1);
  CHECK(registry.quiesce(g) == OB_SUCCESS);
  CHECK(registry.acquire("test.call", 1, 0, lease) == OB_ENTRY_NOT_EXIST);
  CHECK(g->wait_for_drain(0) == OB_TIMEOUT);
  CHECK(g->transition_to(ObPluginState::STOPPED) == OB_EAGAIN);
  std::thread releaser([&] { moved.reset(); });
  CHECK(g->wait_for_drain(2 * 1000 * 1000) == OB_SUCCESS);
  releaser.join();
  CHECK(registry.mark_stopped(g) == OB_SUCCESS);
  CHECK(g->state() == ObPluginState::STOPPED);

  // Aborted candidates release the Rust reservation and publish no service.
  auto next = initializing(2);
  CHECK(registry.begin_registration(next, registration) == OB_SUCCESS);
  CHECK(registration.add_service("test.call", 1, 0, &service) == OB_SUCCESS);
  CHECK(registration.prepare(candidate) == OB_SUCCESS);
  candidate.abort();
  CHECK(next->transition_to(ObPluginState::FAILED) == OB_SUCCESS);
  CHECK(registry.acquire("test.call", 1, 0, lease) == OB_ENTRY_NOT_EXIST);
  std::cout << "Rust/C++ lifecycle, publication, lease and drain integration passed\n";
}
