/*
 * Copyright (c) 2025 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

int create_ls(ObLS &ls,
              ObITxLogParam *param,
              ObITxLogAdapter * log_adapter);
int remove_ls(const bool graceful = true);
/*
 * acquire a transaction descriptor by deserialize from buffer
 */
int acquire_tx(const char* buf, const int64_t len, int64_t &pos, ObTxDesc *&tx);
int release_tx_ref(ObTxDesc &tx);
/*
 * Used when a session is destroyed while it still references a TxDesc.
 */
static void force_release_tx_when_session_destroy(ObTxDesc &tx);
/*
 * interrupt any work in progress thread
 */
int interrupt(ObTxDesc &tx, int cause);

/*
 * create an implicit savepoint when txn is active
 */
int create_in_txn_implicit_savepoint(ObTxDesc &tx, ObTxSEQ &savepoint);

/*
 * prepare a transaction
 * if transaction not started return READ_ONLY
 * and commit phase should not be called
 */
/*
 * commit or abort a prepared transaction
 */

/*
 * prepare transaction commit target
 * return READ_ONLY if transaction does not contain a write state
 */

/*
 * collect transaction write state info after transactional data access
 */
int collect_tx_exec_result(ObTxDesc &tx,
                           ObTxExecResult &result);
/*********************************************************************
 *
 * get_store_ctx / revert_store_ctx
 *
 * pre-hook in Data Access to prepare transaction relative work
 *
 ********************************************************************/
int get_read_store_ctx(const ObTxReadSnapshot &snapshot,
                       const bool read_latest,
                       const int64_t lock_timeout,
                       ObStoreCtx &store_ctx,
                       ObTxDesc *tx_desc = NULL);
int get_read_store_ctx(const share::SCN snapshot_version,
                       const int64_t lock_timeout,
                       ObStoreCtx &store_ctx);
int get_write_store_ctx(ObTxDesc &tx,
                        const ObTxReadSnapshot &snapshot,
                        const concurrent_control::ObWriteFlag write_flag,
                        storage::ObStoreCtx &store_ctx,
                        const ObTxSEQ &spec_seq_no = ObTxSEQ::INVL(),
                        const bool special = false);
int revert_store_ctx(storage::ObStoreCtx &store_ctx);

int acquire_tx_ctx(const ObTxDesc &tx,
                   ObTxCtx *&ctx,
                   ObLS *ls,
                   const bool special,
                   const bool try_get,
                   bool &exist);
int report_write_ctx_status(const ObTransID &tx_id, const int status, int &tx_status);
int handle_tx_commit_timeout(ObTxDesc &tx, const int64_t delay);
int handle_tx_commit_result(const ObTransID &tx_id,
                            const int result,
                            const share::SCN commit_version = share::SCN());

ObTxCtxMgr &get_tx_ctx_mgr() { return tx_ctx_mgr_; }



int block_tx(bool &is_all_tx_cleaned_up);
// block tx and readonly request


int get_tx_ctx_mgr_stat(ObLSTxCtxMgrStat &tx_ctx_mgr_stat);


int iterate_all_observer_tx_stat(ObTxStatIterator &tx_stat_iter);

int iterate_tx_scheduler_stat(ObTxSchedulerStatIterator &tx_scheduler_stat_iter);

int gen_trans_id(ObTransID &trans_id);

TO_STRING_KV(K(is_inited_), KP(this));

private:
int init_tx_(ObTxDesc &tx, const uint32_t session_id);
int reinit_tx_(ObTxDesc &tx, const uint32_t session_id);
int start_tx_(ObTxDesc &tx);
int abort_tx_(ObTxDesc &tx, const int cause, bool cleanup = true);
void abort_tx__(ObTxDesc &tx, const bool cleanup);
int finalize_tx_(ObTxDesc &tx);
int find_write_state_after_savepoint_(ObTxDesc &tx,
                        ObTxWriteState *&part,
                        const ObTxSEQ scn);
int rollback_savepoint_(ObTxDesc &tx,
                        ObTxWriteState *part,
                        const ObTxSEQ savepoint,
                        int64_t expire_ts);
int create_tx_ctx_(const ObTxDesc &tx,
                   ObTxCtx *&ctx,
                   bool &exist);
int create_tx_ctx_(ObLS *ls,
                   const ObTxDesc &tx,
                   ObTxCtx *&ctx,
                   const bool special,
                   bool &exist);
int get_tx_ctx_(ObLS *ls,
                const ObTransID &tx_id,
                ObTxCtx *&ctx);

int get_tx_ctx_(const ObTransID &tx_id,
                ObTxCtx *&ctx);
int revert_tx_ctx_(ObLS *ls, ObTxCtx *ctx);
int revert_tx_ctx_(ObTxCtx *ctx);
int validate_snapshot_version_(const share::SCN snapshot,
                               const int64_t expire_ts,
                               ObLS &ls);
int abort_write_state_(const ObTxDesc &tx_desc);
int sync_acquire_local_snapshot_(ObTxDesc &tx,
                                 const int64_t expire_ts,
                                 share::SCN &snapshot);
int acquire_local_snapshot_(share::SCN &snapshot);
int acquire_local_snapshot_with_retry_(const int64_t expire_ts,
                                       share::SCN &snapshot);
int abort_write_ctx_(const ObTxDesc &tx_desc);

int update_max_read_ts_(const share::SCN ts);
int do_commit_tx_(ObTxDesc &tx,
                  const int64_t expire_ts,
                  ObITxCallback &cb,
                  share::SCN &commit_version);
int do_commit_tx_slowpath_(ObTxDesc &tx);
int register_commit_retry_task_(ObTxDesc &tx, int64_t max_delay = INT64_MAX);
int unregister_commit_retry_task_(ObTxDesc &tx);
int handle_tx_commit_result_(ObTxDesc &tx,
                             const int result,
                             const share::SCN commit_version = share::SCN());
int local_ls_commit_tx_(const ObTransID &tx_id,
                        const int64_t &expire_ts,
                        const int64_t &request_id,
                        const share::SCN commit_start_scn,
                        share::SCN &commit_version);
int get_tx_state_from_tx_table_(const ObTransID &tx_id,
                                int64_t &state,
                                share::SCN &commit_version)
{
  share::SCN recycle_scn;
  return get_tx_state_from_tx_table_(tx_id, state, commit_version, recycle_scn);
}
int get_tx_state_from_tx_table_(const ObTransID &tx_id,
                                int64_t &state,
                                share::SCN &commit_version,
                                share::SCN &recycle_scn);
OB_NOINLINE int gen_trans_id_(ObTransID &trans_id);
bool commit_need_retry_(const int ret);

private:
ObTxCtxMgr tx_ctx_mgr_;
void invalid_registered_snapshot_(ObTxDesc &tx);
void process_registered_snapshot_on_commit_(ObTxDesc &tx);
int rollback_tx_to_savepoint_(const ObTransID &tx_id,
                              const int64_t op_sn,
                              const ObTxSEQ savepoint,
                              const int64_t tx_seq_base,
                              const ObTxDesc &tx,
                              const ObTxSEQ from_scn,
                              int64_t expire_ts = -1);
int create_local_implicit_savepoint_(ObTxDesc &tx,
                                     ObTxSEQ &savepoint);
int create_global_implicit_savepoint_(ObTxDesc &tx,
                                      const ObTxParam &tx_param,
                                      ObTxSEQ &savepoint,
                                      const bool release);
int rollback_to_local_implicit_savepoint_(ObTxDesc &tx,
                                          const ObTxSEQ savepoint,
                                          const int64_t expire_ts);
int rollback_to_global_implicit_savepoint_(ObTxDesc &tx,
                                           const ObTxSEQ savepoint,
                                           const int64_t expire_ts,
                                           const ObTxCleanPolicy clean_policy);
int sync_rollback_to_savepoint_(ObTxCtx *part_ctx,
                                 const ObTxSEQ savepoint,
                                 const int64_t op_sn,
                                 const int64_t tx_seq_base,
                                 const int64_t expire_ts,
                                 const ObTxSEQ specified_from_scn);
void tx_post_terminate_(ObTxDesc &tx);
int start_epoch_(ObTxDesc &tx);
int tx_sanity_check_(ObTxDesc &tx);
MonotonicTs get_req_receive_mts_();
static bool common_retryable_error_(const int ret);
void direct_execute_commit_cb_(ObTxDesc &tx);
void adjust_tx_snapshot_(ObTxDesc &tx, ObTxReadSnapshot &snapshot);
// include tx api refacored for future
public:
#include "ob_tx_api.h"
