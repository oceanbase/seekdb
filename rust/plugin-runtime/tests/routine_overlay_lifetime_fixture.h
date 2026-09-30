// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
#ifndef SEEKDB_TEST_ROUTINE_OVERLAY_LIFETIME_FIXTURE_H_
#define SEEKDB_TEST_ROUTINE_OVERLAY_LIFETIME_FIXTURE_H_
#include "routine_overlay_guard_fixture.h"
#include "share/schema/routine_catalog_savepoint.h"
#include "lib/worker.h"
#include "rpc/ob_request.h"

namespace routine_overlay_lifetime_test {
using namespace oceanbase::common;
using namespace oceanbase::share::schema;

// Exercise the REAL ObSchema allocator selection used by request workers.
// A heap-allocated ObRoutineInfo with no explicit arena still borrows this one.
class RequestScope {
  class RequestWorker final : public oceanbase::lib::Worker {
  public:
    explicit RequestWorker(ObIAllocator &arena) { allocator_ = &arena; }
  };
public:
  explicit RequestScope(ObIAllocator &arena, bool use_schema_stack = true)
      : previous_worker_(&THIS_WORKER), previous_arena_(schema_stack_allocator()),
        worker_(arena), request_(oceanbase::rpc::ObRequest::OB_MYSQL) {
    // The offline thread's default Worker has no request allocator. Supply
    // both worker and optional schema-stack arenas, like a real request.
    worker_.set_session(previous_worker_->get_session());
    worker_.set_timeout_ts(previous_worker_->get_timeout_ts());
    worker_.set_disable_wait_flag(previous_worker_->get_disable_wait_flag());
    worker_.set_req_flag(&request_);
    oceanbase::lib::Worker::set_worker_to_thread_local(&worker_);
    schema_stack_allocator() = use_schema_stack ? &arena : nullptr;
  }
  ~RequestScope() {
    schema_stack_allocator() = previous_arena_;
    oceanbase::lib::Worker::set_worker_to_thread_local(previous_worker_);
  }
private:
  oceanbase::lib::Worker *previous_worker_;
  ObIAllocator *previous_arena_;
  RequestWorker worker_;
  oceanbase::rpc::ObRequest request_;
};

inline void run()
{
  // Zero-argument function (the live crash), ordinary parameters, and enough
  // parameters to move the pointer array beyond its inline capacity.
  for (bool use_schema_stack : {false, true}) for (int count : {0, 1, 70}) {
    auto privileges = std::make_shared<RoutinePrivilegeOverlay>(100, 123);
    auto overlay = std::make_shared<RoutineSchemaOverlay>(privileges);
    const ObRoutineInfo *saved = nullptr;
    auto service = std::make_unique<MockSchemaService>();
    auto manager = std::make_unique<ObSchemaMgr>();
    CHECK(manager->init() == OB_SUCCESS);
    ObArenaAllocator request_arena;
    {
      RequestScope request(request_arena, use_schema_stack);
      ObRoutineInfo source;
      CHECK(source.get_allocator() == &request_arena); // Prove the production precondition.
      source.set_database_id(100); source.set_routine_id(9001); source.set_owner_id(123);
      source.set_schema_version(42); source.set_package_id(OB_INVALID_ID);
      source.set_overload(0); source.set_subprogram_id(0); source.set_routine_type(ROUTINE_FUNCTION_TYPE);
      CHECK(source.set_routine_name(ObString::make_string("caller_value")) == OB_SUCCESS);
      CHECK(source.set_routine_body(ObString::make_string("RETURN 41")) == OB_SUCCESS);
      CHECK(source.set_priv_user(ObString::make_string("owner@%")) == OB_SUCCESS);
      CHECK(source.set_exec_env(ObString::make_string("environment")) == OB_SUCCESS);
      CHECK(source.set_comment(ObString::make_string("comment")) == OB_SUCCESS);
      CHECK(source.set_route_sql(ObString::make_string("route")) == OB_SUCCESS);
      for (int i = 0; i <= count; ++i) {
        ObRoutineParam parameter;
        parameter.set_routine_id(9001); parameter.set_schema_version(42);
        parameter.set_sequence(i); parameter.set_subprogram_id(0);
        parameter.set_param_position(i); parameter.set_param_level(0); parameter.set_param_type(ObIntType);
        if (i) CHECK(parameter.set_param_name(ObString::make_string("argument")) == OB_SUCCESS);
        CHECK(parameter.set_default_value(ObString::make_string("41")) == OB_SUCCESS);
        CHECK(parameter.set_type_name(ObString::make_string("typename")) == OB_SUCCESS);
        CHECK(parameter.set_type_subname(ObString::make_string("subname")) == OB_SUCCESS);
        ObSEArray<ObString, 2> extended;
        CHECK(extended.push_back(ObString::make_string("enum-value")) == OB_SUCCESS);
        CHECK(parameter.set_extended_type_info(extended) == OB_SUCCESS);
        CHECK(source.add_routine_param(parameter) == OB_SUCCESS);
      }
      CHECK(overlay->stage(source) == OB_SUCCESS);
      CHECK(privileges->record_create(source, true) == OB_SUCCESS);
      bool handled = false;
      CHECK(overlay->lookup(9001, handled, saved) == OB_SUCCESS && handled && saved);
      auto *owner = const_cast<ObRoutineInfo *>(saved)->get_allocator();
      // This fails deterministically on the old implementation, before reading
      // freed memory. Also verify deep copies of parameters, not just the shell.
      CHECK(owner != &request_arena && owner != &THIS_WORKER.get_allocator());
      for (int64_t i = 0; i < saved->get_routine_params().count(); ++i)
        CHECK(saved->get_routine_params().at(i)->get_allocator() == owner);
    }
    request_arena.reset();
    // Simulate subsequent request reuse, then acquire a NEW statement guard.
    void *noise = request_arena.alloc(128 * 1024);
    CHECK(noise != nullptr); MEMSET(noise, 0xa5, 128 * 1024);
    ObSchemaGetterGuard next;
    CHECK(MockSchemaService::bind(next, *service, *manager) == OB_SUCCESS);
    CHECK(next.attach_routine_overlay(overlay) == OB_SUCCESS);
    const ObRoutineInfo *found = nullptr;
    CHECK(next.get_standalone_function_info(100, ObString::make_string("caller_value"), found) == OB_SUCCESS);
    CHECK(found == saved && found->get_param_count() == count);
    CHECK(found->get_routine_body() == ObString::make_string("RETURN 41"));
    CHECK(found->get_priv_user() == ObString::make_string("owner@%"));
    CHECK(found->get_exec_env() == ObString::make_string("environment"));
    CHECK(found->get_comment() == ObString::make_string("comment"));
    CHECK(found->get_route_sql() == ObString::make_string("route"));
    CHECK(found->get_ret_info() != nullptr);
    for (int i = 0; i < count; ++i) {
      ObRoutineParam *param = nullptr;
      CHECK(found->get_routine_param(i, param) == OB_SUCCESS && param && param->get_param_position() == i + 1);
      CHECK(param->get_param_name() == ObString::make_string("argument"));
      CHECK(param->get_default_value() == ObString::make_string("41"));
      CHECK(param->get_type_name() == ObString::make_string("typename"));
      CHECK(param->get_type_subname() == ObString::make_string("subname"));
      CHECK(param->get_extended_type_info().count() == 1);
      CHECK(param->get_extended_type_info().at(0) == ObString::make_string("enum-value"));
    }
    // Diagnostic printing must remain safe too; don't hide the original log.
    char text[32768]; CHECK(found->to_string(text, sizeof(text)) > 0);
    {
      RoutineCatalogSavepoint mark(overlay, privileges);
      CHECK(mark.valid());
      CHECK(overlay->erase(100, found->get_routine_name(), ROUTINE_FUNCTION_TYPE, 9001) == OB_SUCCESS);
      CHECK(privileges->record_drop(*found) == OB_SUCCESS);
      CHECK(mark.rollback() == OB_SUCCESS);
    }
    overlay->retire(); // Revokes lookup, but borrowed storage still lives with the overlay.
    CHECK(saved->get_routine_body() == ObString::make_string("RETURN 41"));
    CHECK(saved->to_string(text, sizeof(text)) > 0);
  }
}
}
#endif
