// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
// Production sequence loop + Query resolver + host version reservations. The
// admission/stage callback is controlled; no actual Root/catalog transaction.
#ifndef SEEKDB_TEST_ROUTINE_SEQUENCE_FIXTURE_H_
#define SEEKDB_TEST_ROUTINE_SEQUENCE_FIXTURE_H_
#include "query/command/ob_root_service_serialization.h"

namespace routine_sequence_test {
using namespace oceanbase::common;
using namespace oceanbase::sql;
using namespace oceanbase::share::schema;
using namespace oceanbase::share::plugin;
using namespace oceanbase::rootserver;

inline void run(const char *root, const ObResolverParams &services, const ObSqlCtx &context,
                const ObRoutineInfo &original)
{
  auto versions = std::make_unique<routine_reservation_test::VersionService>();
  auto allocator = std::make_unique<routine_version_test::Allocator>(*services.sql_proxy_, *versions);
  versions->bind_sql(*allocator);
  auto manager = std::make_unique<ObSchemaMgr>();
  CHECK(manager->init() == OB_SUCCESS);
  CHECK(MockSchemaService::set_name_case_mode(*manager, OB_ORIGIN_AND_INSENSITIVE) == OB_SUCCESS);
  const ExtensionVersionSnapshot observed{91, 123, "1", ""};
  std::string error;
  for (const bool fail_tail : {false, true}) {
    ObSchemaGetterGuard view;
    CHECK(MockSchemaService::bind(view, *versions, *manager) == OB_SUCCESS);
    auto overlay = std::make_shared<RoutineSchemaOverlay>();
    CHECK(overlay->stage(original) == OB_SUCCESS);
    CHECK(view.attach_routine_overlay(overlay) == OB_SUCCESS);
    ExtensionUpdatePlan plan;
    CHECK(plan.load(root, 1, original.get_database_id(), fail_tail ? "sequence_driver" : "sequence_success",
        observed, "2", context.session_info_->get_sql_mode(), error) == OB_SUCCESS);
    ExtensionRoutineScriptResolver resolver(plan, services, context);
    CHECK(resolver.preflight(plan.request(), error) == OB_SUCCESS);
    routine_version_test::Transaction transaction;
    std::vector<std::unique_ptr<RoutineVersionReservation>> reservations;
    std::vector<const ExtensionRoutineUpdateOperation *> retained;
    int admitted = 0;
    int64_t latest = original.get_schema_version();
    auto fence = std::make_unique<ObDDLService>();
    const int status = resolve_extension_routine_sequence(resolver, resolver.statement_count(), view,
        [&](const ExtensionRoutineUpdateOperation &op) {
      const ObRoutineInfo *before = nullptr;
      CHECK(view.get_routine_info(original.get_routine_id(), before) == OB_SUCCESS && before);
      CHECK(before->get_schema_version() == latest);
      auto reservation = std::make_unique<RoutineVersionReservation>();
      if (op.kind_ == ExtensionRoutineUpdateOperation::Kind::ALTER) {
        CHECK(admitted < 2);
        const auto &arg = *op.create_arg_;
        CHECK(arg.error_info_.get_error_status() == ERROR_STATUS_NO_ERROR);
        CHECK(arg.routine_info_.get_schema_version() == latest);
        CHECK(arg.routine_info_.get_comment() == "sequence first");
        CHECK(arg.routine_info_.is_invoker_right() == (admitted == 1));
        CHECK(fence->check_parallel_ddl_conflict(view, arg) == OB_SUCCESS);
        ObRoutineInfo after;
        CHECK(after.assign(arg.routine_info_) == OB_SUCCESS);
        CHECK(RoutineVersionReservation::reserve(*versions, transaction, after, before, *reservation) == OB_SUCCESS);
        latest = reservation->version();
        after.set_schema_version(latest);
        CHECK(overlay->stage(after) == OB_SUCCESS);
        CHECK(fence->check_parallel_ddl_conflict(view, arg) == OB_ERR_PARALLEL_DDL_CONFLICT);
      } else {
        CHECK(admitted == 2 && op.kind_ == ExtensionRoutineUpdateOperation::Kind::DROP);
        CHECK(RoutineVersionReservation::reserve_drop(*versions, transaction, *before, *reservation) == OB_SUCCESS);
        CHECK(reservation->version() > latest);
        CHECK(overlay->erase(before->get_database_id(), before->get_routine_name(), before->get_routine_type(),
                            before->get_routine_id()) == OB_SUCCESS);
      }
      reservations.push_back(std::move(reservation));
      retained.push_back(&op);
      ++admitted;
      return OB_SUCCESS;
    }, error);
    CHECK(status == (fail_tail ? OB_ERR_SP_DOES_NOT_EXIST : OB_SUCCESS));
    CHECK(admitted == 3 && transaction.writes_.empty());
    // Earlier operation pointers remain valid after all later resolutions.
    CHECK(retained.at(0)->create_arg_->routine_info_.get_schema_version() == original.get_schema_version());
    CHECK(retained.at(1)->create_arg_->routine_info_.get_schema_version() == reservations.at(0)->version());
    CHECK(retained.at(0)->create_arg_->routine_info_.get_comment() == "sequence first");
    const ObRoutineInfo *deleted = nullptr;
    CHECK(view.get_routine_info(original.get_routine_id(), deleted) == OB_SUCCESS && deleted == nullptr);
    const ExtensionRoutineUpdateOperation *operation = retained.at(0);
    CHECK(resolver.resolve(fail_tail ? 3 : 0, view, operation, error) == OB_STATE_NOT_MATCH && operation == nullptr);
    CHECK(view.reset() == OB_SUCCESS);
  }
  // Whole-script support checks precede any resolver/admission work.
  ExtensionUpdatePlan unsupported;
  CHECK(unsupported.load(root, 1, original.get_database_id(), "sequence_state", observed, "2",
      context.session_info_->get_sql_mode(), error) == OB_SUCCESS);
  ExtensionRoutineScriptResolver invalid(unsupported, services, context);
  CHECK(invalid.preflight(unsupported.request(), error) == OB_NOT_SUPPORTED);

  // Failure of host admission ends the loop, without resolving the next SQL.
  ObSchemaGetterGuard view;
  CHECK(MockSchemaService::bind(view, *versions, *manager) == OB_SUCCESS);
  auto overlay = std::make_shared<RoutineSchemaOverlay>();
  CHECK(overlay->stage(original) == OB_SUCCESS);
  CHECK(view.attach_routine_overlay(overlay) == OB_SUCCESS);
  ExtensionUpdatePlan plan;
  CHECK(plan.load(root, 1, original.get_database_id(), "sequence_success", observed, "2",
      context.session_info_->get_sql_mode(), error) == OB_SUCCESS);
  ExtensionRoutineScriptResolver failed_admission(plan, services, context);
  auto stale = plan.request();
  stale.expected_extension_id_++;
  CHECK(failed_admission.preflight(stale, error) == OB_STATE_NOT_MATCH);
  int calls = 0;
  CHECK(resolve_extension_routine_sequence(failed_admission, 3, view,
      [&](const ExtensionRoutineUpdateOperation &) { ++calls; return OB_TIMEOUT; }, error) == OB_TIMEOUT);
  CHECK(calls == 1 && overlay->record_count() == 1);
  CHECK(resolve_extension_routine_sequence(failed_admission, 4097, view,
      [&](const ExtensionRoutineUpdateOperation &) { CHECK(false); return OB_SUCCESS; }, error) == OB_INVALID_ARGUMENT);
  ExtensionUpdatePlan fixed_empty;
  CHECK(fixed_empty.load(root, 1, original.get_database_id(), "sequence_fixed_empty", observed, "2",
      context.session_info_->get_sql_mode(), error) == OB_SUCCESS);
  ExtensionRoutineScriptResolver fixed_resolver(fixed_empty, services, context);
  CHECK(fixed_resolver.statement_count() == 0 && fixed_resolver.preflight(fixed_empty.request(), error) == OB_SUCCESS);
  CHECK(resolve_extension_routine_sequence(fixed_resolver, 0, view,
      [&](const ExtensionRoutineUpdateOperation &) { CHECK(false); return OB_SUCCESS; }, error) == OB_ERR_BAD_DATABASE);
  CHECK(view.reset() == OB_SUCCESS);

  // Exercise the actual Query orchestration entry. Only the Root command is
  // substituted: it verifies guard release and invokes the real callbacks on a
  // separate host view under the actual serialization guard, then rejects the
  // first operation (no transaction/commit is simulated).
  ObSchemaGetterGuard root_view;
  CHECK(MockSchemaService::bind(root_view, *versions, *manager) == OB_SUCCESS);
  auto root_overlay = std::make_shared<RoutineSchemaOverlay>();
  CHECK(root_overlay->stage(original) == OB_SUCCESS);
  CHECK(root_view.attach_routine_overlay(root_overlay) == OB_SUCCESS);
  class Command final : public ObLocalManagementService {
  public:
    Command(ObSchemaGetterGuard &caller, ObSchemaGetterGuard &view) : caller_(caller), view_(view) {}
    int calls_ = 0, admissions_ = 0, observations_ = 0;
    int read_extension_update_source(uint64_t tenant, uint64_t database, const std::string &name,
        ObSQLSessionInfo &session, ExtensionVersionSnapshot &observed, std::string &) override {
      ++observations_;
      CHECK(tenant == 1 && database == session.get_database_id() && name == "sequence_success");
      std::shared_ptr<const RoutineSchemaOverlay> released;
      CHECK(caller_.capture_routine_overlay(released) == OB_INNER_STAT_ERROR && !released);
      observed = {91, 123, "1", ""}; // Controlled authenticated-source observation, not a database read.
      return OB_SUCCESS;
    }
    int update_extension_routines(const ExtensionUpdateRequest &request,
        const ObIArray<ExtensionRoutineUpdateOperation> &operations, ObSQLSessionInfo &,
        uint64_t &identity, bool &changed, int &publication, std::string &error,
        IExtensionRoutineScript *script) override {
      ++calls_;
      CHECK(operations.empty() && script != nullptr);
      CHECK(identity == 0 && !changed && publication == OB_NOT_INIT);
      std::shared_ptr<const RoutineSchemaOverlay> released;
      CHECK(caller_.capture_routine_overlay(released) == OB_INNER_STAT_ERROR && !released);
      return oceanbase::query::serialize_root_service_call([&]() {
        CHECK(script->preflight(request, error) == OB_SUCCESS);
        return resolve_extension_routine_sequence(*script, script->statement_count(), view_,
            [&](const ExtensionRoutineUpdateOperation &) { ++admissions_; return OB_TIMEOUT; }, error);
      });
    }
  private:
    ObSchemaGetterGuard &caller_, &view_;
  } command(*context.schema_guard_, root_view);
  ObResolverParams bound_services = services;
  bound_services.root_command_service_ = &command;
  uint64_t identity = 9;
  bool changed = true;
  int publication = OB_SUCCESS;
  CHECK(ExtensionRoutineResolver::update(unsupported, bound_services, context, identity, changed,
      publication, error) == OB_NOT_SUPPORTED);
  CHECK(command.calls_ == 0 && identity == 0 && !changed && publication == OB_NOT_INIT);
  std::shared_ptr<const RoutineSchemaOverlay> unchanged;
  CHECK(context.schema_guard_->capture_routine_overlay(unchanged) == OB_SUCCESS && unchanged);
  unchanged.reset();
  CHECK(ExtensionRoutineResolver::update(plan, bound_services, context, identity, changed,
      publication, error) == OB_TIMEOUT);
  CHECK(command.calls_ == 1 && command.admissions_ == 1 && identity == 0 && !changed && publication == OB_NOT_INIT);
  // The SQL executor composes prepare then update without reacquiring Query's
  // old guard: Root supplies the new semantic view. Exercise that exact pairing.
  ExtensionUpdatePlan prepared;
  CHECK(prepared.prepare(root, "sequence_success", "2", context, command, error) == OB_SUCCESS);
  CHECK(command.observations_ == 1 && prepared.request().expected_extension_id_ == 91);
  CHECK(ExtensionRoutineResolver::update(prepared, bound_services, context, identity, changed,
      publication, error) == OB_TIMEOUT);
  CHECK(command.calls_ == 2 && command.admissions_ == 2 && identity == 0 && !changed && publication == OB_NOT_INIT);
  CHECK(root_view.reset() == OB_SUCCESS);

  // The actual process-wide Root lock is non-recursive. A nested callback is
  // rejected immediately; exceptions restore ownership for the next call.
  CHECK(!oceanbase::query::root_service_serial_active());
  CHECK(oceanbase::query::serialize_root_service_call([&]() {
    CHECK(oceanbase::query::root_service_serial_active());
    CHECK(oceanbase::query::serialize_root_service_call([]() {
      CHECK(false); return OB_SUCCESS;
    }) == OB_STATE_NOT_MATCH);
    return OB_SUCCESS;
  }) == OB_SUCCESS);
  try {
    oceanbase::query::serialize_root_service_call([]() -> int { throw 1; });
    CHECK(false);
  } catch (int) {}
  CHECK(!oceanbase::query::root_service_serial_active());
  CHECK(oceanbase::query::serialize_root_service_call([]() { return OB_SUCCESS; }) == OB_SUCCESS);
}
} // namespace routine_sequence_test
#endif
