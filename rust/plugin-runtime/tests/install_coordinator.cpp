// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
// Real C++/Rust driver boundary with an explicit transaction MODEL. This is
// not a test of the SQL transaction service or schema DDL rollback behavior.
#include "plugin_runtime.h"
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <vector>

#define CHECK(expr) do { if (!(expr)) { std::cerr << __LINE__ << ": " << #expr << std::endl; std::abort(); } } while (false)

struct TransactionModel {
  uint32_t fail = 0;
  bool rollback_fails = false;
  bool active = false;
  std::vector<uint32_t> phases;
  std::vector<unsigned> pending;
  std::vector<unsigned> committed;
};

static int32_t step(void *opaque, uint32_t phase, uint64_t *identity) noexcept
{
  auto &transaction = *static_cast<TransactionModel *>(opaque);
  try {
    transaction.phases.push_back(phase);
    switch (phase) {
      case SEEKDB_RUNTIME_INSTALL_PREFLIGHT:
        CHECK(!transaction.active);
        break;
      case SEEKDB_RUNTIME_INSTALL_BEGIN:
        transaction.active = true;
        break;
      case SEEKDB_RUNTIME_INSTALL_APPLY:
        CHECK(transaction.active);
        transaction.pending.push_back(1); // schema member
        break;
      case SEEKDB_RUNTIME_INSTALL_RECORD:
        CHECK(transaction.active);
        transaction.pending.push_back(2); // extension/member ownership
        *identity = 42;
        break;
      case SEEKDB_RUNTIME_INSTALL_COMMIT:
        CHECK(transaction.active);
        transaction.committed = transaction.pending;
        transaction.pending.clear();
        transaction.active = false;
        // Failure here models the commit reply being lost AFTER durability.
        break;
      case SEEKDB_RUNTIME_INSTALL_ROLLBACK:
        if (transaction.rollback_fails) return -4002;
        transaction.pending.clear();
        transaction.active = false;
        break;
      default:
        return -4003;
    }
    return transaction.fail == phase ? -4001 : 0;
  } catch (...) {
    return -4004; // never propagate a C++ exception through Rust
  }
}

struct DropTransactionModel {
  uint32_t fail = 0;
  bool rollback_fails = false;
  bool active = false;
  unsigned staged = 0;
  unsigned committed = 0;
  std::vector<uint32_t> phases;
};

static int32_t drop_step(void *opaque, uint32_t phase, uint64_t *identity) noexcept
{
  auto &model = *static_cast<DropTransactionModel *>(opaque);
  try {
    model.phases.push_back(phase);
    switch (phase) {
      case SEEKDB_RUNTIME_DROP_PREFLIGHT: CHECK(!model.active); break;
      case SEEKDB_RUNTIME_DROP_BEGIN: model.active = true; break;
      case SEEKDB_RUNTIME_DROP_LOCK_SNAPSHOT:
        CHECK(model.active && model.staged == 0);
        *identity = 91;
        break;
      case SEEKDB_RUNTIME_DROP_DETACH:
        CHECK(model.active && *identity == 91);
        model.staged |= 1;
        break;
      case SEEKDB_RUNTIME_DROP_APPLY:
        CHECK(model.active && model.staged == 1);
        model.staged |= 2;
        break;
      case SEEKDB_RUNTIME_DROP_RECORD:
        CHECK(model.active && model.staged == 3);
        model.staged |= 4;
        break;
      case SEEKDB_RUNTIME_DROP_COMMIT:
        CHECK(model.active && model.staged == 7);
        model.committed = model.staged;
        model.staged = 0;
        model.active = false;
        break;
      case SEEKDB_RUNTIME_DROP_ROLLBACK:
        if (model.rollback_fails) return -4002;
        model.staged = 0;
        model.active = false;
        break;
      default: return -4003;
    }
    return model.fail == phase ? -4001 : 0;
  } catch (...) { return -4004; }
}

static void test_drop_driver()
{
  for (uint32_t fail = 0; fail <= SEEKDB_RUNTIME_DROP_COMMIT; ++fail) {
    DropTransactionModel model;
    model.fail = fail;
    seekdb_runtime_extension_drop_result_t result{};
    CHECK(seekdb_runtime_extension_drop_run(&model, drop_step, &result) == SEEKDB_RUNTIME_OK);
    if (fail == 0 || fail == SEEKDB_RUNTIME_DROP_COMMIT) {
      CHECK(result.extension_id == 91 && model.committed == 7);
      CHECK(result.outcome == (fail == 0 ? SEEKDB_RUNTIME_INSTALL_COMMITTED : SEEKDB_RUNTIME_INSTALL_COMMIT_UNKNOWN));
      CHECK(model.phases.back() == SEEKDB_RUNTIME_DROP_COMMIT);
    } else {
      CHECK(result.extension_id == 0 && model.committed == 0 && model.staged == 0);
      CHECK(result.failed_phase == fail && result.operation_status == -4001);
      CHECK(result.outcome == (fail == SEEKDB_RUNTIME_DROP_PREFLIGHT
          ? SEEKDB_RUNTIME_INSTALL_NOT_STARTED : SEEKDB_RUNTIME_INSTALL_ROLLED_BACK));
      CHECK(model.phases.back() == (fail == SEEKDB_RUNTIME_DROP_PREFLIGHT
          ? SEEKDB_RUNTIME_DROP_PREFLIGHT : SEEKDB_RUNTIME_DROP_ROLLBACK));
    }
    CHECK(!model.active);
  }
  DropTransactionModel model;
  model.fail = SEEKDB_RUNTIME_DROP_APPLY;
  model.rollback_fails = true;
  seekdb_runtime_extension_drop_result_t result{};
  CHECK(seekdb_runtime_extension_drop_run(&model, drop_step, &result) == SEEKDB_RUNTIME_OK);
  CHECK(result.outcome == SEEKDB_RUNTIME_INSTALL_ROLLBACK_UNKNOWN && result.extension_id == 91);
  CHECK(result.operation_status == -4001 && result.rollback_status == -4002);
  CHECK(model.active && model.staged == 3 && model.committed == 0);
}

struct UpdateTransactionModel {
  uint32_t fail = 0;
  bool no_op = false;
  bool active = false;
  unsigned version = 1;
  unsigned pending_version = 1;
  std::vector<uint32_t> phases;
};

static int32_t update_step(void *opaque, uint32_t phase, uint64_t *identity) noexcept
{
  auto &model = *static_cast<UpdateTransactionModel *>(opaque);
  try {
    CHECK(*identity == 91);
    model.phases.push_back(phase);
    switch (phase) {
      case SEEKDB_RUNTIME_UPDATE_PREFLIGHT: CHECK(!model.active); break;
      case SEEKDB_RUNTIME_UPDATE_BEGIN: model.active = true; break;
      case SEEKDB_RUNTIME_UPDATE_LOCK_SNAPSHOT: CHECK(model.active && model.version == 1); break;
      case SEEKDB_RUNTIME_UPDATE_DETACH:
      case SEEKDB_RUNTIME_UPDATE_APPLY: CHECK(model.active && !model.no_op); break;
      case SEEKDB_RUNTIME_UPDATE_RECORD:
        CHECK(model.active && !model.no_op);
        model.pending_version = 2;
        break;
      case SEEKDB_RUNTIME_UPDATE_COMMIT:
        CHECK(model.active);
        model.version = model.pending_version;
        model.active = false;
        break;
      case SEEKDB_RUNTIME_UPDATE_ROLLBACK:
        model.pending_version = 1;
        model.active = false;
        break;
      default: return -4003;
    }
    return model.fail == phase ? -4001 : 0;
  } catch (...) { return -4004; }
}

static void test_update_driver()
{
  for (bool no_op : {false, true}) {
    for (uint32_t fail = 0; fail <= SEEKDB_RUNTIME_UPDATE_COMMIT; ++fail) {
      if (no_op && fail >= SEEKDB_RUNTIME_UPDATE_DETACH && fail <= SEEKDB_RUNTIME_UPDATE_RECORD) continue;
      UpdateTransactionModel model;
      model.fail = fail;
      model.no_op = no_op;
      seekdb_runtime_extension_update_result_t result{};
      CHECK(seekdb_runtime_extension_update_run(&model, update_step, 91, no_op ? 1 : 0, &result) == SEEKDB_RUNTIME_OK);
      if (fail == 0 || fail == SEEKDB_RUNTIME_UPDATE_COMMIT) {
        CHECK(result.outcome == (fail == 0 ? SEEKDB_RUNTIME_INSTALL_COMMITTED : SEEKDB_RUNTIME_INSTALL_COMMIT_UNKNOWN));
        CHECK(result.extension_id == 91 && model.version == (no_op ? 1U : 2U));
        CHECK(model.phases.back() == SEEKDB_RUNTIME_UPDATE_COMMIT);
        if (no_op) CHECK(model.phases == std::vector<uint32_t>({1, 2, 3, 7}));
      } else {
        CHECK(result.outcome == (fail == 1 ? SEEKDB_RUNTIME_INSTALL_NOT_STARTED : SEEKDB_RUNTIME_INSTALL_ROLLED_BACK));
        CHECK(result.extension_id == 0 && model.version == 1);
      }
      CHECK(!model.active);
    }
  }
}

int main()
{
  static_assert(sizeof(seekdb_runtime_extension_install_result_t) == 24);
  static_assert(offsetof(seekdb_runtime_extension_install_result_t, extension_id) == 16);
  for (uint32_t fail = 0; fail <= SEEKDB_RUNTIME_INSTALL_COMMIT; ++fail) {
    TransactionModel transaction;
    transaction.fail = fail;
    seekdb_runtime_extension_install_result_t result{};
    CHECK(seekdb_runtime_extension_install_run(&transaction, step, &result) == SEEKDB_RUNTIME_OK);
    if (fail == 0) {
      CHECK(result.outcome == SEEKDB_RUNTIME_INSTALL_COMMITTED);
      CHECK(result.extension_id == 42);
      CHECK(transaction.committed == std::vector<unsigned>({1, 2}));
    } else if (fail == SEEKDB_RUNTIME_INSTALL_PREFLIGHT) {
      CHECK(result.outcome == SEEKDB_RUNTIME_INSTALL_NOT_STARTED);
      CHECK(transaction.phases.size() == 1);
      CHECK(transaction.committed.empty());
    } else if (fail == SEEKDB_RUNTIME_INSTALL_COMMIT) {
      CHECK(result.outcome == SEEKDB_RUNTIME_INSTALL_COMMIT_UNKNOWN);
      CHECK(result.extension_id == 42 && result.operation_status == -4001);
      CHECK(transaction.phases.back() == SEEKDB_RUNTIME_INSTALL_COMMIT);
      CHECK(transaction.committed == std::vector<unsigned>({1, 2}));
    } else {
      CHECK(result.outcome == SEEKDB_RUNTIME_INSTALL_ROLLED_BACK);
      CHECK(result.extension_id == 0);
      CHECK(result.failed_phase == fail && result.operation_status == -4001);
      CHECK(transaction.phases.back() == SEEKDB_RUNTIME_INSTALL_ROLLBACK);
      CHECK(transaction.committed.empty() && transaction.pending.empty());
    }
    CHECK(!transaction.active);
  }
  TransactionModel transaction;
  transaction.fail = SEEKDB_RUNTIME_INSTALL_RECORD;
  transaction.rollback_fails = true;
  seekdb_runtime_extension_install_result_t result{};
  CHECK(seekdb_runtime_extension_install_run(&transaction, step, &result) == SEEKDB_RUNTIME_OK);
  CHECK(result.outcome == SEEKDB_RUNTIME_INSTALL_ROLLBACK_UNKNOWN);
  CHECK(result.operation_status == -4001 && result.rollback_status == -4002);
  CHECK(transaction.active && !transaction.pending.empty());
  CHECK(transaction.committed.empty());
  test_drop_driver();
  test_update_driver();
}
