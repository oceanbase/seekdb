// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
#include "plugin_runtime.h"
#include <cstdlib>
#include <iostream>
#include <vector>
#include <cstddef>
#define CHECK(expr) do { if (!(expr)) { \
  std::cerr << __LINE__ << ": " << #expr << std::endl; std::abort(); \
} } while (false)

struct Payload { std::vector<int> *events; int id; int error; };
static int32_t undo(void *p) {
  auto *value = static_cast<Payload *>(p);
  value->events->push_back(-value->id);
  const int error = value->error; delete value; return error;
}
static void release(void *p) {
  auto *value = static_cast<Payload *>(p);
  value->events->push_back(value->id); delete value;
}
static void host_failure()
{
  static_assert(sizeof(seekdb_runtime_query_operation_result) == 32);
  static_assert(offsetof(seekdb_runtime_query_operation_result, operation_error) == 8);
  static_assert(offsetof(seekdb_runtime_query_operation_result, poison_error) == 28);
  auto *tx = seekdb_runtime_query_transaction_create(77); CHECK(tx);
  std::vector<int> events;
  CHECK(seekdb_runtime_query_transaction_record(tx, 77, 10,
      new Payload{&events, 1, 0}, undo, release) == SEEKDB_RUNTIME_OK);
  CHECK(seekdb_runtime_query_transaction_fail(tx, 78, -4012) == SEEKDB_RUNTIME_STATE_MISMATCH);
  CHECK(seekdb_runtime_query_transaction_fail(tx, 77, 0) == SEEKDB_RUNTIME_INVALID);
  CHECK(seekdb_runtime_query_transaction_fail(tx, 77, -4012) == SEEKDB_RUNTIME_OK);
  CHECK(seekdb_runtime_query_transaction_fail(tx, 77, -4002) == SEEKDB_RUNTIME_OK);
  CHECK(events.empty()); // Poison never pretends data/view rollback happened.
  int32_t error = 0;
  CHECK(seekdb_runtime_query_transaction_prepare_commit(tx, 77, &error) == SEEKDB_RUNTIME_STATE_MISMATCH);
  CHECK(error == -4012);
  CHECK(seekdb_runtime_query_transaction_finish(tx, 77, 1, &error) == SEEKDB_RUNTIME_STATE_MISMATCH);
  CHECK(events.empty());
  CHECK(seekdb_runtime_query_transaction_finish(tx, 77, 0, &error) == SEEKDB_RUNTIME_OK);
  CHECK(error == -4012 && events == std::vector<int>({-1}));
  seekdb_runtime_query_transaction_destroy(tx);
  CHECK(events == std::vector<int>({-1}));
}
static void invalidation_requests()
{
  static_assert(sizeof(seekdb_runtime_routine_invalidation) == 24);
  static_assert(offsetof(seekdb_runtime_routine_invalidation, database) == 8);
  static_assert(offsetof(seekdb_runtime_routine_invalidation, routine) == 16);
  auto *tx = seekdb_runtime_query_transaction_create(77); CHECK(tx);
  int32_t error = 0;
  uint64_t count = 0, version = 0, operations = 0;
  seekdb_runtime_routine_invalidation output{9, 9, 9};
  const auto add = [&](uint64_t seq, uint64_t db = 100, uint64_t routine = 900) {
    return seekdb_runtime_query_transaction_record_invalidation(tx, 77, seq, db, routine);
  };
  const auto size = [&](uint64_t expected) {
    CHECK(seekdb_runtime_query_transaction_invalidation_count(tx, 77, &count, &error) == SEEKDB_RUNTIME_OK);
    CHECK(count == expected && error == 0);
  };
  const auto peek = [&] {
    return seekdb_runtime_query_transaction_peek_invalidation(tx, 77, &output);
  };
  CHECK(add(10) == SEEKDB_RUNTIME_STATE_MISMATCH);
  CHECK(add(0) == SEEKDB_RUNTIME_INVALID);
  CHECK(add(uint64_t{1} << 47) == SEEKDB_RUNTIME_INVALID);
  CHECK(add(10, 0) == SEEKDB_RUNTIME_INVALID);
  CHECK(add(10, 100, 0) == SEEKDB_RUNTIME_INVALID);
  CHECK(add(10, uint64_t{1} << 63) == SEEKDB_RUNTIME_INVALID);
  CHECK(add(10, 100, uint64_t{1} << 63) == SEEKDB_RUNTIME_INVALID);
  CHECK(peek() == SEEKDB_RUNTIME_STATE_MISMATCH);
  CHECK(output.ticket == 0 && output.database == 0 && output.routine == 0);
  CHECK(seekdb_runtime_query_transaction_ack_invalidation(tx, 77, 1) == SEEKDB_RUNTIME_STATE_MISMATCH);
  CHECK(seekdb_runtime_query_transaction_admit_ddl(tx, 77, 10, 7) == SEEKDB_RUNTIME_OK);
  CHECK(add(10) == SEEKDB_RUNTIME_STATE_MISMATCH); // Admission alone is not a schema write.
  CHECK(seekdb_runtime_query_transaction_record_schema_version(tx, 77, 10, 500) == SEEKDB_RUNTIME_OK);
  CHECK(add(11) == SEEKDB_RUNTIME_STATE_MISMATCH); // Must match the actual write barrier.
  CHECK(seekdb_runtime_query_transaction_record_invalidation(tx, 78, 10, 100, 900) == SEEKDB_RUNTIME_STATE_MISMATCH);
  size(0);
  std::vector<int> events;
  CHECK(seekdb_runtime_query_transaction_record(tx, 77, 10, new Payload{&events, 1, 0}, undo, release) == SEEKDB_RUNTIME_OK);
  CHECK(add(10) == SEEKDB_RUNTIME_OK); // ticket 1
  CHECK(seekdb_runtime_query_transaction_record_schema_version(tx, 77, 20, 600) == SEEKDB_RUNTIME_OK);
  CHECK(add(20, 100, 901) == SEEKDB_RUNTIME_OK); // ticket 2, later rolled back
  CHECK(seekdb_runtime_query_transaction_record(tx, 77, 20, new Payload{&events, 2, 0}, undo, release) == SEEKDB_RUNTIME_OK);
  size(2);
  CHECK(seekdb_runtime_query_transaction_rollback(tx, 77, 20, &error) == SEEKDB_RUNTIME_OK);
  size(1); CHECK(events == std::vector<int>({-2}));
  CHECK(add(10) == SEEKDB_RUNTIME_STATE_MISMATCH); // Barrier high-water did not rewind.
  CHECK(add(20) == SEEKDB_RUNTIME_STATE_MISMATCH); // Rolled-back schema write confers nothing.
  CHECK(seekdb_runtime_query_transaction_record_schema_version(tx, 77, 30, 700) == SEEKDB_RUNTIME_OK);
  CHECK(add(30) == SEEKDB_RUNTIME_OK); // ticket 3, duplicate object intentionally retained
  CHECK(add(30, 101, 902) == SEEKDB_RUNTIME_OK); // ticket 4
  CHECK(seekdb_runtime_query_transaction_record(tx, 77, 30, new Payload{&events, 3, 0}, undo, release) == SEEKDB_RUNTIME_OK);
  size(3);
  CHECK(seekdb_runtime_query_transaction_begin_prepare(tx, 77, &version, &operations, &error) == SEEKDB_RUNTIME_OK);
  CHECK(version == 700 && operations == 2); // Invalidations are not schema operations.
  CHECK(add(30) == SEEKDB_RUNTIME_STATE_MISMATCH);
  CHECK(peek() == SEEKDB_RUNTIME_STATE_MISMATCH); size(3);
  CHECK(seekdb_runtime_query_transaction_record_end_sign(tx, 77, 701) == SEEKDB_RUNTIME_OK);
  CHECK(seekdb_runtime_query_transaction_complete_prepare(tx, 77, 701, 0, &error) == SEEKDB_RUNTIME_OK);
  CHECK(peek() == SEEKDB_RUNTIME_STATE_MISMATCH); size(3);
  CHECK(seekdb_runtime_query_transaction_finish(tx, 77, 1, &error) == SEEKDB_RUNTIME_OK);
  CHECK(events == std::vector<int>({-2, 3, 1})); // Commit still releases marks newest first.
  size(3);
  CHECK(seekdb_runtime_query_transaction_peek_invalidation(tx, 78, &output) == SEEKDB_RUNTIME_STATE_MISMATCH);
  CHECK(output.ticket == 0 && output.database == 0 && output.routine == 0);
  CHECK(seekdb_runtime_query_transaction_invalidation_count(tx, 78, &count, &error) == SEEKDB_RUNTIME_STATE_MISMATCH);
  CHECK(count == 0 && error == 0);
  for (uint64_t ticket : {1u, 3u, 4u}) {
    CHECK(peek() == SEEKDB_RUNTIME_OK && output.ticket == ticket);
    CHECK(output.database == (ticket == 4 ? 101 : 100) && output.routine == (ticket == 4 ? 902 : 900));
    CHECK(peek() == SEEKDB_RUNTIME_OK && output.ticket == ticket); // Failed handoff/retry is non-consuming.
    CHECK(seekdb_runtime_query_transaction_ack_invalidation(tx, 78, ticket) == SEEKDB_RUNTIME_STATE_MISMATCH);
    CHECK(seekdb_runtime_query_transaction_ack_invalidation(tx, 77, ticket + 1) == SEEKDB_RUNTIME_STATE_MISMATCH);
    CHECK(seekdb_runtime_query_transaction_ack_invalidation(tx, 77, 0) == SEEKDB_RUNTIME_INVALID);
    CHECK(seekdb_runtime_query_transaction_ack_invalidation(tx, 77, ticket) == SEEKDB_RUNTIME_OK);
    CHECK(seekdb_runtime_query_transaction_ack_invalidation(tx, 77, ticket) == SEEKDB_RUNTIME_STATE_MISMATCH);
  }
  size(0); CHECK(peek() == SEEKDB_RUNTIME_OK);
  CHECK(output.ticket == 0 && output.database == 0 && output.routine == 0);
  seekdb_runtime_query_transaction_destroy(tx);
  CHECK(events == std::vector<int>({-2, 3, 1}));

  // Known abort and failed preparation never grant delivery. Unknown outcome
  // destruction restores only private marks and cannot publish anything.
  for (int outcome = 0; outcome < 3; ++outcome) {
    tx = seekdb_runtime_query_transaction_create(77); CHECK(tx);
    CHECK(seekdb_runtime_query_transaction_admit_ddl(tx, 77, 10, 7) == SEEKDB_RUNTIME_OK);
    CHECK(seekdb_runtime_query_transaction_record_schema_version(tx, 77, 10, 500) == SEEKDB_RUNTIME_OK);
    CHECK(add(10) == SEEKDB_RUNTIME_OK);
    if (outcome == 1) {
      CHECK(seekdb_runtime_query_transaction_begin_prepare(tx, 77, &version, &operations, &error) == SEEKDB_RUNTIME_OK);
      CHECK(seekdb_runtime_query_transaction_complete_prepare(tx, 77, 0, -4109, &error) == SEEKDB_RUNTIME_STATE_MISMATCH);
      CHECK(seekdb_runtime_query_transaction_invalidation_count(tx, 77, &count, &error) == SEEKDB_RUNTIME_STATE_MISMATCH);
      CHECK(count == 0 && error == -4109);
    }
    if (outcome != 2) {
      CHECK(seekdb_runtime_query_transaction_finish(tx, 77, 0, &error) == SEEKDB_RUNTIME_OK);
      CHECK(seekdb_runtime_query_transaction_invalidation_count(tx, 77, &count, &error) == SEEKDB_RUNTIME_STATE_MISMATCH);
      CHECK(count == 0);
    }
    CHECK(peek() == SEEKDB_RUNTIME_STATE_MISMATCH);
    CHECK(output.ticket == 0 && output.database == 0 && output.routine == 0);
    CHECK(seekdb_runtime_query_transaction_ack_invalidation(tx, 77, 1) == SEEKDB_RUNTIME_STATE_MISMATCH);
    seekdb_runtime_query_transaction_destroy(tx);
  }
  tx = seekdb_runtime_query_transaction_create(77); CHECK(tx);
  CHECK(seekdb_runtime_query_transaction_admit_ddl(tx, 77, 10, 7) == SEEKDB_RUNTIME_OK);
  CHECK(seekdb_runtime_query_transaction_record_schema_version(tx, 77, 10, 500) == SEEKDB_RUNTIME_OK);
  for (int i = 0; i < 16383; ++i) CHECK(add(10) == SEEKDB_RUNTIME_OK);
  CHECK(add(10) == SEEKDB_RUNTIME_LIMIT); size(16383);
  CHECK(seekdb_runtime_query_transaction_rollback(tx, 77, 10, &error) == SEEKDB_RUNTIME_OK);
  size(0);
  CHECK(add(10) == SEEKDB_RUNTIME_STATE_MISMATCH);
  CHECK(seekdb_runtime_query_transaction_admit_ddl(tx, 77, 20, 8) == SEEKDB_RUNTIME_OK);
  CHECK(seekdb_runtime_query_transaction_record_schema_version(tx, 77, 20, 600) == SEEKDB_RUNTIME_OK);
  CHECK(add(20) == SEEKDB_RUNTIME_OK); size(1); // Rollback reclaimed the common record budget.
  CHECK(seekdb_runtime_query_transaction_peek_invalidation(tx, 77, nullptr) == SEEKDB_RUNTIME_INVALID);
  CHECK(seekdb_runtime_query_transaction_invalidation_count(tx, 77, nullptr, &error) == SEEKDB_RUNTIME_INVALID);
  CHECK(seekdb_runtime_query_transaction_invalidation_count(tx, 77, &count, nullptr) == SEEKDB_RUNTIME_INVALID);
  seekdb_runtime_query_transaction_destroy(tx);
  CHECK(seekdb_runtime_query_transaction_record_invalidation(nullptr, 77, 10, 100, 900) == SEEKDB_RUNTIME_INVALID);
  CHECK(seekdb_runtime_query_transaction_ack_invalidation(nullptr, 77, 1) == SEEKDB_RUNTIME_INVALID);
  output = {9, 9, 9};
  CHECK(seekdb_runtime_query_transaction_peek_invalidation(nullptr, 77, &output) == SEEKDB_RUNTIME_INVALID);
  CHECK(output.ticket == 0 && output.database == 0 && output.routine == 0);
}
static void invalidation_queue_bridge()
{
  auto *queue = seekdb_runtime_invalidation_queue_create(1, 1); CHECK(queue);
  auto *tx = seekdb_runtime_query_transaction_create(88); CHECK(tx);
  CHECK(seekdb_runtime_query_transaction_admit_ddl(tx, 88, 10, 7) == SEEKDB_RUNTIME_OK);
  CHECK(seekdb_runtime_query_transaction_record_schema_version(tx, 88, 10, 500) == SEEKDB_RUNTIME_OK);
  CHECK(seekdb_runtime_query_transaction_record_invalidation(tx, 88, 10, 100, 900) == SEEKDB_RUNTIME_OK);
  CHECK(seekdb_runtime_query_transaction_reserve_invalidations(queue, tx, 88) == SEEKDB_RUNTIME_STATE_MISMATCH);
  uint64_t version = 0, count = 0, token = 99;
  int32_t error = 0;
  seekdb_runtime_routine_invalidation request{99, 99, 99};
  CHECK(seekdb_runtime_query_transaction_begin_prepare(tx, 88, &version, &count, &error) == SEEKDB_RUNTIME_OK);
  CHECK(seekdb_runtime_query_transaction_reserve_invalidations(nullptr, tx, 88) == SEEKDB_RUNTIME_INVALID);
  CHECK(seekdb_runtime_query_transaction_reserve_invalidations(queue, tx, 88) == SEEKDB_RUNTIME_OK);
  CHECK(seekdb_runtime_invalidation_queue_peek(queue, &token, &version, &request) == SEEKDB_RUNTIME_OK);
  CHECK(token == 0 && request.ticket == 0 && request.database == 0 && request.routine == 0);
  CHECK(seekdb_runtime_invalidation_queue_close(queue, 0) == SEEKDB_RUNTIME_OK);
  CHECK(seekdb_runtime_query_transaction_record_end_sign(tx, 88, 501) == SEEKDB_RUNTIME_OK);
  CHECK(seekdb_runtime_query_transaction_complete_prepare(tx, 88, 501, 0, &error) == SEEKDB_RUNTIME_OK);
  CHECK(seekdb_runtime_query_transaction_finish(tx, 88, 1, &error) == SEEKDB_RUNTIME_OK);
  seekdb_runtime_query_transaction_destroy(tx);
  CHECK(seekdb_runtime_invalidation_queue_peek(queue, &token, &version, &request) == SEEKDB_RUNTIME_OK);
  CHECK(version == 501);
  CHECK(token != 0 && request.ticket == 1 && request.database == 100 && request.routine == 900);
  CHECK(seekdb_runtime_invalidation_queue_complete(queue, token, request.ticket, 0) == SEEKDB_RUNTIME_OK);
  CHECK(seekdb_runtime_invalidation_queue_peek(queue, &token, &version, &request) == SEEKDB_RUNTIME_OK && token != 0);
  CHECK(seekdb_runtime_invalidation_queue_complete(queue, token, request.ticket + 1, 1) == SEEKDB_RUNTIME_STATE_MISMATCH);
  CHECK(seekdb_runtime_invalidation_queue_complete(queue, token, request.ticket, 1) == SEEKDB_RUNTIME_OK);
  CHECK(seekdb_runtime_invalidation_queue_complete(queue, token, request.ticket, 1) == SEEKDB_RUNTIME_STATE_MISMATCH);
  CHECK(seekdb_runtime_invalidation_queue_peek(queue, &token, &version, &request) == SEEKDB_RUNTIME_OK && token == 0);
  CHECK(seekdb_runtime_invalidation_queue_close(queue, 2) == SEEKDB_RUNTIME_INVALID);
  CHECK(seekdb_runtime_invalidation_queue_close(queue, 1) == SEEKDB_RUNTIME_OK);
  request = {99, 99, 99}; token = 99;
  CHECK(seekdb_runtime_invalidation_queue_peek(queue, &token, &version, &request) == SEEKDB_RUNTIME_STATE_MISMATCH);
  CHECK(token == 0 && request.ticket == 0 && request.database == 0 && request.routine == 0);
  seekdb_runtime_invalidation_queue_destroy(queue);
  CHECK(seekdb_runtime_invalidation_queue_peek(nullptr, &token, &version, &request) == SEEKDB_RUNTIME_INVALID);
  CHECK(seekdb_runtime_invalidation_queue_close(nullptr, 0) == SEEKDB_RUNTIME_INVALID);
  CHECK(seekdb_runtime_invalidation_queue_complete(nullptr, 1, 1, 1) == SEEKDB_RUNTIME_INVALID);
  seekdb_runtime_invalidation_queue_destroy(nullptr);
}
int main()
{
  std::vector<int> events;
  auto *tx = seekdb_runtime_query_transaction_create(123);
  CHECK(tx && !seekdb_runtime_query_transaction_create(0));
  int32_t error = 99;
  const auto record = [&](uint64_t identity, uint64_t seq, int id, int failure = 0) {
    auto *p = new Payload{&events, id, failure};
    const int status = seekdb_runtime_query_transaction_record(tx, identity, seq, p, undo, release);
    if (status != SEEKDB_RUNTIME_OK) delete p;
    return status;
  };
  CHECK(record(123, 10, 1) == SEEKDB_RUNTIME_OK);
  CHECK(record(123, 20, 2) == SEEKDB_RUNTIME_OK);
  CHECK(record(123, 20, 3) == SEEKDB_RUNTIME_OK); // Same barrier, reverse order.
  CHECK(record(124, 30, 4) == SEEKDB_RUNTIME_STATE_MISMATCH);
  CHECK(record(123, 19, 4) == SEEKDB_RUNTIME_STATE_MISMATCH);
  CHECK(record(123, 0, 4) == SEEKDB_RUNTIME_INVALID);
  CHECK(record(123, uint64_t{1} << 62, 4) == SEEKDB_RUNTIME_INVALID); // Packed seq rejected.
  CHECK(seekdb_runtime_query_transaction_rollback(tx, 124, 10, &error) == SEEKDB_RUNTIME_STATE_MISMATCH);
  CHECK(error == 0 && events.empty());
  CHECK(seekdb_runtime_query_transaction_rollback(tx, 123, 20, nullptr) == SEEKDB_RUNTIME_INVALID);
  CHECK(events.empty());
  CHECK(seekdb_runtime_query_transaction_rollback(tx, 123, 20, &error) == SEEKDB_RUNTIME_OK);
  CHECK(error == 0 && events == std::vector<int>({-3, -2}));
  CHECK(seekdb_runtime_query_transaction_rollback(tx, 123, 20, &error) == SEEKDB_RUNTIME_OK);
  CHECK(events.size() == 2); // Reusable SQL savepoint requires no new view mark.
  CHECK(record(123, 19, 4) == SEEKDB_RUNTIME_STATE_MISMATCH); // High-water never rewinds.
  CHECK(record(123, 30, 4) == SEEKDB_RUNTIME_OK);
  CHECK(seekdb_runtime_query_transaction_rollback(tx, 123, 20, &error) == SEEKDB_RUNTIME_OK);
  CHECK(events == std::vector<int>({-3, -2, -4}));
  CHECK(seekdb_runtime_query_transaction_finish(tx, 123, 2, &error) == SEEKDB_RUNTIME_INVALID);
  CHECK(seekdb_runtime_query_transaction_finish(tx, 123, 1, &error) == SEEKDB_RUNTIME_STATE_MISMATCH);
  CHECK(seekdb_runtime_query_transaction_prepare_commit(tx, 124, &error) == SEEKDB_RUNTIME_STATE_MISMATCH);
  CHECK(seekdb_runtime_query_transaction_prepare_commit(tx, 123, &error) == SEEKDB_RUNTIME_OK);
  CHECK(seekdb_runtime_query_transaction_prepare_commit(tx, 123, &error) == SEEKDB_RUNTIME_STATE_MISMATCH);
  CHECK(record(123, 40, 5) == SEEKDB_RUNTIME_STATE_MISMATCH);
  CHECK(seekdb_runtime_query_transaction_rollback(tx, 123, 10, &error) == SEEKDB_RUNTIME_STATE_MISMATCH);
  CHECK(seekdb_runtime_query_transaction_finish(tx, 123, 1, &error) == SEEKDB_RUNTIME_OK);
  CHECK(events == std::vector<int>({-3, -2, -4, 1}));
  CHECK(record(123, 40, 5) == SEEKDB_RUNTIME_STATE_MISMATCH);
  CHECK(seekdb_runtime_query_transaction_finish(tx, 123, 1, &error) == SEEKDB_RUNTIME_STATE_MISMATCH);
  seekdb_runtime_query_transaction_destroy(tx);
  CHECK(events.size() == 4);

  events.clear(); tx = seekdb_runtime_query_transaction_create(123); CHECK(tx);
  CHECK(record(123, 10, 1, -4101) == SEEKDB_RUNTIME_OK);
  CHECK(record(123, 20, 2, -4102) == SEEKDB_RUNTIME_OK);
  CHECK(record(123, 30, 3, -4103) == SEEKDB_RUNTIME_OK);
  CHECK(seekdb_runtime_query_transaction_rollback(tx, 123, 20, &error) == SEEKDB_RUNTIME_OK);
  CHECK(error == -4103 && events == std::vector<int>({-3, -2}));
  CHECK(record(123, 40, 4) == SEEKDB_RUNTIME_STATE_MISMATCH);
  CHECK(seekdb_runtime_query_transaction_prepare_commit(tx, 123, &error) == SEEKDB_RUNTIME_STATE_MISMATCH);
  CHECK(error == -4103);
  CHECK(seekdb_runtime_query_transaction_finish(tx, 123, 1, &error) == SEEKDB_RUNTIME_STATE_MISMATCH);
  CHECK(error == -4103 && events.size() == 2);
  CHECK(seekdb_runtime_query_transaction_finish(tx, 123, 0, &error) == SEEKDB_RUNTIME_OK);
  CHECK(error == -4103 && events == std::vector<int>({-3, -2, -1}));
  seekdb_runtime_query_transaction_destroy(tx); CHECK(events.size() == 3);

  events.clear(); tx = seekdb_runtime_query_transaction_create(123); CHECK(tx);
  CHECK(record(123, 10, 1) == SEEKDB_RUNTIME_OK);
  CHECK(record(123, 20, 2) == SEEKDB_RUNTIME_OK);
  CHECK(seekdb_runtime_query_transaction_prepare_commit(tx, 123, &error) == SEEKDB_RUNTIME_OK);
  seekdb_runtime_query_transaction_destroy(tx);
  CHECK(events == std::vector<int>({-2, -1})); // Unknown outcome: private cleanup only.
  seekdb_runtime_query_transaction_destroy(nullptr);

  events.clear(); tx = seekdb_runtime_query_transaction_create(123); CHECK(tx);
  for (int i = 1; i <= 16384; ++i) CHECK(record(123, i, i) == SEEKDB_RUNTIME_OK);
  CHECK(record(123, 16385, 16385) == SEEKDB_RUNTIME_LIMIT);
  CHECK(seekdb_runtime_query_transaction_finish(tx, 123, 0, &error) == SEEKDB_RUNTIME_OK);
  CHECK(error == 0 && events.size() == 16384 && events.front() == -16384 && events.back() == -1);
  seekdb_runtime_query_transaction_destroy(tx);
  events.clear(); tx = seekdb_runtime_query_transaction_create(123); CHECK(tx);
  uint64_t version = 99, operations = 99;
  const auto state = [&](uint64_t expected_version, uint64_t expected_operations) {
    CHECK(seekdb_runtime_query_transaction_schema_state(tx, 123, &version, &operations, &error) == SEEKDB_RUNTIME_OK);
    CHECK(error == 0 && version == expected_version && operations == expected_operations);
  };
  state(0, 0);
  CHECK(record(123, 10, 1) == SEEKDB_RUNTIME_OK);
  CHECK(seekdb_runtime_query_transaction_record_schema_version(tx, 123, 10, 500) == SEEKDB_RUNTIME_OK);
  CHECK(seekdb_runtime_query_transaction_record_schema_version(tx, 123, 10, 400) == SEEKDB_RUNTIME_OK);
  CHECK(record(123, 20, 2) == SEEKDB_RUNTIME_OK);
  CHECK(seekdb_runtime_query_transaction_record_schema_version(tx, 123, 20, 900) == SEEKDB_RUNTIME_OK);
  state(900, 3);
  CHECK(seekdb_runtime_query_transaction_schema_state(tx, 124, &version, &operations, &error) == SEEKDB_RUNTIME_STATE_MISMATCH);
  CHECK(version == 0 && operations == 0 && error == 0);
  CHECK(seekdb_runtime_query_transaction_record_schema_version(tx, 124, 20, 999) == SEEKDB_RUNTIME_STATE_MISMATCH);
  CHECK(seekdb_runtime_query_transaction_record_schema_version(tx, 123, 19, 999) == SEEKDB_RUNTIME_STATE_MISMATCH);
  CHECK(seekdb_runtime_query_transaction_record_schema_version(tx, 123, 20, 0) == SEEKDB_RUNTIME_INVALID);
  CHECK(seekdb_runtime_query_transaction_record_schema_version(tx, 123, 20, uint64_t{1} << 63) == SEEKDB_RUNTIME_INVALID);
  CHECK(seekdb_runtime_query_transaction_rollback(tx, 123, 20, &error) == SEEKDB_RUNTIME_OK);
  CHECK(events == std::vector<int>({-2})); state(500, 2);
  CHECK(seekdb_runtime_query_transaction_rollback(tx, 123, 10, &error) == SEEKDB_RUNTIME_OK);
  CHECK(events == std::vector<int>({-2, -1})); state(0, 0);
  CHECK(seekdb_runtime_query_transaction_record_schema_version(tx, 123, 19, 600) == SEEKDB_RUNTIME_STATE_MISMATCH);
  CHECK(seekdb_runtime_query_transaction_admit_ddl(tx, 123, 30, 7) == SEEKDB_RUNTIME_OK);
  CHECK(seekdb_runtime_query_transaction_record_schema_version(tx, 123, 30, 600) == SEEKDB_RUNTIME_OK);
  CHECK(seekdb_runtime_query_transaction_prepare_commit(tx, 123, &error) == SEEKDB_RUNTIME_STATE_MISMATCH);
  CHECK(seekdb_runtime_query_transaction_begin_prepare(tx, 123, &version, &operations, &error) == SEEKDB_RUNTIME_OK);
  CHECK(version == 600 && operations == 1 && error == 0);
  CHECK(seekdb_runtime_query_transaction_record_end_sign(tx, 123, 601) == SEEKDB_RUNTIME_OK);
  CHECK(seekdb_runtime_query_transaction_complete_prepare(tx, 123, 601, 0, &error) == SEEKDB_RUNTIME_OK);
  state(601, 2); // Seal exposes publication inputs, but cannot publish anything.
  CHECK(seekdb_runtime_query_transaction_record_schema_version(tx, 123, 30, 700) == SEEKDB_RUNTIME_STATE_MISMATCH);
  CHECK(seekdb_runtime_query_transaction_finish(tx, 123, 1, &error) == SEEKDB_RUNTIME_OK);
  CHECK(seekdb_runtime_query_transaction_schema_state(tx, 123, &version, &operations, &error) == SEEKDB_RUNTIME_STATE_MISMATCH);
  CHECK(version == 0 && operations == 0 && error == 0);
  seekdb_runtime_query_transaction_destroy(tx);

  tx = seekdb_runtime_query_transaction_create(123); CHECK(tx);
  CHECK(seekdb_runtime_query_transaction_admit_ddl(tx, 123, 1, 7) == SEEKDB_RUNTIME_OK);
  for (int i = 1; i <= 16384; ++i)
    CHECK(seekdb_runtime_query_transaction_record_schema_version(tx, 123, i, i) == SEEKDB_RUNTIME_OK);
  CHECK(record(123, 16385, 99) == SEEKDB_RUNTIME_LIMIT); // One shared budget, not two independent caps.
  CHECK(seekdb_runtime_query_transaction_record_schema_version(tx, 123, 16385, 1) == SEEKDB_RUNTIME_LIMIT);
  state(16384, 16384);
  // The finalizer has a fixed slot: full ordinary journal cannot make recording
  // an already-written end-sign allocate or fail on its record budget.
  CHECK(seekdb_runtime_query_transaction_begin_prepare(tx, 123, &version, &operations, &error) == SEEKDB_RUNTIME_OK);
  CHECK(seekdb_runtime_query_transaction_record_end_sign(tx, 123, 20000) == SEEKDB_RUNTIME_OK);
  state(20000, 16385);
  CHECK(seekdb_runtime_query_transaction_finish(tx, 123, 0, &error) == SEEKDB_RUNTIME_OK);
  seekdb_runtime_query_transaction_destroy(tx);
  events.clear(); tx = seekdb_runtime_query_transaction_create(123); CHECK(tx);
  CHECK(seekdb_runtime_query_transaction_record_schema_version(tx, 123, 10, 100) == SEEKDB_RUNTIME_OK);
  CHECK(record(123, 20, 1, -4107) == SEEKDB_RUNTIME_OK);
  CHECK(seekdb_runtime_query_transaction_record_schema_version(tx, 123, 20, 200) == SEEKDB_RUNTIME_OK);
  CHECK(seekdb_runtime_query_transaction_rollback(tx, 123, 20, &error) == SEEKDB_RUNTIME_OK && error == -4107);
  CHECK(seekdb_runtime_query_transaction_schema_state(tx, 123, &version, &operations, &error) == SEEKDB_RUNTIME_STATE_MISMATCH);
  CHECK(error == -4107 && version == 0 && operations == 0);
  CHECK(seekdb_runtime_query_transaction_record_schema_version(tx, 123, 30, 300) == SEEKDB_RUNTIME_STATE_MISMATCH);
  CHECK(seekdb_runtime_query_transaction_finish(tx, 123, 0, &error) == SEEKDB_RUNTIME_OK && error == -4107);
  CHECK(events == std::vector<int>({-1}));
  seekdb_runtime_query_transaction_destroy(tx);
  // Preparation freezes all ordinary admission, even between host SQL calls.
  // Wrong identities and invalid FFI inputs do not consume a valid attempt;
  // an actual completion attempt is terminal, including metadata mismatch.
  for (int scenario = 0; scenario < 9; ++scenario) {
    events.clear(); tx = seekdb_runtime_query_transaction_create(123); CHECK(tx);
    const bool changed = scenario != 1;
    CHECK(record(123, 10, 1, scenario == 5 ? -4999 : 0) == SEEKDB_RUNTIME_OK);
    if (changed) {
      CHECK(seekdb_runtime_query_transaction_admit_ddl(tx, 123, 10, 7) == SEEKDB_RUNTIME_OK);
      CHECK(seekdb_runtime_query_transaction_record_schema_version(tx, 123, 10, 100) == SEEKDB_RUNTIME_OK);
    }
    CHECK(seekdb_runtime_query_transaction_check_preparing(nullptr, 123, &error) == SEEKDB_RUNTIME_INVALID);
    CHECK(error == 0);
    CHECK(seekdb_runtime_query_transaction_check_preparing(tx, 123, &error) == SEEKDB_RUNTIME_STATE_MISMATCH);
    CHECK(seekdb_runtime_query_transaction_check_preparing(tx, 123, nullptr) == SEEKDB_RUNTIME_INVALID);
    CHECK(seekdb_runtime_query_transaction_record_end_sign(tx, 123, 101) == SEEKDB_RUNTIME_STATE_MISMATCH);
    CHECK(seekdb_runtime_query_transaction_begin_prepare(tx, 124, &version, &operations, &error) == SEEKDB_RUNTIME_STATE_MISMATCH);
    CHECK(version == 0 && operations == 0 && error == 0);
    CHECK(seekdb_runtime_query_transaction_begin_prepare(tx, 123, &version, &operations, nullptr) == SEEKDB_RUNTIME_INVALID);
    CHECK(seekdb_runtime_query_transaction_begin_prepare(tx, 123, &version, &operations, &error) == SEEKDB_RUNTIME_OK);
    CHECK(version == (changed ? 100 : 0) && operations == (changed ? 1 : 0));
    CHECK(seekdb_runtime_query_transaction_check_preparing(tx, 124, &error) == SEEKDB_RUNTIME_STATE_MISMATCH);
    CHECK(error == 0);
    CHECK(seekdb_runtime_query_transaction_check_preparing(tx, 123, &error) == SEEKDB_RUNTIME_OK && error == 0);
    CHECK(seekdb_runtime_query_transaction_begin_prepare(tx, 123, &version, &operations, &error) == SEEKDB_RUNTIME_STATE_MISMATCH);
    CHECK(version == 0 && operations == 0 && error == 0);
    CHECK(record(123, 20, 2) == SEEKDB_RUNTIME_STATE_MISMATCH);
    CHECK(seekdb_runtime_query_transaction_record_schema_version(tx, 123, 20, 102) == SEEKDB_RUNTIME_STATE_MISMATCH);
    CHECK(seekdb_runtime_query_transaction_rollback(tx, 123, 10, &error) == SEEKDB_RUNTIME_STATE_MISMATCH);
    CHECK(seekdb_runtime_query_transaction_prepare_commit(tx, 123, &error) == SEEKDB_RUNTIME_STATE_MISMATCH);
    CHECK(seekdb_runtime_query_transaction_finish(tx, 123, 1, &error) == SEEKDB_RUNTIME_STATE_MISMATCH);
    if (scenario >= 7) {
      if (scenario == 7) CHECK(seekdb_runtime_query_transaction_finish(tx, 123, 0, &error) == SEEKDB_RUNTIME_OK);
      seekdb_runtime_query_transaction_destroy(tx); // Aborted or unknown outcome while still Preparing.
      CHECK(events == std::vector<int>({-1}));
      continue;
    }
    CHECK(seekdb_runtime_query_transaction_record_end_sign(tx, 124, 101) == SEEKDB_RUNTIME_STATE_MISMATCH);
    CHECK(seekdb_runtime_query_transaction_record_end_sign(tx, 123, 0) == SEEKDB_RUNTIME_INVALID);
    CHECK(seekdb_runtime_query_transaction_record_end_sign(tx, 123, uint64_t{1} << 63) == SEEKDB_RUNTIME_INVALID);
    if (changed) CHECK(seekdb_runtime_query_transaction_record_end_sign(tx, 123, 100) == SEEKDB_RUNTIME_STATE_MISMATCH);
    if (scenario != 3) {
      CHECK(seekdb_runtime_query_transaction_record_end_sign(tx, 123, 101) ==
          (changed ? SEEKDB_RUNTIME_OK : SEEKDB_RUNTIME_STATE_MISMATCH));
      CHECK(seekdb_runtime_query_transaction_record_end_sign(tx, 123, 102) == SEEKDB_RUNTIME_STATE_MISMATCH);
    }
    CHECK(seekdb_runtime_query_transaction_complete_prepare(tx, 124, 101, -4108, &error) == SEEKDB_RUNTIME_STATE_MISMATCH);
    CHECK(error == 0);
    CHECK(seekdb_runtime_query_transaction_check_preparing(tx, 123, &error) == SEEKDB_RUNTIME_OK && error == 0);
    const auto final_version = !changed ? 0 : scenario == 2 ? 102 : 101;
    const int host_failure = scenario >= 4 ? -4108 : 0;
    const bool success = scenario < 2;
    CHECK(seekdb_runtime_query_transaction_complete_prepare(tx, 123, final_version, host_failure, &error) ==
        (success ? SEEKDB_RUNTIME_OK : SEEKDB_RUNTIME_STATE_MISMATCH));
    CHECK(error == host_failure);
    CHECK(seekdb_runtime_query_transaction_check_preparing(tx, 123, &error) == SEEKDB_RUNTIME_STATE_MISMATCH);
    CHECK(error == host_failure);
    CHECK(seekdb_runtime_query_transaction_complete_prepare(tx, 123, final_version, 0, &error) == SEEKDB_RUNTIME_STATE_MISMATCH);
    CHECK(error == host_failure);
    CHECK(seekdb_runtime_query_transaction_prepare_commit(tx, 123, &error) == SEEKDB_RUNTIME_STATE_MISMATCH);
    CHECK(error == host_failure);
    CHECK(record(123, 20, 3) == SEEKDB_RUNTIME_STATE_MISMATCH);
    if (success) state(changed ? 101 : 0, changed ? 2 : 0);
    else {
      CHECK(seekdb_runtime_query_transaction_schema_state(tx, 123, &version, &operations, &error) == SEEKDB_RUNTIME_STATE_MISMATCH);
      CHECK(version == 0 && operations == 0 && error == host_failure);
      CHECK(seekdb_runtime_query_transaction_finish(tx, 123, 1, &error) == SEEKDB_RUNTIME_STATE_MISMATCH);
    }
    if (scenario != 6) {
      CHECK(seekdb_runtime_query_transaction_finish(tx, 123, success ? 1 : 0, &error) == SEEKDB_RUNTIME_OK);
      CHECK(error == host_failure); // Abort undo failure cannot replace SQL/MDS first error.
    }
    seekdb_runtime_query_transaction_destroy(tx); // Also covers unknown outcome during failed preparation.
    CHECK(events == std::vector<int>({success ? 1 : -1}));
  }
  tx = seekdb_runtime_query_transaction_create(123); CHECK(tx);
  uint64_t epoch = 99, sequence = 99;
  const auto admission = [&](uint64_t e, uint64_t s) {
    CHECK(seekdb_runtime_query_transaction_ddl_admission(tx, 123, &epoch, &sequence, &error) == SEEKDB_RUNTIME_OK);
    CHECK(epoch == e && sequence == s && error == 0);
  };
  admission(0, 0);
  CHECK(seekdb_runtime_query_transaction_check_ddl_write(tx, 123, 10, &error) == SEEKDB_RUNTIME_STATE_MISMATCH);
  CHECK(seekdb_runtime_query_transaction_admit_ddl(tx, 124, 10, 7) == SEEKDB_RUNTIME_STATE_MISMATCH);
  CHECK(seekdb_runtime_query_transaction_admit_ddl(tx, 123, 0, 7) == SEEKDB_RUNTIME_INVALID);
  CHECK(seekdb_runtime_query_transaction_admit_ddl(tx, 123, 10, 0) == SEEKDB_RUNTIME_INVALID);
  CHECK(seekdb_runtime_query_transaction_admit_ddl(tx, 123, 10, uint64_t{1} << 63) == SEEKDB_RUNTIME_INVALID);
  CHECK(seekdb_runtime_query_transaction_admit_ddl(tx, 123, 10, 7) == SEEKDB_RUNTIME_OK);
  admission(7, 10);
  CHECK(seekdb_runtime_query_transaction_admit_ddl(tx, 123, 10, 8) == SEEKDB_RUNTIME_STATE_MISMATCH);
  CHECK(seekdb_runtime_query_transaction_check_ddl_write(tx, 123, 9, &error) == SEEKDB_RUNTIME_STATE_MISMATCH);
  CHECK(seekdb_runtime_query_transaction_check_ddl_write(tx, 123, 10, &error) == SEEKDB_RUNTIME_OK);
  CHECK(seekdb_runtime_query_transaction_record_schema_version(tx, 123, 10, 100) == SEEKDB_RUNTIME_OK);
  CHECK(seekdb_runtime_query_transaction_record_schema_version(tx, 123, 20, 200) == SEEKDB_RUNTIME_OK);
  CHECK(seekdb_runtime_query_transaction_rollback(tx, 123, 20, &error) == SEEKDB_RUNTIME_OK);
  admission(7, 10); state(100, 1);
  CHECK(seekdb_runtime_query_transaction_check_ddl_write(tx, 123, 19, &error) == SEEKDB_RUNTIME_STATE_MISMATCH);
  CHECK(seekdb_runtime_query_transaction_rollback(tx, 123, 10, &error) == SEEKDB_RUNTIME_OK);
  admission(0, 0); state(0, 0);
  CHECK(seekdb_runtime_query_transaction_admit_ddl(tx, 123, 19, 8) == SEEKDB_RUNTIME_STATE_MISMATCH);
  CHECK(seekdb_runtime_query_transaction_admit_ddl(tx, 123, 30, 8) == SEEKDB_RUNTIME_OK);
  admission(8, 30);
  CHECK(seekdb_runtime_query_transaction_record_schema_version(tx, 123, 30, 300) == SEEKDB_RUNTIME_OK);
  CHECK(seekdb_runtime_query_transaction_begin_prepare(tx, 123, &version, &operations, &error) == SEEKDB_RUNTIME_OK);
  CHECK(seekdb_runtime_query_transaction_check_ddl_write(tx, 123, 30, &error) == SEEKDB_RUNTIME_STATE_MISMATCH);
  admission(8, 30);
  CHECK(seekdb_runtime_query_transaction_finish(tx, 123, 0, &error) == SEEKDB_RUNTIME_OK);
  CHECK(seekdb_runtime_query_transaction_ddl_admission(tx, 123, &epoch, &sequence, &error) == SEEKDB_RUNTIME_STATE_MISMATCH);
  CHECK(epoch == 0 && sequence == 0 && error == 0);
  seekdb_runtime_query_transaction_destroy(tx);
  tx = seekdb_runtime_query_transaction_create(123); CHECK(tx);
  CHECK(seekdb_runtime_query_transaction_record_schema_version(tx, 123, 10, 100) == SEEKDB_RUNTIME_OK);
  CHECK(seekdb_runtime_query_transaction_begin_prepare(tx, 123, &version, &operations, &error) == SEEKDB_RUNTIME_STATE_MISMATCH);
  CHECK(version == 0 && operations == 0 && error == 0);
  CHECK(seekdb_runtime_query_transaction_admit_ddl(tx, 123, 10, 7) == SEEKDB_RUNTIME_STATE_MISMATCH); // No retroactive authority.
  seekdb_runtime_query_transaction_destroy(tx);
  invalidation_requests();
  host_failure();
  invalidation_queue_bridge();
  std::cout << "Rust catalog view journal C ABI ownership, barriers, failures and invalidation delivery passed\n";
}
