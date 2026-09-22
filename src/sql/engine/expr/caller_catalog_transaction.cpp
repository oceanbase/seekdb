/* Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0. */
#include "sql/engine/expr/caller_catalog_transaction.h"
#include "share/schema/borrowed_sql_transaction.h"
#include "share/schema/routine_catalog_transaction.h"
#include "share/schema/routine_schema_overlay.h"
#include "share/schema/ob_multi_version_schema_service.h"
#include "share/ob_server_struct.h"
#include "data_plane/transaction/ob_i_transaction_service.h"
#include "data_plane/transaction/ob_tx_control.h"
#include "rootserver/catalog_commit_preparation.h"
#include "rootserver/pl_ddl/routine_cache_invalidation.h"
#include "common/ob_timeout_ctx.h"
#include "lib/worker.h"
#include "query/session/ob_inner_sql_connection_access.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/engine/ob_physical_plan.h"
#include "sql/engine/ob_physical_plan_ctx.h"
#include "sql/parser/ob_parser.h"
#include "sql/ob_sql_context.h"
#include "sql/ob_sql_trans_control.h"
#include "sql/session/ob_inner_sql_connection.h"
#include <cstring>
#include <thread>
#include <algorithm>

namespace oceanbase { namespace sql {
using namespace common;
using namespace share::schema;

struct CallerCatalogTransaction::Impl final : ObISQLClient, ICallerTransactionGuard,
    ICatalogOperationRecorder, rootserver::IRoutineCacheInvalidation
{
  Impl(ObExecContext &context, const transaction::ObTxSEQ &barrier)
      : context_(&context), session_(context.get_my_session()),
      thread_(std::this_thread::get_id()), barrier_(barrier), adapter_(*this, *this, this) {}
  Impl(ObSQLSessionInfo &session, RoutineCatalogTransaction &journal, int64_t transaction_id,
      int64_t sequence_base, int64_t absolute_deadline)
      : context_(session.get_cur_exec_ctx()), session_(&session),
      thread_(std::this_thread::get_id()), barrier_(transaction::ObTxSEQ::INVL()),
      transaction_id_(transaction_id), seq_base_(sequence_base), journal_(&journal),
      absolute_deadline_(absolute_deadline), adapter_(*this, *this, this) {}
  int check_schema_operation() const override {
    const int ret = check();
    if (ret != OB_SUCCESS) return ret;
    if (journal_) return OB_SUCCESS; // check() verified session ownership and Preparing.
    if (!ddl_journal_)
      return OB_STATE_NOT_MATCH;
    return OB_SUCCESS;
  }
  int finish_schema_operation(int64_t version, int sql_result) override {
    if (sql_result != OB_SUCCESS) return sql_result;
    const int ret = check_schema_operation();
    if (ret != OB_SUCCESS) return ret;
    return journal_ ? journal_->record_end_sign(transaction_id_, version)
                    : session_->record_plugin_catalog_schema_version(barrier_, version);
  }
  int on_drop(uint64_t routine, uint64_t database) override {
    const int ret = check_schema_operation();
    if (ret != OB_SUCCESS) return ret;
    if (journal_ || !ddl_journal_ || database != database_) return OB_STATE_NOT_MATCH;
    return ddl_journal_->record_invalidation(transaction_id_, barrier_, database, routine);
  }
  int check() const override {
    if (thread_ != std::this_thread::get_id()) return OB_STATE_NOT_MATCH;
    if (!session_ || (context_ && context_->get_my_session() != session_) ||
        session_->get_cur_exec_ctx() != context_)
      return OB_STATE_NOT_MATCH;
    if (session_->get_is_deserialized() || !data_plane::tx_desc_is_statement_ready(session_->get_tx_desc()) ||
        data_plane::tx_desc_is_committing(session_->get_tx_desc()) ||
        data_plane::tx_desc_id(session_->get_tx_desc()).get_id() != transaction_id_ ||
        data_plane::tx_desc_seq_base(session_->get_tx_desc()) != seq_base_ ||
        session_->get_database_id() != database_ || session_->get_priv_user_id() != principal_)
      return OB_TRANS_INVALID_STATE;
    if (data_plane::tx_desc_is_read_only(session_->get_tx_desc())) return OB_ERR_READ_ONLY_TRANSACTION;
    if (!journal_) {
      if (!context_) return OB_STATE_NOT_MATCH;
      const int ret = context_->check_status();
      if (ret != OB_SUCCESS || !ddl_journal_) return ret;
      if (!session_->owns_plugin_catalog_transaction(ddl_journal_, transaction_id_, seq_base_))
        return OB_TRANS_INVALID_STATE;
      return ddl_journal_->check_ddl_write(transaction_id_, barrier_);
    }
    // The session can discard its journal on transaction reset. Validate its
    // ownership BEFORE dereferencing the borrowed pointer on every operation.
    if (!session_->owns_plugin_catalog_transaction(journal_, transaction_id_, seq_base_))
      return OB_TRANS_INVALID_STATE;
    int ret = journal_->check_preparing(transaction_id_);
    if (ret != OB_SUCCESS) return ret;
    if (OB_SUCCESS != (ret = session_->check_session_status())) return ret;
    if (ObTimeUtility::current_time() >= absolute_deadline_ || THIS_WORKER.is_timeout()) return OB_TIMEOUT;
    return OB_SUCCESS;
  }
  int init() {
    if (context_ && context_->get_nested_level() >= 64) return OB_SIZE_OVERFLOW;
    if (!session_ || session_->get_cur_exec_ctx() != context_)
      return OB_STATE_NOT_MATCH;
    if (session_->get_nested_count() >= 64) return OB_SIZE_OVERFLOW;
    if (journal_) {
      if (transaction_id_ <= 0 || seq_base_ < 0 || absolute_deadline_ <= 0) return OB_INVALID_ARGUMENT;
    } else {
      if (!context_ || !context_->get_physical_plan_ctx() || !context_->get_physical_plan_ctx()->get_phy_plan())
        return OB_STATE_NOT_MATCH;
      transaction_id_ = data_plane::tx_desc_id(session_->get_tx_desc()).get_id();
      if (transaction_id_ <= 0) return OB_TRANS_INVALID_STATE;
      seq_base_ = data_plane::tx_desc_seq_base(session_->get_tx_desc());
    }
    database_ = session_->get_database_id(); principal_ = session_->get_priv_user_id();
    int ret = check();
    if (ret != OB_SUCCESS) return ret;
    if (journal_) {
      // Do not extend an enclosing timeout. External inner SQL already restores
      // worker/session timeout values around each execution; this TLS frame
      // bounds every preparation SQL and lives until results/connection close.
      deadline_ = std::make_unique<ObTimeoutCtx>();
      absolute_deadline_ = std::min(absolute_deadline_, deadline_->get_abs_timeout(absolute_deadline_));
      ret = deadline_->set_abs_timeout(absolute_deadline_);
      if (ret != OB_SUCCESS) return ret;
      if ((ret = check()) != OB_SUCCESS) return ret;
    }
    ret = query::ObInnerSQLConnectionAccess::create_spi_connection_with_external_session(session_, connection_);
    if (ret != OB_SUCCESS) return ret;
    inner_ = as_inner_sql_connection(connection_.get_ptr());
    if (!inner_) return OB_ERR_UNEXPECTED;
    // This surface accepts generated catalog DML only, after object-level host
    // ACL admission; ordinary plugin SQL continues using check_priv=true SPI.
    connection_->set_check_priv(false);
    trans_type_ = session_->get_trans_type();
    if (journal_ && !session_->has_start_stmt()) {
      ret = session_->set_start_stmt();
      if (ret != OB_SUCCESS) return ret;
      owns_statement_ = true; // Bookkeeping only, never starts a data transaction.
    }
    ret = inner_->begin_nested_session(saved_session_, saved_connection_, false);
    nested_ = ret == OB_SUCCESS;
    if (ret == OB_SUCCESS) ret = inner_->enable_plugin_catalog_sql();
    return ret;
  }
  int restore(int &cleanup_result) {
    cleanup_result = OB_SUCCESS;
    int ret = adapter_.status();
    if (nested_) {
      const int cleanup = inner_->end_nested_session(saved_session_, saved_connection_);
      session_->set_trans_type(trans_type_);
      nested_ = false;
      cleanup_result = cleanup;
      if (ret == OB_SUCCESS) ret = cleanup;
    }
    connection_.reset(); inner_ = nullptr;
    if (owns_statement_) {
      const int cleanup = session_->set_end_stmt();
      owns_statement_ = false;
      if (cleanup_result == OB_SUCCESS) cleanup_result = cleanup;
      if (ret == OB_SUCCESS) ret = cleanup;
    }
    deadline_.reset();
    return ret;
  }
  // Parse before execution: no DDL, session mutation, procedure call or explicit
  // transaction control may reach the inner SQL engine through this adapter.
  int validate_sql(const char *text, bool write) {
    return CallerCatalogTransaction::validate_statement(*session_, text, write);
  }
  int read(ReadResult &result, const char *text, int32_t) override {
    const int ret = validate_sql(text, false);
    if (ret != OB_SUCCESS) return ret;
    session_->set_trans_type(trans_type_);
    return connection_->execute_read(ObString::make_string(text), result, false);
  }
  int write(const char *text, int32_t, int64_t &rows) override {
    if (!journal_ && !ddl_journal_) return OB_STATE_NOT_MATCH;
    const int ret = validate_sql(text, true);
    if (ret != OB_SUCCESS) return ret;
    session_->set_trans_type(trans_type_);
    return connection_->execute_write(ObString::make_string(text), rows, false);
  }
  int escape(const char *, int64_t, char *, int64_t, int64_t &size) override {
    size = 0;
    // Catalog SQL splicers encode their literals themselves. Do not invent a
    // second session-independent escaping contract for arbitrary input here.
    return OB_NOT_SUPPORTED;
  }
  sqlclient::ObISQLConnection *get_connection() override { return connection_.get_ptr(); }
  int acquire_connection(sqlclient::ObISQLConnectionGuard &output, int32_t) override {
    output.reset(); return OB_NOT_SUPPORTED;
  }
  ObExecContext *context_;
  ObSQLSessionInfo *session_;
  const std::thread::id thread_;
  const transaction::ObTxSEQ barrier_;
  int64_t transaction_id_ = 0, seq_base_ = 0;
  RoutineCatalogTransaction *journal_ = nullptr;
  RoutineCatalogTransaction *ddl_journal_ = nullptr;
  int64_t absolute_deadline_ = 0;
  std::unique_ptr<ObTimeoutCtx> deadline_;
  uint64_t database_ = OB_INVALID_ID, principal_ = OB_INVALID_ID;
  transaction::ObTxClass trans_type_ = transaction::ObTxClass::USER;
  sqlclient::ObISQLConnectionGuard connection_;
  ObIInnerSQLConnection *inner_ = nullptr;
  ObSQLSessionInfo::StmtSavedValue saved_session_;
  ObIInnerSQLConnection::SavedValue saved_connection_;
  bool nested_ = false;
  bool owns_statement_ = false;
  BorrowedSQLTransaction adapter_;
};

CallerCatalogTransaction::CallerCatalogTransaction() = default;
CallerCatalogTransaction::~CallerCatalogTransaction() { (void)close(); }
int CallerCatalogTransaction::validate_statement(ObSQLSessionInfo &session, const char *text, bool write)
{
  if (!text) return OB_INVALID_ARGUMENT;
  constexpr size_t limit = 16 * 1024 * 1024;
  const size_t length = strnlen(text, limit + 1);
  if (!length || length > limit) return OB_SIZE_OVERFLOW;
  ObArenaAllocator arena;
  ObParser parser(arena, session.get_sql_mode(), session.get_charsets4parser());
  ObSEArray<ObString, 1> statements;
  ObMPParseStat split;
  int ret = parser.split_multiple_stmt(ObString(static_cast<int32_t>(length), text), statements, split, false, true);
  if (ret != OB_SUCCESS) return ret;
  if (split.parse_fail_) return split.fail_ret_ == OB_SUCCESS ? OB_ERR_PARSE_SQL : split.fail_ret_;
  if (statements.count() != 1) return OB_NOT_SUPPORTED;
  ParseResult parsed{};
  ret = parser.parse(statements.at(0), parsed);
  if (ret != OB_SUCCESS) return ret;
  const auto *tree = parsed.result_tree_;
  if (!tree || tree->num_child_ != 1 || !tree->children_ || !tree->children_[0]) return OB_NOT_SUPPORTED;
  const auto type = tree->children_[0]->type_;
  if (!write) return type == T_SELECT ? OB_SUCCESS : OB_NOT_SUPPORTED;
  return type == T_INSERT || type == T_UPDATE || type == T_DELETE ? OB_SUCCESS : OB_NOT_SUPPORTED;
}
int CallerCatalogTransaction::open(ObExecContext &context)
{ return open_impl(context, transaction::ObTxSEQ::INVL()); }
int CallerCatalogTransaction::open(ObExecContext &context, const transaction::ObTxSEQ &barrier)
{ return open_impl(context, barrier); }
int CallerCatalogTransaction::open_impl(ObExecContext &context, const transaction::ObTxSEQ &barrier)
{
  if (used_) return OB_INIT_TWICE;
  used_ = true;
  try {
    impl_ = std::make_unique<Impl>(context, barrier);
    result_ = impl_->init();
  } catch (const std::bad_alloc &) { result_ = OB_ALLOCATE_MEMORY_FAILED; }
  catch (...) { result_ = OB_ERR_UNEXPECTED; }
  if (result_ != OB_SUCCESS) (void)close();
  return result_;
}
int CallerCatalogTransaction::open_for_commit(ObSQLSessionInfo &session,
    RoutineCatalogTransaction &journal, int64_t transaction_id, int64_t sequence_base,
    int64_t absolute_deadline)
{
  if (used_) return OB_INIT_TWICE;
  used_ = true;
  try {
    impl_ = std::make_unique<Impl>(session, journal, transaction_id, sequence_base, absolute_deadline);
    result_ = impl_->init();
  } catch (const std::bad_alloc &) { result_ = OB_ALLOCATE_MEMORY_FAILED; }
  catch (...) { result_ = OB_ERR_UNEXPECTED; }
  if (result_ != OB_SUCCESS) (void)close();
  return result_;
}
int CallerCatalogTransaction::admit_ddl(RoutineCatalogTransaction &journal,
    ObMultiVersionSchemaService &service, ObISQLClient &fresh_reader,
    int64_t refreshed_schema_version, int64_t absolute_deadline)
{
  if (!impl_ || result_ != OB_SUCCESS) return result_ == OB_SUCCESS ? OB_NOT_INIT : result_;
  if (impl_->journal_ || impl_->ddl_journal_) return OB_INIT_TWICE;
  int ret = impl_->adapter_.status();
  if (ret == OB_SUCCESS && (refreshed_schema_version <= 0 || absolute_deadline <= 0)) ret = OB_INVALID_ARGUMENT;
  if (ret == OB_SUCCESS && ObTimeUtility::current_time() >= absolute_deadline) ret = OB_TIMEOUT;
  if (ret == OB_SUCCESS && (!impl_->barrier_.is_valid() || impl_->barrier_.get_branch() != 0))
    ret = OB_INVALID_ARGUMENT;
  if (ret == OB_SUCCESS && !impl_->session_->owns_plugin_catalog_transaction(&journal, impl_->transaction_id_, impl_->seq_base_))
    ret = OB_TRANS_INVALID_STATE;
  uint64_t epoch = 0, sequence = 0;
  if (ret == OB_SUCCESS) ret = journal.ddl_admission(impl_->transaction_id_, epoch, sequence);
  if (ret == OB_SUCCESS && epoch == 0) {
    rootserver::CatalogDDLAdmission admission(service, impl_->adapter_, fresh_reader);
    int64_t captured = 0;
    ret = admission.acquire(refreshed_schema_version, absolute_deadline, captured);
    if (ret == OB_SUCCESS) ret = journal.admit_ddl(impl_->transaction_id_, impl_->barrier_, captured);
  }
  if (ret == OB_SUCCESS) ret = journal.check_ddl_write(impl_->transaction_id_, impl_->barrier_);
  if (ret == OB_SUCCESS) impl_->ddl_journal_ = &journal;
  else result_ = ret;
  return ret;
}
ObMySQLTransaction *CallerCatalogTransaction::transaction()
{ return impl_ && result_ == OB_SUCCESS ? &impl_->adapter_ : nullptr; }
int CallerCatalogTransaction::status() const
{ return result_ != OB_SUCCESS ? result_ : impl_ ? impl_->adapter_.status() : OB_NOT_INIT; }
rootserver::IRoutineCacheInvalidation *CallerCatalogTransaction::invalidation_sink()
{
  return impl_ && result_ == OB_SUCCESS && !impl_->journal_ && impl_->ddl_journal_ &&
      impl_->check_schema_operation() == OB_SUCCESS ? impl_.get() : nullptr;
}
int CallerCatalogTransaction::close()
{
  int cleanup = OB_SUCCESS;
  return close(cleanup);
}
int CallerCatalogTransaction::close(int &cleanup_result)
{
  if (impl_) {
    int restore_result = OB_SUCCESS;
    int cleanup = OB_SUCCESS;
    try { cleanup = impl_->restore(restore_result); }
    catch (const std::bad_alloc &) { cleanup = restore_result = OB_ALLOCATE_MEMORY_FAILED; }
    catch (...) { cleanup = restore_result = OB_ERR_UNEXPECTED; }
    if (cleanup_result_ == OB_SUCCESS) cleanup_result_ = restore_result;
    if (result_ == OB_SUCCESS) result_ = cleanup;
    impl_.reset();
  }
  cleanup_result = cleanup_result_;
  return result_;
}

namespace {
// Owns references to the admitted journal/views across inner SQL and failed
// session restoration. Those references preserve storage, never TX authority.
class CallerCatalogOperation final : public ICatalogOperationHost
{
public:
  CallerCatalogOperation(ObExecContext &context, ICallerCatalogMutation &mutation)
      : context_(context), mutation_(mutation), session_(context.get_my_session()),
        sql_(context.get_sql_ctx()), guard_(sql_ ? sql_->schema_guard_ : nullptr),
        thread_(std::this_thread::get_id()),
        database_(session_ ? session_->get_database_id() : OB_INVALID_ID),
        principal_(session_ ? session_->get_priv_user_id() : OB_INVALID_ID) {}
  int preflight() override {
    int ret = check_context();
    if (ret != OB_SUCCESS) return ret;
    if (context_.get_physical_plan_ctx() == nullptr ||
        context_.get_physical_plan_ctx()->get_phy_plan() == nullptr ||
        GCTX.schema_service_ == nullptr || context_.get_sql_proxy() == nullptr) return OB_NOT_INIT;
    if (context_.get_das_ctx().get_write_branch_id() != 0) return OB_NOT_SUPPORTED;
    if (!valid_id(database_) || !valid_id(principal_)) return OB_INVALID_ARGUMENT;
    if (session_->get_tx_read_only() || data_plane::tx_desc_is_read_only(session_->get_tx_desc()))
      return OB_ERR_READ_ONLY_TRANSACTION;
    if (data_plane::tx_desc_is_statement_ready(session_->get_tx_desc())) {
      transaction_id_ = data_plane::tx_desc_id(session_->get_tx_desc()).get_id();
      sequence_base_ = data_plane::tx_desc_seq_base(session_->get_tx_desc());
      if (transaction_id_ <= 0 || sequence_base_ <= 0) return OB_TRANS_INVALID_STATE;
    }
    if ((ret = context_.check_status()) != OB_SUCCESS) return ret;
    if ((ret = session_->bind_plugin_catalog_view(*guard_)) != OB_SUCCESS) return ret;
    const int64_t observed_id = data_plane::tx_desc_id(session_->get_tx_desc()).get_id();
    const int64_t observed_base = data_plane::tx_desc_seq_base(session_->get_tx_desc());
    ret = mutation_.preflight(context_); // No data effects or plugin callback.
    if (ret == OB_SUCCESS &&
        (data_plane::tx_desc_id(session_->get_tx_desc()).get_id() != observed_id ||
         data_plane::tx_desc_seq_base(session_->get_tx_desc()) != observed_base)) return OB_TRANS_INVALID_STATE;
    return ret == OB_SUCCESS ? check_transaction() : ret;
  }
  int prepare() override {
    int ret = check_transaction();
    if (ret != OB_SUCCESS) return ret;
    // This can lazily assign the caller's ordinary SELECT transaction identity
    // and savepoint while leaving it IDLE until the first storage write. Borrow
    // that prepared identity too; do not force BEGIN or create another TX.
    ret = ObSqlTransControl::prepare_plugin_sql(context_);
    const int64_t id = data_plane::tx_desc_id(session_->get_tx_desc()).get_id();
    const int64_t base = data_plane::tx_desc_seq_base(session_->get_tx_desc());
    if (transaction_id_ == 0 && data_plane::tx_desc_is_statement_ready(session_->get_tx_desc())) {
      transaction_id_ = id; sequence_base_ = base;
    }
    if (ret != OB_SUCCESS) return ret;
    if (transaction_id_ <= 0 || sequence_base_ <= 0 ||
        (ret = check_transaction()) != OB_SUCCESS) return OB_TRANS_INVALID_STATE;
    deadline_ = context_.get_physical_plan_ctx()->get_timeout_timestamp();
    if (deadline_ <= 0) return OB_INVALID_ARGUMENT;
    if ((ret = ObSqlTransControl::create_anonymous_savepoint(context_, barrier_)) != OB_SUCCESS) return ret;
    barrier_ready_ = true;
    if (!barrier_.is_valid() || barrier_.get_branch() != 0) return OB_STATE_NOT_MATCH;
    if ((ret = session_->prepare_plugin_catalog_view(barrier_, schema_, privileges_, journal_)) != OB_SUCCESS) return ret;
    if ((ret = session_->bind_plugin_catalog_view(*guard_)) != OB_SUCCESS) return ret;
    return transport_.open(context_, barrier_);
  }
  int apply() override {
    int ret = check_transaction();
    if (ret != OB_SUCCESS) return ret;
    if (!journal_ || !schema_ || !privileges_ || !barrier_ready_) return OB_STATE_NOT_MATCH;
    if ((ret = context_.check_status()) != OB_SUCCESS) return ret;
    int64_t version = 0;
    if ((ret = guard_->get_schema_version(version)) != OB_SUCCESS) return ret;
    if ((ret = transport_.admit_ddl(*journal_, *GCTX.schema_service_, *context_.get_sql_proxy(),
        version, deadline_)) != OB_SUCCESS) return ret;
    auto *transaction = transport_.transaction();
    auto *invalidation = transport_.invalidation_sink();
    if (!transaction || !invalidation) return OB_STATE_NOT_MATCH;
    ret = mutation_.apply(context_, *transaction, *schema_, *privileges_, *invalidation);
    // Ignoring a failed catalog SQL in an action cannot turn it into success.
    if (ret == OB_SUCCESS) ret = transport_.status();
    return ret == OB_SUCCESS ? check_transaction() : ret;
  }
  int close() override {
    int cleanup = OB_SUCCESS;
    (void)transport_.close(cleanup);
    return cleanup; // The original operation error is recorded separately.
  }
  int check_transaction() override {
    const int ret = check_context();
    if (ret != OB_SUCCESS) return ret;
    if (transaction_id_ > 0 &&
        (!data_plane::tx_desc_is_statement_ready(session_->get_tx_desc()) ||
         data_plane::tx_desc_id(session_->get_tx_desc()).get_id() != transaction_id_ ||
         data_plane::tx_desc_seq_base(session_->get_tx_desc()) != sequence_base_))
      return OB_TRANS_INVALID_STATE;
    if (journal_ && !session_->owns_plugin_catalog_transaction(journal_.get(), transaction_id_, sequence_base_))
      return OB_TRANS_INVALID_STATE;
    return OB_SUCCESS;
  }
  int rollback_data() override {
    const int ret = check_transaction();
    if (ret != OB_SUCCESS || !barrier_ready_) return ret;
    auto *service = data_plane::query_transaction_service();
    if (!service) return OB_NOT_INIT;
    // Deliberately do not call the combined SQL rollback wrapper: Rust must
    // observe confirmed data rollback BEFORE deciding to invoke private undo.
    return service->rollback_to_implicit_savepoint(*session_->get_tx_desc(), barrier_, deadline_, false);
  }
  int rollback_view() override {
    const int ret = check_transaction();
    if (ret != OB_SUCCESS || !journal_) return ret;
    return journal_->rollback(transaction_id_, barrier_);
  }
  int poison(int cause) override {
    int ret = OB_SUCCESS;
    // Revoke the captured owner even if nested SQL discarded it from session.
    if (journal_) {
      ret = journal_->fail(transaction_id_, cause);
      schema_->retire(); privileges_->retire();
    } else if (session_ && transaction_id_ > 0 && sequence_base_ > 0) {
      ret = session_->fail_plugin_catalog_transaction(transaction_id_, sequence_base_, cause);
    }
    // Do not require an unexpired query context for cleanup; do require the
    // exact data identity. Abort is best-effort, journal failure stays sticky.
    if (session_ && transaction_id_ > 0 &&
        data_plane::tx_desc_is_statement_ready(session_->get_tx_desc()) &&
        data_plane::tx_desc_id(session_->get_tx_desc()).get_id() == transaction_id_ &&
        data_plane::tx_desc_seq_base(session_->get_tx_desc()) == sequence_base_) {
      const int abort_ret = data_plane::abort_transaction_for_error(*session_->get_tx_desc(), cause);
      if (ret == OB_SUCCESS) ret = abort_ret;
    }
    return ret;
  }
private:
  static bool valid_id(uint64_t id) { return id > 0 && id <= static_cast<uint64_t>(INT64_MAX); }
  int check_context() const {
    if (thread_ != std::this_thread::get_id() || !session_ || !sql_ || !guard_ ||
        context_.get_my_session() != session_ || context_.get_sql_ctx() != sql_ ||
        sql_->session_info_ != session_ || sql_->schema_guard_ != guard_ ||
        session_->get_cur_exec_ctx() != &context_ || session_->get_is_deserialized() ||
        session_->get_database_id() != database_ || session_->get_priv_user_id() != principal_)
      return OB_STATE_NOT_MATCH;
    if (sql_->disable_privilege_check_ != PRIV_CHECK_FLAG_NORMAL) return OB_ERR_NO_PRIVILEGE;
    if (!guard_->is_inited() || guard_->has_retired_routine_overlay()) return OB_STATE_NOT_MATCH;
    return OB_SUCCESS;
  }
  ObExecContext &context_;
  ICallerCatalogMutation &mutation_;
  ObSQLSessionInfo *session_;
  ObSqlCtx *sql_;
  ObSchemaGetterGuard *guard_;
  const std::thread::id thread_;
  const uint64_t database_, principal_;
  int64_t transaction_id_ = 0, sequence_base_ = 0, deadline_ = 0;
  transaction::ObTxSEQ barrier_ = transaction::ObTxSEQ::INVL();
  bool barrier_ready_ = false;
  std::shared_ptr<RoutineSchemaOverlay> schema_;
  std::shared_ptr<RoutinePrivilegeOverlay> privileges_;
  std::shared_ptr<RoutineCatalogTransaction> journal_;
  CallerCatalogTransaction transport_;
};
}
int run_caller_catalog_operation(ObExecContext &context, ICallerCatalogMutation &mutation,
    CatalogOperationResult &result) noexcept
{
  CallerCatalogOperation host(context, mutation);
  return run_catalog_operation(host, result);
}
} }
