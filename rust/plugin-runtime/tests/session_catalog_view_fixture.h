// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
// Real session, descriptor observations, Rust journal, guards and SQL context
// initialization. Descriptor states/outcomes are controlled, not live commits.
#ifndef SEEKDB_TEST_SESSION_CATALOG_VIEW_FIXTURE_H_
#define SEEKDB_TEST_SESSION_CATALOG_VIEW_FIXTURE_H_
#include "storage/tx/ob_trans_define_v4.h"
#include "sql/ob_sql.h"
#include "sql/ob_result_set.h"
#include "share/schema/routine_catalog_transaction.h"
#include "share/schema/routine_catalog_savepoint.h"
#include <utility>

namespace oceanbase { namespace transaction {
class SessionCatalogTestAccess
{
public:
  static void active(ObTxDesc &tx, int64_t id = 771, int64_t base = 1000, bool read_only = false)
  {
    tx.tx_id_ = ObTransID(id); tx.seq_base_ = base;
    tx.state_ = ObTxDesc::State::ACTIVE;
    tx.access_mode_ = read_only ? ObTxAccessMode::RD_ONLY : ObTxAccessMode::RW;
    tx.flags_.SHADOW_ = true; // Never registered with a descriptor manager.
  }
  static void committed(ObTxDesc &tx) { tx.state_ = ObTxDesc::State::COMMITTED; }
  static void idle(ObTxDesc &tx, bool prepared = true, int64_t id = 771, int64_t base = 1000)
  {
    active(tx, id, base);
    tx.state_ = ObTxDesc::State::IDLE;
    tx.flags_.EXPLICIT_ = false;
    tx.release_all_implicit_savepoint();
    if (prepared) tx.add_implicit_savepoint(ObTxSEQ(2, 0));
  }
  static void implicit_active(ObTxDesc &tx) { tx.state_ = ObTxDesc::State::IMPLICIT_ACTIVE; }
  static void check_statement_states(ObTxDesc &tx)
  {
    // Retaining a stale savepoint must not admit rollback/terminal states.
    for (auto state : {ObTxDesc::State::INVL, ObTxDesc::State::ROLLBACK_SAVEPOINT,
        ObTxDesc::State::IN_TERMINATE, ObTxDesc::State::ABORTED,
        ObTxDesc::State::ROLLED_BACK, ObTxDesc::State::COMMIT_TIMEOUT,
        ObTxDesc::State::COMMIT_UNKNOWN, ObTxDesc::State::COMMITTED}) {
      idle(tx);
      tx.state_ = state;
      CHECK(!data_plane::tx_desc_is_statement_ready(&tx));
    }
  }
};
} }

namespace session_catalog_view_test {
using namespace oceanbase::common;
using namespace oceanbase::sql;
using namespace oceanbase::share::schema;
using oceanbase::transaction::ObTxDesc;
using oceanbase::transaction::ObTxSEQ;
using oceanbase::transaction::SessionCatalogTestAccess;

inline void statement_ready()
{
  auto service = std::make_unique<MockSchemaService>();
  auto manager = std::make_unique<ObSchemaMgr>();
  CHECK(manager->init() == OB_SUCCESS);
  ObSQLSessionInfo session;
  ObTxDesc tx;
  struct Binding {
    ObSQLSessionInfo &session;
    Binding(ObSQLSessionInfo &session, ObTxDesc &tx) : session(session) { session.get_tx_desc() = &tx; }
    ~Binding() { session.discard_plugin_catalog_transaction(); session.get_tx_desc() = nullptr; }
  } binding(session, tx);
  std::shared_ptr<RoutineSchemaOverlay> schema;
  std::shared_ptr<RoutinePrivilegeOverlay> privileges;
  std::shared_ptr<RoutineCatalogTransaction> journal;
  const auto prepare = [&] {
    return session.prepare_plugin_catalog_view(ObTxSEQ(10, 0), schema, privileges, journal);
  };
  CHECK(!oceanbase::data_plane::tx_desc_is_statement_ready(nullptr));
  CHECK(!oceanbase::data_plane::tx_desc_is_statement_ready(&tx));
  for (auto identity : {std::pair<int64_t, int64_t>{0, 1000}, {771, 0}}) {
    SessionCatalogTestAccess::idle(tx, true, identity.first, identity.second);
    CHECK(!oceanbase::data_plane::tx_desc_is_statement_ready(&tx));
    CHECK(prepare() == OB_TRANS_INVALID_STATE && !journal);
  }
  SessionCatalogTestAccess::idle(tx, false);
  CHECK(!oceanbase::data_plane::tx_desc_is_statement_ready(&tx));
  CHECK(prepare() == OB_TRANS_INVALID_STATE && !journal);
  SessionCatalogTestAccess::check_statement_states(tx);
  CHECK(prepare() == OB_TRANS_INVALID_STATE && !journal);
  SessionCatalogTestAccess::idle(tx);
  CHECK(!oceanbase::data_plane::tx_desc_is_active(&tx));
  CHECK(!oceanbase::data_plane::tx_desc_is_explicit(&tx));
  CHECK(oceanbase::data_plane::tx_desc_is_statement_ready(&tx));
  CHECK(prepare() == OB_SUCCESS && journal && schema && privileges);
  CHECK(session.owns_plugin_catalog_transaction(journal.get(), 771, 1000));
  ObSchemaGetterGuard guard;
  CHECK(MockSchemaService::bind(guard, *service, *manager) == OB_SUCCESS);
  CHECK(session.bind_plugin_catalog_view(guard) == OB_SUCCESS && guard.has_routine_overlay());
  // Merely admitting a borrowed view does not authorize a completed write or
  // pretend that a transaction has become active for commit preparation.
  CHECK(session.record_plugin_catalog_schema_version(ObTxSEQ(10, 0), 42) == OB_TRANS_INVALID_STATE);
  CHECK(session.prepare_plugin_catalog_commit() == OB_TRANS_INVALID_STATE);
  CHECK(!oceanbase::data_plane::tx_desc_is_active(&tx));
  CHECK(!oceanbase::data_plane::tx_desc_is_explicit(&tx));
  SessionCatalogTestAccess::idle(tx, true, 772);
  CHECK(session.bind_plugin_catalog_view(guard) == OB_TRANS_INVALID_STATE);
  SessionCatalogTestAccess::idle(tx, true, 771, 1001);
  CHECK(session.bind_plugin_catalog_view(guard) == OB_TRANS_INVALID_STATE);
  SessionCatalogTestAccess::idle(tx);
  // Operation rollback releases the inner mark, retaining the statement mark.
  tx.add_implicit_savepoint(ObTxSEQ(10, 0));
  tx.release_implicit_savepoint(ObTxSEQ(10, 0));
  CHECK(oceanbase::data_plane::tx_desc_is_statement_ready(&tx));
  CHECK(session.rollback_plugin_catalog_view(771, ObTxSEQ(10, 0)) == OB_SUCCESS);
  CHECK(session.bind_plugin_catalog_view(guard) == OB_SUCCESS);
  tx.release_all_implicit_savepoint();
  CHECK(!oceanbase::data_plane::tx_desc_is_statement_ready(&tx));
  CHECK(session.bind_plugin_catalog_view(guard) == OB_TRANS_INVALID_STATE);
  SessionCatalogTestAccess::idle(tx);
  // First real storage write activates this same identity, not a replacement.
  SessionCatalogTestAccess::implicit_active(tx);
  CHECK(oceanbase::data_plane::tx_desc_is_active(&tx));
  CHECK(session.bind_plugin_catalog_view(guard) == OB_SUCCESS);
  CHECK(!oceanbase::data_plane::tx_desc_is_explicit(&tx));
  CHECK(session.prepare_plugin_catalog_commit() == OB_SUCCESS); // View-only fixture.
  SessionCatalogTestAccess::committed(tx);
  CHECK(session.bind_plugin_catalog_view(guard) == OB_TRANS_INVALID_STATE);
  CHECK(session.complete_plugin_catalog_transaction(771, OB_SUCCESS, false) == OB_SUCCESS);
  CHECK(schema->is_retired() && privileges->is_retired());
}

inline void run()
{
  statement_ready();
  auto service = std::make_unique<MockSchemaService>();
  auto manager = std::make_unique<ObSchemaMgr>();
  CHECK(manager->init() == OB_SUCCESS);
  auto session = std::make_unique<ObSQLSessionInfo>();
  std::shared_ptr<RoutineSchemaOverlay> schema;
  std::shared_ptr<RoutinePrivilegeOverlay> privileges;
  std::shared_ptr<RoutineCatalogTransaction> journal;
  const auto prepare = [&](uint64_t seq) {
    return session->prepare_plugin_catalog_view(ObTxSEQ(seq, 0), schema, privileges, journal);
  };
  CHECK(prepare(10) == OB_TRANS_INVALID_STATE);
  CHECK(!schema && !privileges && !journal && !session->has_plugin_catalog_transaction());
  ObTxDesc tx;
  struct Binding {
    ObSQLSessionInfo &session;
    Binding(ObSQLSessionInfo &session, ObTxDesc &tx) : session(session) { session.get_tx_desc() = &tx; }
    ~Binding() { session.discard_plugin_catalog_transaction(); session.get_tx_desc() = nullptr; }
  } binding(*session, tx);
  CHECK(prepare(10) == OB_TRANS_INVALID_STATE);
  SessionCatalogTestAccess::active(tx, 771, 0);
  CHECK(prepare(10) == OB_TRANS_INVALID_STATE);
  SessionCatalogTestAccess::active(tx, 771, 1000, true);
  CHECK(prepare(10) == OB_ERR_READ_ONLY_TRANSACTION);
  SessionCatalogTestAccess::active(tx);
  CHECK(session->prepare_plugin_catalog_view(ObTxSEQ::INVL(), schema, privileges, journal) == OB_INVALID_ARGUMENT);
  CHECK(!session->has_plugin_catalog_transaction());
  CHECK(session->prepare_plugin_catalog_view(ObTxSEQ(10, 1), schema, privileges, journal) == OB_NOT_SUPPORTED);
  CHECK(!schema && !privileges && !journal && !session->has_plugin_catalog_transaction());
  CHECK(prepare(10) == OB_SUCCESS);
  CHECK(schema && privileges && journal && schema->privileges() == privileges.get());
  CHECK(session->owns_plugin_catalog_transaction(journal.get(), 771, 1000));
  const auto original_schema = schema;
  const auto original_privileges = privileges;
  const auto original_journal = journal;
  ObRoutineInfo routine;
  routine.set_database_id(100); routine.set_owner_id(123); routine.set_routine_id(9001);
  routine.set_schema_version(42); routine.set_package_id(OB_INVALID_ID); routine.set_overload(0);
  routine.set_routine_type(ROUTINE_FUNCTION_TYPE);
  CHECK(routine.set_routine_name("session_value") == OB_SUCCESS);
  CHECK(routine.set_routine_body("RETURN 1") == OB_SUCCESS);
  CHECK(privileges->record_create(routine, true) == OB_SUCCESS && schema->stage(routine) == OB_SUCCESS);
  // Keep the first view in the actual session cache, not only a stack guard.
  auto &first = session->get_cached_schema_guard_info().get_schema_guard();
  ObSchemaGetterGuard next, foreign, uninitialized;
  for (auto *guard : {&first, &next, &foreign}) CHECK(MockSchemaService::bind(*guard, *service, *manager) == OB_SUCCESS);
  CHECK(session->bind_plugin_catalog_view(uninitialized) == OB_INNER_STAT_ERROR);
  CHECK(session->bind_plugin_catalog_view(first) == OB_SUCCESS);
  CHECK(session->bind_plugin_catalog_view(first) == OB_SUCCESS);
  auto foreign_privileges = std::make_shared<RoutinePrivilegeOverlay>();
  auto foreign_schema = std::make_shared<RoutineSchemaOverlay>(foreign_privileges);
  CHECK(foreign.attach_routine_overlay(foreign_schema) == OB_SUCCESS);
  CHECK(session->bind_plugin_catalog_view(foreign) == OB_STATE_NOT_MATCH);
  CHECK(session->record_plugin_catalog_view(ObTxSEQ(20, 0), foreign_schema, foreign_privileges) == OB_STATE_NOT_MATCH);
  CHECK(session->record_plugin_catalog_view(ObTxSEQ(20, 0), schema, foreign_privileges) == OB_INVALID_ARGUMENT);
  // A fresh SQL statement's actual initialization attaches the session pair;
  // no parent guard is passed or manually inherited by this fixture.
  struct Access final : oceanbase::query::ObIPlanCacheAccessService {
    int depth = 0;
    void enter_access() override { ++depth; }
    void leave_access() override { CHECK(--depth >= 0); }
    void check_current_thread() override { CHECK(depth > 0); }
    int get_global_safe_timestamp(int64_t &) const override { CHECK(false); return OB_ERR_UNEXPECTED; }
  } access;
  auto engine = std::make_unique<ObSql>();
  ObArenaAllocator arena;
  {
    ObSqlCtx context; context.session_info_ = session.get(); context.schema_guard_ = &next;
    ObResultSet result(*session, arena, access);
    CHECK(!next.has_routine_overlay());
    CHECK(engine->init_result_set(context, result) == OB_SUCCESS);
    CHECK(next.has_routine_overlay() && result.get_exec_context().get_physical_plan_ctx());
  }
  {
    ObSqlCtx context; context.session_info_ = session.get(); context.schema_guard_ = &foreign;
    ObResultSet result(*session, arena, access);
    CHECK(engine->init_result_set(context, result) == OB_STATE_NOT_MATCH);
    CHECK(!result.get_exec_context().get_physical_plan_ctx());
  }
  CHECK(access.depth == 0);
  const auto lookup = [&](uint64_t db, uint64_t id, uint64_t owner, bool present) {
    bool handled = false; const ObRoutineInfo *found = nullptr;
    CHECK(schema->lookup(db, OB_INVALID_ID, routine.get_routine_name(), 0, ROUTINE_FUNCTION_TYPE, handled, found) == OB_SUCCESS);
    CHECK(handled == present && (found ? found->get_routine_id() : 0) == (present ? id : 0));
    bool granted = false; ObPrivSet bits = 0;
    CHECK(privileges->lookup(db, routine.get_routine_name(), ROUTINE_FUNCTION_TYPE,
        owner, handled, found, granted, bits) == OB_SUCCESS);
    CHECK(granted == present && bits == (present ? OB_PRIV_EXECUTE | OB_PRIV_ALTER_ROUTINE : 0));
    if (present) {
      const ObRoutineInfo *from_guard = nullptr;
      CHECK(next.get_routine_info(id, from_guard) == OB_SUCCESS && from_guard == found);
    }
    return found;
  };
  const auto *borrowed = lookup(100, 9001, 123, true);
  session->set_database_id(101); session->set_priv_user_id(125);
  CHECK(prepare(20) == OB_SUCCESS);
  CHECK(schema == original_schema && privileges == original_privileges && journal == original_journal);
  routine.set_database_id(101); routine.set_routine_id(9002); routine.set_owner_id(125);
  CHECK(privileges->record_create(routine, true) == OB_SUCCESS && schema->stage(routine) == OB_SUCCESS);
  lookup(100, 9001, 123, true); lookup(101, 9002, 125, true);
  CHECK(session->rollback_plugin_catalog_view(771, ObTxSEQ(20, 0)) == OB_SUCCESS);
  lookup(100, 9001, 123, true); lookup(101, 9002, 125, false);
  // Rolling back before first use retains this session's empty paired owner;
  // subsequent statements cannot silently substitute a different view.
  CHECK(session->rollback_plugin_catalog_view(771, ObTxSEQ(2, 0)) == OB_SUCCESS);
  lookup(100, 9001, 123, false);
  CHECK(borrowed->get_routine_id() == 9001 && borrowed->get_owner_id() == 123);
  CHECK(prepare(30) == OB_SUCCESS && schema == original_schema && journal == original_journal);
  routine.set_routine_id(9003);
  CHECK(privileges->record_create(routine, true) == OB_SUCCESS && schema->stage(routine) == OB_SUCCESS);
  SessionCatalogTestAccess::active(tx, 772);
  CHECK(session->bind_plugin_catalog_view(next) == OB_TRANS_INVALID_STATE);
  CHECK(prepare(40) == OB_TRANS_INVALID_STATE && !schema && !privileges && !journal);
  SessionCatalogTestAccess::active(tx, 771, 1001);
  CHECK(session->bind_plugin_catalog_view(next) == OB_TRANS_INVALID_STATE);
  SessionCatalogTestAccess::active(tx);
  CHECK(session->bind_plugin_catalog_view(next) == OB_SUCCESS);
  CHECK(prepare(40) == OB_SUCCESS);
  CHECK(session->prepare_plugin_catalog_commit() == OB_SUCCESS); // View-only, no SQL/MDS.
  CHECK(session->bind_plugin_catalog_view(next) == OB_SUCCESS); // Frozen view remains readable.
  CHECK(prepare(50) == OB_STATE_NOT_MATCH && !schema && !privileges && !journal);
  SessionCatalogTestAccess::committed(tx);
  CHECK(session->bind_plugin_catalog_view(next) == OB_TRANS_INVALID_STATE);
  CHECK(session->complete_plugin_catalog_transaction(771, OB_SUCCESS, false) == OB_SUCCESS);
  CHECK(!session->has_plugin_catalog_transaction());
  CHECK(original_schema->is_retired() && original_privileges->is_retired());
  CHECK(first.has_retired_routine_overlay() && next.has_retired_routine_overlay());
  CHECK(session->get_cached_schema_guard_info().get_schema_guard().has_retired_routine_overlay());
  CHECK(session->bind_plugin_catalog_view(next) == OB_STATE_NOT_MATCH);
  bool retired_handled = true; const ObRoutineInfo *retired_found = borrowed;
  CHECK(next.get_routine_info(9003, retired_found) == OB_STATE_NOT_MATCH && !retired_found);
  CHECK(original_schema->lookup(9003, retired_handled, retired_found) == OB_STATE_NOT_MATCH);
  CHECK(!retired_handled && !retired_found);
  retired_handled = true; retired_found = borrowed;
  CHECK(original_schema->lookup(101, OB_INVALID_ID, routine.get_routine_name(), 0,
      ROUTINE_FUNCTION_TYPE, retired_handled, retired_found) == OB_STATE_NOT_MATCH);
  CHECK(!retired_handled && !retired_found);
  ObPrivSet retired_grants = OB_PRIV_EXECUTE;
  CHECK(original_privileges->lookup(101, routine.get_routine_name(), ROUTINE_FUNCTION_TYPE, 125,
      true, borrowed, retired_handled, retired_grants) == OB_STATE_NOT_MATCH);
  CHECK(!retired_handled && retired_grants == 0);
  CHECK(original_schema->stage(routine) == OB_STATE_NOT_MATCH);
  CHECK(original_schema->erase(101, routine.get_routine_name(), ROUTINE_FUNCTION_TYPE, 9003) == OB_STATE_NOT_MATCH);
  CHECK(original_privileges->record_create(routine, true) == OB_STATE_NOT_MATCH);
  CHECK(original_privileges->record_drop(routine) == OB_STATE_NOT_MATCH);
  CHECK(borrowed->get_routine_id() == 9001 && borrowed->get_owner_id() == 123);
  {
    std::shared_ptr<const RoutineSchemaOverlay> captured = original_schema;
    CHECK(next.capture_routine_overlay(captured) == OB_STATE_NOT_MATCH && !captured);
    ObSqlCtx context; context.session_info_ = session.get(); context.schema_guard_ = &next;
    ObResultSet result(*session, arena, access);
    CHECK(engine->init_result_set(context, result) == OB_STATE_NOT_MATCH);
    CHECK(!result.get_exec_context().get_physical_plan_ctx());
  }
  ObSchemaGetterGuard after;
  CHECK(MockSchemaService::bind(after, *service, *manager) == OB_SUCCESS);
  CHECK(after.attach_routine_overlay(original_schema) == OB_STATE_NOT_MATCH);
  CHECK(after.inherit_routine_overlay(next) == OB_STATE_NOT_MATCH);
  CHECK(session->bind_plugin_catalog_view(after) == OB_SUCCESS && !after.has_routine_overlay());
  // Fresh transactions get fresh owners; reset and unknown outcomes cannot
  // attach an old transaction's schema to a new statement.
  for (int outcome = 0; outcome < 3; ++outcome) {
    SessionCatalogTestAccess::active(tx, 800 + outcome);
    CHECK(session->record_plugin_catalog_view(ObTxSEQ(10, 0), original_schema, original_privileges) == OB_STATE_NOT_MATCH);
    CHECK(!session->has_plugin_catalog_transaction());
    CHECK(prepare(10) == OB_SUCCESS && schema != original_schema && journal != original_journal);
    routine.set_routine_id(9100 + outcome);
    CHECK(privileges->record_create(routine, true) == OB_SUCCESS && schema->stage(routine) == OB_SUCCESS);
    auto view = schema;
    // A host journal lease can delay private undo, but not view retirement.
    auto held_journal = outcome == 2 ? journal : std::shared_ptr<RoutineCatalogTransaction>{};
    bool live_handled = false; const ObRoutineInfo *live_borrowed = nullptr;
    CHECK(view->lookup(routine.get_routine_id(), live_handled, live_borrowed) == OB_SUCCESS);
    CHECK(live_handled && live_borrowed);
    journal.reset(); schema.reset(); privileges.reset();
    if (outcome == 0) session->reset_tx_variable(false);
    else CHECK(session->complete_plugin_catalog_transaction(800 + outcome,
        outcome == 1 ? OB_TRANS_ROLLBACKED : OB_TIMEOUT, false) == OB_SUCCESS);
    CHECK(!session->has_plugin_catalog_transaction());
    bool handled = true; const ObRoutineInfo *found = &routine;
    CHECK(view->is_retired() && view->privileges()->is_retired());
    CHECK(view->lookup(routine.get_routine_id(), handled, found) == OB_STATE_NOT_MATCH && !handled && !found);
    held_journal.reset(); // Pending Rust marks can still undo the retired indices.
    CHECK(view->lookup(routine.get_routine_id(), handled, found) == OB_STATE_NOT_MATCH && !handled && !found);
    CHECK(live_borrowed->get_routine_id() == routine.get_routine_id());
  }
  {
    auto grants = std::make_shared<RoutinePrivilegeOverlay>();
    auto view = std::make_shared<RoutineSchemaOverlay>(grants);
    RoutineCatalogSavepoint before(view, grants);
    CHECK(before.valid());
    CHECK(grants->record_create(routine, true) == OB_SUCCESS && view->stage(routine) == OB_SUCCESS);
    grants->retire(); // Retirement of either half prevents schema access too.
    CHECK(view->is_retired());
    RoutineCatalogSavepoint too_late(view, grants);
    CHECK(!too_late.valid());
    view->retire(); view->retire();
    CHECK(before.rollback() == OB_SUCCESS);
    CHECK(view->is_retired() && grants->is_retired());
    CHECK(view->stage(routine) == OB_STATE_NOT_MATCH);
  }
  {
    // A retained, already-finished Rust participant is not authority to attach
    // its view merely because the underlying descriptor is still active.
    SessionCatalogTestAccess::active(tx, 900);
    CHECK(prepare(10) == OB_SUCCESS);
    CHECK(journal->finish(900, false) == OB_SUCCESS);
    CHECK(session->bind_plugin_catalog_view(after) == OB_STATE_NOT_MATCH && !after.has_routine_overlay());
    CHECK(prepare(20) == OB_STATE_NOT_MATCH && !schema && !privileges && !journal);
    session->discard_plugin_catalog_transaction();
  }
  {
    auto deserialized = std::make_unique<ObSQLSessionInfo>();
    Binding routed(*deserialized, tx);
    deserialized->set_is_deserialized();
    CHECK(deserialized->prepare_plugin_catalog_view(ObTxSEQ(10, 0), schema, privileges, journal) == OB_TRANS_INVALID_STATE);
    CHECK(!schema && !privileges && !journal);
  }
  {
    SessionCatalogTestAccess::active(tx, 1000);
    CHECK(prepare(10) == OB_SUCCESS);
    CHECK(session->fail_plugin_catalog_transaction(1001, 1000, OB_TIMEOUT) == OB_TRANS_INVALID_STATE);
    CHECK(session->fail_plugin_catalog_transaction(1000, 1001, OB_TIMEOUT) == OB_TRANS_INVALID_STATE);
    CHECK(session->fail_plugin_catalog_transaction(1000, 1000, OB_SUCCESS) == OB_INVALID_ARGUMENT);
    CHECK(!schema->is_retired() && !privileges->is_retired());
    CHECK(session->fail_plugin_catalog_transaction(1000, 1000, OB_TIMEOUT) == OB_SUCCESS);
    CHECK(schema->is_retired() && privileges->is_retired() && session->has_plugin_catalog_transaction());
    CHECK(session->fail_plugin_catalog_transaction(1000, 1000, OB_ERR_UNEXPECTED) == OB_SUCCESS);
    CHECK(session->prepare_plugin_catalog_commit() == OB_TIMEOUT);
    CHECK(session->bind_plugin_catalog_view(after) == OB_TIMEOUT && !after.has_routine_overlay());
    CHECK(prepare(20) == OB_STATE_NOT_MATCH && !schema && !privileges && !journal);
    session->discard_plugin_catalog_transaction();
    SessionCatalogTestAccess::active(tx, 1001);
    CHECK(prepare(10) == OB_SUCCESS);
    CHECK(session->fail_plugin_catalog_transaction(1000, 1000, OB_TIMEOUT) == OB_TRANS_INVALID_STATE);
    CHECK(!schema->is_retired() && !privileges->is_retired());
    // Revoking an old participant does not mutate a reused data descriptor.
    SessionCatalogTestAccess::active(tx, 1002);
    CHECK(session->fail_plugin_catalog_transaction(1001, 1000, OB_TIMEOUT) == OB_SUCCESS);
    CHECK(schema->is_retired() && privileges->is_retired());
    CHECK(oceanbase::data_plane::tx_desc_id(&tx).get_id() == 1002);
    session->discard_plugin_catalog_transaction();
  }
}
}
#endif
