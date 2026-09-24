// Included inside oceanbase::observer::namespace_worker_prototype.
struct EngineWrite {
  ObArenaAllocator allocator{ObMemAttr("NsRemoteWrite")};
  ObSchemaGetterGuard guard;
  ObTableSchema logical_schema{&allocator};
  ObTableSchema routed_schema{&allocator};
  std::vector<std::unique_ptr<ObTableSchema>> logical_materialization_schemas;
  std::vector<std::unique_ptr<ObTableSchema>> routed_materialization_schemas;
  ObArray<const ObTableSchema *> materialization_schemas;
  ObDmlTablePlan plan{allocator};
  ObTimeZoneInfo timezone;
  ObDmlWriteSpec spec;
  ObTxReadSnapshot snapshot;
  concurrent_control::ObWriteFlag write_flag;
  ObWriteContext context;
  ObDmlExecution execution; // Released before context, plan and allocator.
  ObSEArray<uint64_t, 2> columns;
  std::vector<uint64_t> logical_tablets;
  StorageSpaceHandle storage_space;
  const ObTableSchema *schema = nullptr;

  int prepare(const WritePrepareRequest &request, ObTxDesc &tx) {
    const uint64_t table = request.table_id;
    if (request.spec.tz_info_ == nullptr) { return OB_INVALID_ARGUMENT; }
    int64_t request_bytes = 137; // Former opcode and seventeen number fields.
    auto add_bytes = [&](int64_t size) {
      if (size < 0 || size > static_cast<int64_t>(MAX_SQL_MESSAGE) - request_bytes) {
        return OB_SIZE_OVERFLOW;
      }
      request_bytes += size;
      return OB_SUCCESS;
    };
    int ret = add_bytes(request.logical_schema.get_serialize_size());
    for (int64_t i = 0; !ret && i < request.materialization_schemas.count(); ++i) {
      const ObTableSchema *schema = request.materialization_schemas.at(i);
      ret = schema ? add_bytes(schema->get_serialize_size()) : OB_INVALID_ARGUMENT;
    }
    if (ret || OB_FAIL(add_bytes(request.spec.tz_info_->get_serialize_size()))
        || OB_FAIL(add_bytes(request.snapshot.get_serialize_size()))
        || OB_FAIL(add_bytes(request.write_flag.get_serialize_size()))
        || OB_FAIL(add_bytes(request.columns.size() * 8))) { return ret; }
    storage_space = request.storage_space;
    spec = request.spec;
    ret = logical_schema.assign(request.logical_schema);
    if (OB_FAIL(ret)) { return ret; }
    const uint64_t ns = storage_space.namespace_id();
    const int64_t materialization_schema_count = request.materialization_schemas.count();
    if (materialization_schema_count > 3) {
      return OB_INVALID_ARGUMENT;
    }
    for (int64_t i = 0; i < materialization_schema_count; ++i) {
      auto logical = std::make_unique<ObTableSchema>(&allocator);
      if (OB_FAIL(logical->assign(*request.materialization_schemas.at(i)))) { return ret; }
      auto routed = std::make_unique<ObTableSchema>(&allocator);
      int schema_ret = storage_space.is_global()
          ? routed->assign(*logical)
          : NamespaceForkKernelPrototype::make_storage_schema(ns, *logical, *routed);
      if (schema_ret != OB_SUCCESS) { return schema_ret; }
      if (OB_FAIL(materialization_schemas.push_back(routed.get()))) { return ret; }
      logical_materialization_schemas.push_back(std::move(logical));
      routed_materialization_schemas.push_back(std::move(routed));
    }
    spec.timeout_ = std::min<int64_t>(spec.timeout_, THIS_WORKER.get_timeout_ts());
    if (OB_FAIL(timezone.assign(*request.spec.tz_info_))) { return ret; }
    spec.tz_info_ = &timezone;
    if (OB_FAIL(snapshot.assign(request.snapshot))) { return ret; }
    write_flag = request.write_flag;
    const int64_t count = request.columns.size();
    if (count == 0 || count > OB_MAX_COLUMN_NUMBER) {
      fprintf(stderr, "PROTOTYPE_V17_WRITE_PREPARE ns=%llu table=%llu columns=%llu stage=validate ret=%d\n",
          (unsigned long long)ns, (unsigned long long)table, (unsigned long long)count,
          ret);
      return OB_NOT_SUPPORTED;
    }
    if ((!is_inner_table(table) && spec.schema_version_ <= 0)
            || logical_schema.get_table_id() != table
            || logical_schema.get_schema_version() < 0
            || (spec.schema_version_ > 0
                && logical_schema.get_schema_version() != spec.schema_version_)) {
      ret = OB_INVALID_ARGUMENT;
    } else {
      ret = storage_space.is_global()
          ? routed_schema.assign(logical_schema)
          : NamespaceForkKernelPrototype::make_storage_schema(ns, logical_schema, routed_schema);
      if (!ret) { schema = &routed_schema; }
    }
    if (!ret) {
      // The request carries the exact schema pinned by the worker's SchemaGuard.
      // Asking storage to validate it against the shared process SchemaService
      // would reintroduce a second, stale namespace schema authority.
      spec.check_schema_version_ = false;
    }
    if (ret) {
      fprintf(stderr, "PROTOTYPE_V17_WRITE_PREPARE ns=%llu table=%llu stage=schema ret=%d\n",
          (unsigned long long)ns, (unsigned long long)table, ret);
      return ret;
    }
    if (!schema || schema->get_schema_version() != spec.schema_version_) {
      fprintf(stderr, "PROTOTYPE_V17_WRITE_PREPARE ns=%llu table=%llu stage=version requested=%lld actual=%lld ret=%d\n",
          (unsigned long long)ns, (unsigned long long)table, (long long)spec.schema_version_,
          (long long)(schema ? schema->get_schema_version() : OB_INVALID_VERSION), OB_SCHEMA_EAGAIN);
      return OB_SCHEMA_EAGAIN;
    }
    ObArray<ObTabletID> schema_tablets;
    if (OB_FAIL(schema->get_tablet_ids(schema_tablets)) || schema_tablets.empty()) {
      return ret ? ret : OB_INVALID_ARGUMENT;
    }
    logical_tablets.reserve(schema_tablets.count());
    for (int64_t i = 0; OB_SUCC(ret) && i < schema_tablets.count(); ++i) {
      uint64_t logical_tablet_id = schema_tablets.at(i).id();
      if (storage_space.is_namespace()
          && NamespaceForkKernelPrototype::is_encoded_id(logical_tablet_id)) {
        ret = NamespaceForkKernelPrototype::local_object_id(
            ns, logical_tablet_id, logical_tablet_id);
      }
      if (OB_SUCC(ret)) { logical_tablets.push_back(logical_tablet_id); }
    }
    if (OB_FAIL(ret)) { return ret; }
    for (int64_t i = 0; !ret && i < count; ++i) {
      const uint64_t id = request.columns[i];
      const auto *column = schema->get_column_schema(id);
      if (!column || has_exist_in_array(columns, id)) { ret = OB_INVALID_ARGUMENT; }
      else { ret = columns.push_back(id); }
    }
    if (ret) {
      fprintf(stderr, "PROTOTYPE_V17_WRITE_PREPARE ns=%llu table=%llu stage=columns ret=%d\n",
          (unsigned long long)ns, (unsigned long long)table, ret);
      return ret;
    }
    if (!ret) { ret = plan.build(schema, spec.schema_version_, columns); }
    if (!ret) { ret = acquire(tx); }
    if (ret) {
      fprintf(stderr, "PROTOTYPE_V17_WRITE_PREPARE ns=%llu table=%llu tablet=%llu stage=native ret=%d\n",
          (unsigned long long)ns, (unsigned long long)table,
          (unsigned long long)(logical_tablets.empty() ? 0 : logical_tablets.front()), ret);
    }
    return ret;
  }

  int acquire(ObTxDesc &tx) {
    int ret = OB_SUCCESS;
    if (context.is_valid()) { return ret; }
    if (!ret) { ret = share::server_service<ObIWriteContextService>()->acquire_write_context(
        spec.timeout_, tx, snapshot, spec.branch_id_, write_flag, context); }
    if (!ret) { ret = share::server_service<ObIDmlService>()->prepare_execution(
        spec, plan, snapshot, allocator, context, write_flag, execution); }
    return ret;
  }

  // Native store ctxs merge write state into the tx descriptor only when
  // released. Savepoint rollback decisions depend on that state, so the 'B'
  // handler releases all open contexts first; the next batch re-acquires.
  void release_context() {
    execution.reset();
    context.reset();
  }

  int batch(const WriteBatch &request, ObTxDesc &tx,
            int64_t &affected, WriteResult &returned) {
    const char operation = request.operation;
    const uint64_t ns = storage_space.namespace_id();
    const uint64_t tablet_id = request.tablet_id, count = request.rows;
    const bool update = operation == 'U';
    if (std::find(logical_tablets.begin(), logical_tablets.end(), tablet_id)
            == logical_tablets.end()
        || count == 0
        || count > (update ? 64 : 32) || (update && count % 2)
        || request.cells.size() != count * columns.count()
        || request.lob_headers.size() != request.cells.size()) { return OB_INVALID_ARGUMENT; }
    ObTabletID tablet(tablet_id);
    int ret = OB_SUCCESS;
    ret = route_tablet_id(storage_space, tablet);
    if (OB_SUCC(ret) && !materialization_schemas.empty()) {
      ret = NamespaceForkKernelPrototype::ensure_tablet(
          tablet, *schema, materialization_schemas);
    }
    if (OB_FAIL(ret)) { return ret; }
    if (OB_SUCC(ret)) { ret = acquire(tx); }
    if (OB_FAIL(ret)) { return ret; }
    const int64_t lock_timeout = request.lock_timeout;
    const ObRowLockMode lock_mode = request.lock_mode;
    if (operation == 'L' && lock_mode != ObRowLockMode::NONE
        && lock_mode != ObRowLockMode::WRITE) { return OB_INVALID_ARGUMENT; }
    ObSEArray<uint64_t, 2> updated_columns;
    const uint64_t updated_count = request.updated_columns.size();
    if (updated_count > uint64_t(columns.count())
        || (update || operation == 'f' ? updated_count == 0 : updated_count != 0)) { return OB_INVALID_ARGUMENT; }
    for (uint64_t i = 0; i < updated_count; ++i) {
      const uint64_t column = request.updated_columns[i];
      if (!has_exist_in_array(columns, column)
          || has_exist_in_array(updated_columns, column)) { return OB_INVALID_ARGUMENT; }
      int ret = updated_columns.push_back(column);
      if (ret) { return ret; }
    }
    const auto duplicate_mode = request.duplicate_mode;
    if (duplicate_mode != ObDuplicateReturnMode::ALL && duplicate_mode != ObDuplicateReturnMode::ONE) { return OB_INVALID_ARGUMENT; }
    class Rows final : public ObDatumRowIterator {
    public:
      const std::vector<ObObj> &cells;
      const std::vector<bool> &lob_headers;
      int64_t width;
      size_t position = 0;
      // UPDATE's old row must remain alive while storage obtains its new row.
      ObDatumRow rows[2];
      Rows(const std::vector<ObObj> &values, const std::vector<bool> &headers, int64_t columns)
          : cells(values), lob_headers(headers), width(columns) {}
      int get_next_row(ObDatumRow *&out) override {
        if (position == cells.size()) { return OB_ITER_END; }
        ObDatumRow &row = rows[(position / width) % 2];
        int ret = OB_SUCCESS;
        for (int64_t i = 0; !ret && i < width; ++i) {
          const size_t index = position++;
          ret = row.storage_datums_[i].from_obj_enhance(cells[index]);
          if (!ret && lob_headers[index]) { row.storage_datums_[i].set_has_lob_header(); }
        }
        row.row_flag_.set_flag(DF_INSERT); out = &row; return ret;
      }
    } rows(request.cells, request.lob_headers, columns.count());
    ret = rows.rows[0].init(columns.count());
    if (!ret) { ret = rows.rows[1].init(columns.count()); }
    auto *service = share::server_service<ObIDmlService>();
    if (!ret && operation == 'f') {
      ObDatumRowIterator *duplicates = nullptr;
      ret = service->insert_rows_fetch_duplicates(tablet, tx, execution, columns, updated_columns,
                                                 &rows, duplicate_mode, affected, duplicates);
      struct ReleaseDuplicates { ObIDmlService *service; ObDatumRowIterator *rows;
        ~ReleaseDuplicates() { if (rows) { service->free_duplicate_rows_iterator(rows); } }
      } release{service, duplicates};
      if (!ret || ret == OB_ERR_PRIMARY_KEY_DUPLICATE) {
        const int storage_ret = ret;
        int64_t duplicate_count = 0; ret = OB_SUCCESS;
        ObDatumRow *row = nullptr;
        while (duplicates && !ret && !(ret = duplicates->get_next_row(row))) {
          if (!row || row->get_column_count() != updated_columns.count()
              || duplicate_count >= 32) { ret = OB_SIZE_OVERFLOW; break; }
          for (int64_t i = 0; !ret && i < updated_columns.count(); ++i) {
            ObObj cell;
            const auto &descriptors = plan.get_col_descs();
            const ObColDesc *column = nullptr;
            for (int64_t j = 0; !column && j < descriptors.count(); ++j) {
              if (descriptors.at(j).col_id_ == updated_columns.at(i)) { column = &descriptors.at(j); }
            }
            ret = column ? row->storage_datums_[i].to_obj_enhance(cell, column->col_type_) : OB_INVALID_ARGUMENT;
            if (!ret) {
              ret = returned.append(cell, row->storage_datums_[i].has_lob_header());
            }
          }
          ++duplicate_count;
        }
        if (ret == OB_ITER_END) { ret = OB_SUCCESS; }
        if (!ret) {
          returned.rows = duplicate_count;
          ret = storage_ret;
        }
      }
    } else if (!ret && update) { ret = service->update_rows(tablet, tx, execution, columns, updated_columns, &rows, affected); }
    else if (!ret && operation == 'D') { ret = service->delete_rows(tablet, tx, execution, columns, &rows, affected); }
    else if (!ret && operation == 'L') { ret = service->lock_rows(tablet, tx, execution, lock_timeout, lock_mode, &rows, affected); }
    else if (!ret && operation == 'p') { ret = service->put_rows(tablet, tx, execution, columns, &rows, affected); }
    else if (!ret) { ret = service->insert_rows(tablet, tx, execution, columns, &rows, affected); }
    fprintf(stderr, "PROTOTYPE_V15_WRITE_BATCH op=%c tx=%lld rows=%llu affected=%lld ret=%d\n",
        operation, (long long)tx.get_tx_id().get_id(), (unsigned long long)(update ? count / 2 : count), (long long)affected,
        ret);
    return ret;
  }
};

struct EngineWrites {
  StorageSpaceHandle storage_space;
  sql::ObSQLSessionInfo &session;
  uint32_t sid;
  ObTxDesc *&tx;
  uint64_t sequence = 0;
  std::map<uint64_t, std::unique_ptr<EngineWrite>> writes;
  explicit EngineWrites(StorageSpaceHandle space, sql::ObSQLSessionInfo &s)
      : storage_space(space), session(s), sid(s.get_server_sid()), tx(s.get_tx_desc()) {}
  void reset() {
    session.reset_reserved_snapshot_version();
    writes.clear();
    if (tx) {
      auto *service = query_transaction_service();
      const bool rollback = !tx->is_shadow() && tx->is_in_tx() && !tx->is_tx_end();
      if (rollback) { service->rollback_tx(*tx); }
      service->release_tx(*tx);
      tx = nullptr;
      fprintf(stderr, "PROTOTYPE_V14_TX_RELEASED session=%u rollback=%d\n", sid, rollback);
    }
  }
  ~EngineWrites() { reset(); }
  int release(uint64_t txid) {
    if (!tx || static_cast<uint64_t>(tx->get_tx_id().get_id()) != txid
        || !writes.empty()) { return OB_INVALID_ARGUMENT; }
    reset();
    return OB_SUCCESS;
  }
  int close(uint64_t txid, uint64_t handle, ObTxDesc &view) {
    if (!tx || static_cast<uint64_t>(tx->get_tx_id().get_id()) != txid
        || static_cast<uint64_t>(view.get_tx_id().get_id()) != txid) {
      return OB_INVALID_ARGUMENT;
    }
    auto it = writes.find(handle);
    if (it == writes.end()) { return OB_INVALID_ARGUMENT; }
    writes.erase(it);
    return query_transaction_service()->merge_tx_state(view, *tx);
  }
  int prepare(const WritePrepareRequest &request, const ObTxDesc &view,
              uint64_t &handle) {
    handle = 0;
    if (!tx || tx->get_tx_id() != view.get_tx_id()
        || (request.storage_space != storage_space
            && !request.storage_space.is_global())) { return OB_INVALID_ARGUMENT; }
    if (writes.size() >= 32) { return OB_SIZE_OVERFLOW; }
    auto prepared = std::make_unique<EngineWrite>();
    int ret = prepared->prepare(request, *tx);
    if (!ret) {
      handle = ++sequence;
      writes.emplace(handle, std::move(prepared));
    }
    return ret;
  }
  int batch(const ObTxDesc &view, const WriteBatch &request,
            int64_t &affected, WriteResult &duplicates) {
    if (!tx || tx->get_tx_id() != view.get_tx_id()) { return OB_INVALID_ARGUMENT; }
    if (request.operation != 'I' && request.operation != 'U'
        && request.operation != 'D' && request.operation != 'L'
        && request.operation != 'p' && request.operation != 'f') {
      return OB_INVALID_ARGUMENT;
    }
    auto it = writes.find(request.handle);
    return it == writes.end() ? OB_INVALID_ARGUMENT
        : it->second->batch(request, *tx, affected, duplicates);
  }
};

// Compatibility view for query's existing descriptor accessors. Constructing
// and decoding this value does not start a transaction service, register a
// transaction, or allocate a storage context in the worker. Engine owns all
// authoritative transaction state; this view is refreshed by native calls.
// Ticket 05c: resolve the transaction's owning session when the ambient
// worker names another session or none (async end-trans completion runs off
// the query thread). A session borrowed from the session manager is returned
// through `borrowed` and must be reverted by the caller.
sql::ObSQLSessionInfo *tx_owner_session(transaction::ObTxDesc &tx,
                                        sql::ObSQLSessionInfo *&borrowed)
{
  borrowed = nullptr;
  sql::ObSQLSessionInfo *session = THIS_WORKER.get_session();
  if (session != nullptr && session->get_tx_desc() == &tx) { return session; }
  auto *mgr = share::server_service<sql::ObSQLSessionMgr>();
  sql::ObSQLSessionInfo *resolved = nullptr;
  if (OB_NOT_NULL(mgr)
      && OB_SUCCESS == mgr->get_session(tx.get_session_id(), resolved)
      && OB_NOT_NULL(resolved) && resolved->get_tx_desc() == &tx
      && in_process_session_ns(resolved) > 0) {
    borrowed = resolved;
    return resolved;
  }
  if (OB_NOT_NULL(resolved)) { mgr->revert_session(resolved); }
  return session;
}
void revert_tx_owner_session(sql::ObSQLSessionInfo *borrowed)
{
  if (OB_NOT_NULL(borrowed)) {
    share::server_service<sql::ObSQLSessionMgr>()->revert_session(borrowed);
  }
}
int call_in_process_tx_state(char operation, ObTxDesc &view,
                             const ObTxParam *param, int64_t deadline);
int call_in_process_tx_read_snapshot(ObTxDesc &view,
    ObTxIsolationLevel isolation, int64_t deadline, ObTxReadSnapshot &snapshot);
int call_in_process_tx_create_savepoint(ObTxDesc &view, char operation,
    const ObTxParam *param, bool release, int16_t branch, ObTxSEQ &savepoint);
int call_in_process_tx_rollback_savepoint(ObTxDesc &view, ObTxSEQ savepoint,
    int64_t deadline, bool touched_storage, ObTxCleanPolicy policy);
int call_in_process_tx_named_savepoint(ObTxDesc &view, char operation,
    const ObString &name, int64_t deadline);
int call_in_process_tx_exec_result(ObTxDesc &view, char operation,
    const ObTxExecResult *input, ObTxExecResult *output);
int call_in_process_tx_register_mds(ObTxDesc &view,
    ObTxDataSourceType type, StorageSpaceHandle space,
    const char *buffer, int64_t buffer_size,
    const ObRegisterMdsFlag &flag, ObTxSEQ sequence);
int call_in_process_tx_table_lock(ObTxDesc &view,
    obcall::ObInnerSQLTransmitArg::InnerSQLOperationType operation,
    StorageSpaceHandle space, const ObTxParam &param,
    const ObString &payload, const TableLockPlan &plan);
int call_in_process_tablet_binding(ObTxDesc &view, char operation,
    StorageSpaceHandle space, const ObIArray<ObTabletID> &tablets,
    const ObIArray<ObTabletID> *hidden, int64_t schema_version,
    int64_t deadline);
int tx_state(char operation, ObTxDesc &view,
             const ObTxParam *param = nullptr, int64_t deadline = 0)
{
  sql::ObSQLSessionInfo *borrowed = nullptr;
  auto *session = tx_owner_session(view, borrowed);
  StorageSessionScope scope(session && session->get_tx_desc() == &view ? session : nullptr);
  const int ret = scope.error() ? scope.error()
      : call_in_process_tx_state(operation, view, param, deadline);
  revert_tx_owner_session(borrowed);
  return ret;
}
int tx_read_snapshot(ObTxDesc &view, ObTxIsolationLevel isolation,
                     int64_t deadline, ObTxReadSnapshot &snapshot)
{
  sql::ObSQLSessionInfo *borrowed = nullptr;
  auto *session = tx_owner_session(view, borrowed);
  StorageSessionScope scope(session && session->get_tx_desc() == &view ? session : nullptr);
  const int ret = scope.error() ? scope.error()
      : call_in_process_tx_read_snapshot(view, isolation, deadline, snapshot);
  revert_tx_owner_session(borrowed);
  return ret;
}
int tx_create_savepoint(ObTxDesc &view, char operation,
    const ObTxParam *param, bool release, int16_t branch, ObTxSEQ &savepoint)
{
  sql::ObSQLSessionInfo *borrowed = nullptr;
  auto *session = tx_owner_session(view, borrowed);
  StorageSessionScope scope(session && session->get_tx_desc() == &view ? session : nullptr);
  const int ret = scope.error() ? scope.error()
      : call_in_process_tx_create_savepoint(
          view, operation, param, release, branch, savepoint);
  revert_tx_owner_session(borrowed);
  return ret;
}
int tx_rollback_savepoint(ObTxDesc &view, ObTxSEQ savepoint,
    int64_t deadline, bool touched_storage, ObTxCleanPolicy policy)
{
  sql::ObSQLSessionInfo *borrowed = nullptr;
  auto *session = tx_owner_session(view, borrowed);
  StorageSessionScope scope(session && session->get_tx_desc() == &view ? session : nullptr);
  const int ret = scope.error() ? scope.error()
      : call_in_process_tx_rollback_savepoint(
          view, savepoint, deadline, touched_storage, policy);
  revert_tx_owner_session(borrowed);
  return ret;
}
int tx_named_savepoint(ObTxDesc &view, char operation,
    const ObString &name, int64_t deadline = 0)
{
  sql::ObSQLSessionInfo *borrowed = nullptr;
  auto *session = tx_owner_session(view, borrowed);
  StorageSessionScope scope(session && session->get_tx_desc() == &view ? session : nullptr);
  const int ret = scope.error() ? scope.error()
      : call_in_process_tx_named_savepoint(view, operation, name, deadline);
  revert_tx_owner_session(borrowed);
  return ret;
}
int tx_exec_result(ObTxDesc &view, char operation,
    const ObTxExecResult *input, ObTxExecResult *output)
{
  sql::ObSQLSessionInfo *borrowed = nullptr;
  auto *session = tx_owner_session(view, borrowed);
  StorageSessionScope scope(session && session->get_tx_desc() == &view ? session : nullptr);
  const int ret = scope.error() ? scope.error()
      : call_in_process_tx_exec_result(view, operation, input, output);
  revert_tx_owner_session(borrowed);
  return ret;
}
int tx_register_mds(ObTxDesc &view, ObTxDataSourceType type,
    StorageSpaceHandle space, const char *buffer, int64_t buffer_size,
    const ObRegisterMdsFlag &flag, ObTxSEQ sequence)
{
  sql::ObSQLSessionInfo *borrowed = nullptr;
  auto *session = tx_owner_session(view, borrowed);
  StorageSessionScope scope(session && session->get_tx_desc() == &view ? session : nullptr);
  const int ret = scope.error() ? scope.error()
      : call_in_process_tx_register_mds(
          view, type, space, buffer, buffer_size, flag, sequence);
  revert_tx_owner_session(borrowed);
  return ret;
}
int tx_table_lock(ObTxDesc &view,
    obcall::ObInnerSQLTransmitArg::InnerSQLOperationType operation,
    StorageSpaceHandle space, const ObTxParam &param,
    const ObString &payload, const TableLockPlan &plan)
{
  sql::ObSQLSessionInfo *borrowed = nullptr;
  auto *session = tx_owner_session(view, borrowed);
  StorageSessionScope scope(session && session->get_tx_desc() == &view ? session : nullptr);
  const int ret = scope.error() ? scope.error()
      : call_in_process_tx_table_lock(
          view, operation, space, param, payload, plan);
  revert_tx_owner_session(borrowed);
  return ret;
}

class InProcessInnerConnectionLockRuntime final : public ObIInnerConnectionLockRuntime
{
public:
  int process_lock_rpc(
      const obcall::ObInnerSQLTransmitArg &arg,
      common::sqlclient::ObISQLConnection *conn) override
  {
    return invoke_raw(arg.get_operation_type(), arg.get_inner_sql(), conn);
  }

  int lock_table(uint64_t table_id,
                 ObTableLockMode lock_mode,
                 int64_t timeout_us,
                 common::sqlclient::ObISQLConnection *conn,
                 ObTableLockOwnerID owner_id,
                 ObTableLockPriority lock_priority) override
  {
    ObLockTableRequest arg;
    arg.table_id_ = table_id;
    arg.owner_id_ = owner_id;
    arg.lock_mode_ = lock_mode;
    arg.op_type_ = IN_TRANS_COMMON_LOCK;
    arg.timeout_us_ = timeout_us;
    arg.lock_priority_ = lock_priority;
    return invoke(obcall::ObInnerSQLTransmitArg::OPERATION_TYPE_LOCK_TABLE, arg, conn);
  }

  int lock_table(const ObLockTableRequest &arg,
                 common::sqlclient::ObISQLConnection *conn) override
  {
    return invoke(obcall::ObInnerSQLTransmitArg::OPERATION_TYPE_LOCK_TABLE, arg, conn);
  }

  int unlock_table(const ObUnLockTableRequest &arg,
                   common::sqlclient::ObISQLConnection *conn) override
  {
    return invoke(obcall::ObInnerSQLTransmitArg::OPERATION_TYPE_UNLOCK_TABLE, arg, conn);
  }

  int lock_tablet(uint64_t table_id,
                  ObTabletID tablet_id,
                  ObTableLockMode lock_mode,
                  int64_t timeout_us,
                  common::sqlclient::ObISQLConnection *conn) override
  {
    ObLockTabletsRequest arg;
    arg.table_id_ = table_id;
    arg.owner_id_.set_default();
    arg.lock_mode_ = lock_mode;
    arg.op_type_ = IN_TRANS_COMMON_LOCK;
    arg.timeout_us_ = timeout_us;
    int ret = arg.tablet_ids_.push_back(tablet_id);
    return ret ? ret : invoke(obcall::ObInnerSQLTransmitArg::OPERATION_TYPE_LOCK_TABLET, arg, conn);
  }

  int lock_tablet(uint64_t table_id,
                  const ObIArray<ObTabletID> &tablet_ids,
                  ObTableLockMode lock_mode,
                  int64_t timeout_us,
                  common::sqlclient::ObISQLConnection *conn) override
  {
    ObLockTabletsRequest arg;
    arg.table_id_ = table_id;
    arg.owner_id_.set_default();
    arg.lock_mode_ = lock_mode;
    arg.op_type_ = IN_TRANS_COMMON_LOCK;
    arg.timeout_us_ = timeout_us;
    int ret = arg.tablet_ids_.assign(tablet_ids);
    return ret ? ret : invoke(obcall::ObInnerSQLTransmitArg::OPERATION_TYPE_LOCK_TABLET, arg, conn);
  }

  int lock_tablet(const ObLockAloneTabletRequest &arg,
                  common::sqlclient::ObISQLConnection *conn) override
  {
    return invoke(obcall::ObInnerSQLTransmitArg::OPERATION_TYPE_LOCK_ALONE_TABLET, arg, conn);
  }

  int unlock_tablet(const ObUnLockAloneTabletRequest &arg,
                    common::sqlclient::ObISQLConnection *conn) override
  {
    return invoke(obcall::ObInnerSQLTransmitArg::OPERATION_TYPE_UNLOCK_ALONE_TABLET, arg, conn);
  }

  int lock_obj(const ObLockObjRequest &arg,
               common::sqlclient::ObISQLConnection *conn) override
  {
    return invoke(obcall::ObInnerSQLTransmitArg::OPERATION_TYPE_LOCK_OBJ, arg, conn);
  }

  int unlock_obj(const ObUnLockObjRequest &arg,
                 common::sqlclient::ObISQLConnection *conn) override
  {
    return invoke(obcall::ObInnerSQLTransmitArg::OPERATION_TYPE_UNLOCK_OBJ, arg, conn);
  }

  int lock_obj(const ObLockObjsRequest &arg,
               common::sqlclient::ObISQLConnection *conn) override
  {
    return invoke(obcall::ObInnerSQLTransmitArg::OPERATION_TYPE_LOCK_OBJS, arg, conn);
  }

  int unlock_obj(const ObUnLockObjsRequest &arg,
                 common::sqlclient::ObISQLConnection *conn) override
  {
    return invoke(obcall::ObInnerSQLTransmitArg::OPERATION_TYPE_UNLOCK_OBJS, arg, conn);
  }

  int replace_lock(const ObReplaceLockRequest &arg,
                   common::sqlclient::ObISQLConnection *conn) override
  {
    return invoke(obcall::ObInnerSQLTransmitArg::OPERATION_TYPE_REPLACE_LOCK, arg, conn);
  }

  int replace_lock(const ObReplaceAllLocksRequest &arg,
                   common::sqlclient::ObISQLConnection *conn) override
  {
    return invoke(obcall::ObInnerSQLTransmitArg::OPERATION_TYPE_REPLACE_LOCKS, arg, conn);
  }

  int execute_write_sql(common::sqlclient::ObISQLConnection *conn,
                        const ObSqlString &sql,
                        int64_t &affected_rows) override
  {
    auto *inner = static_cast<ObInnerSQLConnection *>(conn);
    return inner ? inner->execute_write(sql.ptr(), affected_rows) : OB_INVALID_ARGUMENT;
  }

  int execute_read_sql(common::sqlclient::ObISQLConnection *conn,
                       const ObSqlString &sql,
                       ObISQLClient::ReadResult &result) override
  {
    auto *inner = static_cast<ObInnerSQLConnection *>(conn);
    return inner ? inner->execute_read(sql.ptr(), result) : OB_INVALID_ARGUMENT;
  }

private:
  int invoke_raw(obcall::ObInnerSQLTransmitArg::InnerSQLOperationType operation,
                 const ObString &payload,
                 common::sqlclient::ObISQLConnection *conn)
  {
    auto *inner = static_cast<ObInnerSQLConnection *>(conn);
    ObTxDesc *tx = inner ? inner->get_session().get_tx_desc() : nullptr;
    if (!inner || !inner->is_in_trans() || !tx || payload.empty()) {
      return OB_INVALID_ARGUMENT;
    }
    // An inner SQL lock may run while another session is ambient. Restore the
    // inner session's storage route so locks and catalog writes share its tx.
    if (!inner->get_session().namespace_storage_binding()) {
      return OB_NOT_INIT;
    }
    StorageSessionScope storage_scope(&inner->get_session(), false);
    if (storage_scope.error()) {
      return storage_scope.error();
    }
    ObTxParam param;
    param.access_mode_ = ObTxAccessMode::RW;
    param.isolation_ = inner->get_session().get_tx_isolation();
    inner->get_session().get_tx_timeout(param.timeout_us_);
    param.lock_timeout_us_ = inner->get_session().get_trx_lock_timeout();
    TableLockPlan lock_plan;
    lock_plan.storage_space = active_worker_storage_space();
    int ret = OB_SUCCESS;
    if (is_schema_table_lock_operation(operation)) {
      ret = append_worker_table_lock_plan(operation, payload, lock_plan);
    }
    return ret ? ret : tx_table_lock(
        *tx, operation, lock_plan.storage_space, param, payload, lock_plan);
  }

  template <typename T>
  int invoke(obcall::ObInnerSQLTransmitArg::InnerSQLOperationType operation,
             const T &arg,
             common::sqlclient::ObISQLConnection *conn)
  {
    const int64_t size = arg.get_serialize_size();
    if (size <= 0 || size > static_cast<int64_t>(MAX_SQL_MESSAGE)) {
      return OB_SIZE_OVERFLOW;
    }
    std::vector<char> buffer(size);
    int64_t pos = 0;
    int ret = arg.serialize(buffer.data(), buffer.size(), pos);
    if (!ret && pos != size) { ret = OB_ERR_UNEXPECTED; }
    return ret ? ret : invoke_raw(
        operation, ObString(static_cast<int32_t>(pos), buffer.data()), conn);
  }
};

int call_in_process_tx_clock(
    const std::function<int(ObITransactionService &)> &call);
int call_in_process_tx_interrupt(const transaction::ObTxDesc &tx, int cause);
int call_in_process_tx_snapshot(char operation,
                                transaction::ObTxReadSnapshot &snapshot);
int release_in_process_tx(const transaction::ObTxDesc &tx);
int close_in_process_write(transaction::ObTxDesc &view, uint64_t handle);
