// Included inside oceanbase::observer::namespace_worker_prototype.
class InProcessTransactionService final : public ObITransactionService {
public:
  int gen_unique_id(int64_t &unique_id, int64_t timeout_us) override {
    if (timeout_us <= 0) { return OB_INVALID_ARGUMENT; }
    return call_in_process_tx_clock(
        [&](ObITransactionService &service) {
          return service.gen_unique_id(unique_id, timeout_us);
        });
  }
  int get_gts_sync(int64_t timeout_us, share::SCN &gts) override {
    if (timeout_us <= 0) { return OB_INVALID_ARGUMENT; }
    return call_in_process_tx_clock(
        [&](ObITransactionService &service) {
          return service.get_gts_sync(timeout_us, gts);
        });
  }
  int acquire_tx(transaction::ObTxDesc *&tx,
                         uint32_t session_id) override {
    if (tx) { return OB_INVALID_ARGUMENT; }
    auto owned = std::make_unique<ObTxDesc>();
    int ret = tx_state('A', *owned);
    if (!ret) { tx = owned.release(); }
    return ret; }
  int acquire_tx(const char *buf,
                         int64_t len,
                         int64_t &pos,
                         transaction::ObTxDesc *&tx) override {
    if (tx) { return OB_INVALID_ARGUMENT; }
    auto owned = std::make_unique<ObTxDesc>();
    int ret = owned->deserialize_shadow(buf, len, pos);
    if (!ret) {
      // Native PX deserialization creates a private execution-state copy. Its
      // release must never terminate the coordinator's storage transaction.
      tx = owned.release();
    }
    return ret; }
  int start_tx(transaction::ObTxDesc &tx,
                       const transaction::ObTxParam &tx_param) override {
    return tx_state('H', tx, &tx_param); }
  int abort_tx(transaction::ObTxDesc &tx, int cause) override { return rollback_tx(tx); }
  int rollback_tx(transaction::ObTxDesc &tx) override { return tx_state('R', tx); }
  int commit_tx(transaction::ObTxDesc &tx,
                        int64_t expire_ts) override { return tx_state('C', tx, nullptr, expire_ts); }
  int submit_commit_tx(transaction::ObTxDesc &tx,
                               int64_t expire_ts,
                               transaction::ObITxCallback &callback) override {
    // Native callbacks support completion before submit returns. Commit stays
    // authoritative in storage; only then release the native SQL response.
    const int ret = commit_tx(tx, expire_ts);
    if (!ret) { callback.callback(OB_SUCCESS); }
    return ret;
  }
  int release_tx(transaction::ObTxDesc &tx) override {
    int ret = OB_SUCCESS;
    if (!tx.is_shadow()) {
      // Release through the owning session's in-process storage context.
      sql::ObSQLSessionInfo *borrowed = nullptr;
      auto *session = tx_owner_session(tx, borrowed);
      if (session != nullptr && session->get_tx_desc() == &tx
          && in_process_session_ns(session) > 1) {
        StorageSessionScope scope(session, false);
        ret = scope.error() ? scope.error() : release_in_process_tx(tx);
      }
      revert_tx_owner_session(borrowed);
    }
    delete &tx; return ret; }
  int reuse_tx(transaction::ObTxDesc &tx) override { return tx_state('U', tx); }
  int prepare_tx_for_statement(transaction::ObTxDesc &tx) override { return tx_state('S', tx); }
  int prepare_tx_for_autocommit_retry(transaction::ObTxDesc &tx) override { return tx_state('N', tx); }
  int register_mds_into_tx(
      transaction::ObTxDesc &tx,
      const transaction::ObTxDataSourceType &type,
      const char *buffer,
      int64_t buffer_size,
      const transaction::ObRegisterMdsFlag &flag,
      transaction::ObTxSEQ sequence) override {
    if (buffer == nullptr || buffer_size <= 0 || buffer_size > INT32_MAX) {
      return OB_INVALID_ARGUMENT;
    }
    StorageSpaceHandle storage_space;
    int ret = worker_mds_storage_space(type, buffer, buffer_size, storage_space);
    return ret ? ret : tx_register_mds(
        tx, type, storage_space, buffer, buffer_size, flag, sequence);
  }
  int interrupt(transaction::ObTxDesc &tx, int cause) override {
    return call_in_process_tx_interrupt(tx, cause);
  }
  int get_read_snapshot(transaction::ObTxDesc &tx,
                                transaction::ObTxIsolationLevel isolation_level,
                                int64_t expire_ts,
                                transaction::ObTxReadSnapshot &snapshot) override {
    return tx_read_snapshot(tx, isolation_level, expire_ts, snapshot); }
  int get_read_snapshot_version(int64_t expire_ts,
                                share::SCN &snapshot_version) override {
    if (expire_ts <= 0) { return OB_INVALID_ARGUMENT; }
    return call_in_process_tx_clock(
        [&](ObITransactionService &service) {
          return service.get_read_snapshot_version(
              std::min(expire_ts, THIS_WORKER.get_timeout_ts()), snapshot_version);
        });
  }
  int get_weak_read_snapshot_version(int64_t max_read_stale_time,
                                     share::SCN &snapshot_version) override {
    return call_in_process_tx_clock(
        [&](ObITransactionService &service) {
          return service.get_weak_read_snapshot_version(
              max_read_stale_time, snapshot_version);
        });
  }
  int register_tx_snapshot_verify(
      transaction::ObTxReadSnapshot &snapshot) override {
    if (!snapshot.tx_id().is_valid()) { return OB_SUCCESS; }
    sql::ObSQLSessionInfo *session = THIS_WORKER.get_session();
    transaction::ObTxDesc *tx = session ? session->get_tx_desc() : nullptr;
    if (!tx || tx->get_tx_id() != snapshot.tx_id()) {
      return OB_INVALID_ARGUMENT;
    }
    return call_in_process_tx_snapshot('v', snapshot);
  }
  int refresh_tx_snapshot_verify(
      transaction::ObTxReadSnapshot &snapshot) override {
    if (!snapshot.tx_id().is_valid()
        || !snapshot.is_valid()
        || snapshot.is_committed()) {
      return OB_SUCCESS;
    }
    return call_in_process_tx_snapshot('z', snapshot);
  }
  int unregister_tx_snapshot_verify(
      transaction::ObTxReadSnapshot &snapshot) override {
    if (!snapshot.tx_id().is_valid()) { return OB_SUCCESS; }
    return call_in_process_tx_snapshot('y', snapshot);
  }
  int create_implicit_savepoint(transaction::ObTxDesc &tx,
                                        const transaction::ObTxParam &tx_param,
                                        transaction::ObTxSEQ &savepoint,
                                        bool release) override {
    return tx_create_savepoint(tx, 'P', &tx_param, release, 0, savepoint); }
  int create_branch_savepoint(transaction::ObTxDesc &tx,
                                      int16_t branch,
                                      transaction::ObTxSEQ &savepoint) override {
    return tx_create_savepoint(tx, 'J', nullptr, false, branch, savepoint); }
  int create_in_txn_implicit_savepoint(transaction::ObTxDesc &tx,
                                               transaction::ObTxSEQ &savepoint) override {
    return tx_create_savepoint(tx, 'I', nullptr, false, 0, savepoint); }
  int create_explicit_savepoint(transaction::ObTxDesc &tx,
                                        const common::ObString &savepoint) override {
    return tx_named_savepoint(tx, 'F', savepoint); }
  int rollback_to_implicit_savepoint(
      transaction::ObTxDesc &tx,
      transaction::ObTxSEQ savepoint,
      int64_t expire_ts,
      bool touched_storage,
      transaction::ObTxCleanPolicy clean_policy) override {
    return tx_rollback_savepoint(tx, savepoint, expire_ts,
        touched_storage, clean_policy); }
  int rollback_to_explicit_savepoint(transaction::ObTxDesc &tx,
                                             const common::ObString &savepoint,
                                             int64_t expire_ts) override {
    return tx_named_savepoint(tx, 'L', savepoint, expire_ts); }
  int release_explicit_savepoint(transaction::ObTxDesc &tx,
                                         const common::ObString &savepoint) override {
    return tx_named_savepoint(tx, 'D', savepoint); }
  int create_stash_savepoint(transaction::ObTxDesc &tx,
                                     const common::ObString &name) override {
    return tx_named_savepoint(tx, 'K', name); }
  int merge_tx_state(transaction::ObTxDesc &to,
                             const transaction::ObTxDesc &from) override {
    if (to.get_tx_id() != from.get_tx_id()) { return OB_INVALID_ARGUMENT; }
    // These are the same descriptor operations used by ObTransService. Task
    // copies aggregate locally; add_tx_exec_result publishes to the owner.
    return to.merge_exec_info_with(from);
  }
  int get_tx_exec_result(transaction::ObTxDesc &tx,
                                 transaction::ObTxExecResult &exec_info) override {
    return tx.is_shadow() ? tx.get_inc_exec_info(exec_info) : collect_tx_exec_result(tx, exec_info);
  }
  int add_tx_exec_result(transaction::ObTxDesc &tx,
                                 const transaction::ObTxExecResult &exec_info) override {
    if (tx.is_shadow()) { return tx.add_exec_info(exec_info); }
    return tx_exec_result(tx, 'a', &exec_info, nullptr);
  }
  int collect_tx_exec_result(transaction::ObTxDesc &tx,
                                     transaction::ObTxExecResult &result) override {
    if (tx.is_shadow()) { return tx.get_inc_exec_info(result); }
    return tx_exec_result(tx, 'E', nullptr, &result); }
  bool can_elr() const override { return false; }
};

int call_in_process_rootserver_runtime(
    uint64_t namespace_id,
    const std::function<int(rootserver::ObIRootserverLocalRuntime &,
                            StorageSpaceHandle)> &call);

class InProcessRootserverLocalRuntime final
    : public rootserver::ObIRootserverLocalRuntime
{
public:
  explicit InProcessRootserverLocalRuntime(uint64_t namespace_id = 0)
      : namespace_id_(namespace_id) {}
  int set_ds_action(const obcall::ObDebugSyncActionArg &arg) override {
    return call_in_process_rootserver_runtime(namespace_id_,
        [&](rootserver::ObIRootserverLocalRuntime &runtime, StorageSpaceHandle) {
          return runtime.set_ds_action(arg);
        });
  }
  int calc_column_checksum_request(
      const obcall::ObCalcColumnChecksumRequestArg &arg,
      obcall::ObCalcColumnChecksumRequestRes &result) override {
    return call_in_process_rootserver_runtime(namespace_id_,
        [&](rootserver::ObIRootserverLocalRuntime &runtime, StorageSpaceHandle space) {
          return calc_namespace_column_checksum(space, runtime, arg, result);
        });
  }
  int build_ddl_local(
      const obcall::ObDDLLocalBuildArg &arg,
      obcall::ObDDLLocalBuildResult &result) override {
    return call_in_process_rootserver_runtime(namespace_id_,
        [&](rootserver::ObIRootserverLocalRuntime &runtime, StorageSpaceHandle space) {
          obcall::ObDDLLocalBuildArg routed;
          const int ret = route_rootserver_build_arg(space, arg, routed);
          return ret ? ret : runtime.build_ddl_local(routed, result);
        });
  }
  int check_and_cancel_ddl_complement_data_dag(
      const obcall::ObDDLLocalBuildArg &arg,
      bool &is_dag_exist) override {
    return call_in_process_rootserver_runtime(namespace_id_,
        [&](rootserver::ObIRootserverLocalRuntime &runtime, StorageSpaceHandle space) {
          obcall::ObDDLLocalBuildArg routed;
          const int ret = route_rootserver_build_arg(space, arg, routed);
          return ret ? ret : runtime.check_and_cancel_ddl_complement_data_dag(
              routed, is_dag_exist);
        });
  }
  int check_and_cancel_delete_lob_meta_row_dag(
      const obcall::ObDDLLocalBuildArg &arg,
      bool &is_dag_exist) override {
    return call_in_process_rootserver_runtime(namespace_id_,
        [&](rootserver::ObIRootserverLocalRuntime &runtime, StorageSpaceHandle space) {
          obcall::ObDDLLocalBuildArg routed;
          const int ret = route_rootserver_build_arg(space, arg, routed);
          return ret ? ret : runtime.check_and_cancel_delete_lob_meta_row_dag(
              routed, is_dag_exist);
        });
  }
  int minor_freeze(
      const obcall::ObMinorFreezeArg &arg,
      obcall::Int64 &result) override {
    return call_in_process_rootserver_runtime(namespace_id_,
        [&](rootserver::ObIRootserverLocalRuntime &runtime, StorageSpaceHandle space) {
          obcall::ObMinorFreezeArg routed = arg;
          int ret = routed.tablet_id_.is_valid()
              ? route_tablet_id(space, routed.tablet_id_) : OB_SUCCESS;
          return ret ? ret : runtime.minor_freeze(routed, result);
        });
  }
  int check_schema_version_elapsed(
      const obcall::ObCheckSchemaVersionElapsedArg &arg,
      obcall::ObCheckSchemaVersionElapsedResult &result) override {
    return call_in_process_rootserver_runtime(namespace_id_,
        [&](rootserver::ObIRootserverLocalRuntime &runtime, StorageSpaceHandle space) {
          obcall::ObCheckSchemaVersionElapsedArg routed = arg;
          routed.schema_version_refreshed_by_caller_ = true;
          return check_namespace_tablet_elapsed(
              space, routed, result,
              [&runtime](const obcall::ObCheckSchemaVersionElapsedArg &storage_arg,
                         obcall::ObCheckSchemaVersionElapsedResult &storage_result) {
                return runtime.check_schema_version_elapsed(storage_arg, storage_result);
              });
        });
  }
  int check_modify_time_elapsed(
      const obcall::ObCheckModifyTimeElapsedArg &arg,
      obcall::ObCheckModifyTimeElapsedResult &result) override {
    return call_in_process_rootserver_runtime(namespace_id_,
        [&](rootserver::ObIRootserverLocalRuntime &runtime, StorageSpaceHandle space) {
          obcall::ObCheckModifyTimeElapsedArg routed = arg;
          return check_namespace_tablet_elapsed(
              space, routed, result,
              [&runtime](const obcall::ObCheckModifyTimeElapsedArg &storage_arg,
                         obcall::ObCheckModifyTimeElapsedResult &storage_result) {
                return runtime.check_modify_time_elapsed(storage_arg, storage_result);
              });
        });
  }
  int check_ddl_tablet_merge_status(
      const obcall::ObDDLCheckTabletMergeStatusArg &arg,
      obcall::ObDDLCheckTabletMergeStatusResult &result) override {
    return call_in_process_rootserver_runtime(namespace_id_,
        [&](rootserver::ObIRootserverLocalRuntime &runtime, StorageSpaceHandle space) {
          obcall::ObDDLCheckTabletMergeStatusArg routed = arg;
          int ret = OB_SUCCESS;
          for (int64_t i = 0; !ret && i < routed.tablet_ids_.count(); ++i) {
            ret = route_tablet_id(space, routed.tablet_ids_.at(i));
          }
          return ret ? ret : runtime.check_ddl_tablet_merge_status(routed, result);
        });
  }
  int check_server_empty(bool &is_empty) override {
    return call_in_process_rootserver_runtime(namespace_id_,
        [&](rootserver::ObIRootserverLocalRuntime &runtime, StorageSpaceHandle) {
          return runtime.check_server_empty(is_empty);
        });
  }
  int modify_tablet_binding_for_rw_defensive(
      common::ObMySQLTransaction &trans,
      const common::ObIArray<common::ObTabletID> &tablet_ids,
      int64_t schema_version,
      int64_t abs_timeout_us) override {
    return modify_tablet_binding_defensive_(
        'b', trans, tablet_ids, schema_version, abs_timeout_us);
  }
  int modify_tablet_binding_for_write_defensive(
      common::ObMySQLTransaction &trans,
      const common::ObIArray<common::ObTabletID> &tablet_ids,
      int64_t schema_version,
      int64_t abs_timeout_us) override {
    return modify_tablet_binding_defensive_(
        'd', trans, tablet_ids, schema_version, abs_timeout_us);
  }
private:
  int modify_tablet_binding_defensive_(
      char operation,
      common::ObMySQLTransaction &trans,
      const common::ObIArray<common::ObTabletID> &tablet_ids,
      int64_t schema_version,
      int64_t abs_timeout_us) {
    sqlclient::ObISQLConnection *connection = trans.get_connection();
    sql::ObSQLSessionInfo *session =
        query::ObInnerSQLConnectionAccess::get_session(connection);
    transaction::ObTxDesc *tx = session ? session->get_tx_desc() : nullptr;
    int ret = !trans.is_started() || !connection || !session || !tx
        ? OB_INVALID_ARGUMENT : OB_SUCCESS;
    StorageSessionScope scope(session, false);
    if (!ret && scope.error()) { ret = scope.error(); }
    if (!ret) { ret = call_in_process_tablet_binding(*tx, operation,
        storage_space_(), tablet_ids, nullptr, schema_version, abs_timeout_us); }
    return ret;
  }
public:
  int modify_tablet_binding_for_unbind(
      common::ObMySQLTransaction &trans,
      const common::ObIArray<common::ObTabletID> &orig_tablet_ids,
      const common::ObIArray<common::ObTabletID> &hidden_tablet_ids,
      int64_t schema_version,
      int64_t abs_timeout_us) override {
    sqlclient::ObISQLConnection *connection = trans.get_connection();
    sql::ObSQLSessionInfo *session =
        query::ObInnerSQLConnectionAccess::get_session(connection);
    transaction::ObTxDesc *tx = session ? session->get_tx_desc() : nullptr;
    int ret = !trans.is_started() || !connection || !session || !tx
        ? OB_INVALID_ARGUMENT : OB_SUCCESS;
    StorageSessionScope scope(session, false);
    if (!ret && scope.error()) { ret = scope.error(); }
    if (!ret) { ret = call_in_process_tablet_binding(*tx, 'u',
        storage_space_(), orig_tablet_ids, &hidden_tablet_ids,
        schema_version, abs_timeout_us); }
    return ret;
  }
  int wait_until_change_stream_refreshed(
      common::ObMySQLProxy &mysql_proxy,
      int64_t timeout_us) override {
    UNUSEDx(mysql_proxy, timeout_us);
    // Change-stream freshness is SQL/catalog work and must stay in the worker.
    // The namespace-fork path does not require it; expose no shared-process SQL
    // fallback while that worker-local service is being separated.
    return OB_NOT_SUPPORTED;
  }

private:
  StorageSpaceHandle storage_space_() const {
    return namespace_id_ == 0 ? active_worker_storage_space()
        : StorageSpaceHandle::namespace_space(namespace_id_);
  }
  uint64_t namespace_id_;
};
