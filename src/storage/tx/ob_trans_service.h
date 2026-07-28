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

#ifndef OCEANBASE_TRANSACTION_OB_TRANS_SERVICE_
#define OCEANBASE_TRANSACTION_OB_TRANS_SERVICE_

#include <stdlib.h>
#include <time.h>
#include "share/ob_define.h"
#include "share/ob_errno.h"
#include "storage/memtable/ob_memtable_interface.h"
#include "storage/memtable/ob_memtable_context.h"
#include "share/schema/ob_multi_version_schema_service.h"
#include "share/ob_light_hashmap.h"
#include "sql/ob_end_trans_callback.h"
#include "lib/utility/utility.h"
#include "ob_trans_define.h"
#include "ob_trans_timer.h"
#include "ob_tx_timestamp_waiter.h"
#include "ob_trans_ctx_mgr.h"
#include "ob_trans_memory_stat.h"
#include "ob_trans_event.h"
#include "ob_gti_source.h"
#include "ob_tx_version_mgr.h"
#include "lib/utility/ob_tracepoint.h"
#include "lib/container/ob_iarray.h"
#include "observer/ob_server_struct.h"
#include "common/storage/ob_sequence.h"
#include "ob_tx_elr_util.h"
#include "src/storage/tx_storage/ob_tx_leak_checker.h"

#define MAX_REDO_SYNC_TASK_COUNT 10

namespace oceanbase
{

namespace common
{
class ObAddr;
}

namespace storage
{
class ObIMemtable;
}

namespace memtable
{
class ObMemtableCtx;
}

namespace transaction
{
class ObTsMgr;
class ObTimestampService;
class ObITxLogParam;

// iterate transaction module memory usage status
typedef common::ObSimpleIterator<ObTransMemoryStat,
    ObModIds::OB_TRANS_VIRTUAL_TABLE_MEMORY_STAT, 16> ObTransMemStatIterator;
class KillTransArg
{
public:
  KillTransArg(const bool graceful, const bool ignore_ro_trans = true)
    : graceful_(graceful), ignore_ro_trans_(ignore_ro_trans) {}
  ~KillTransArg() {}
  TO_STRING_KV(K_(graceful), K_(ignore_ro_trans));
public:
  bool graceful_;
  bool ignore_ro_trans_;
};

enum class ObThreadLocalTransCtxState : int
{
  OB_THREAD_LOCAL_CTX_INVALID,
  OB_THREAD_LOCAL_CTX_READY,
  OB_THREAD_LOCAL_CTX_RUNNING,
  OB_THREAD_LOCAL_CTX_BLOCKING
};

class ObThreadLocalTransCtx
{
public:
  static const int64_t MAX_BIG_TRANS_TASK = 100 * 1000;
public:
  ObThreadLocalTransCtx() : state_(ObThreadLocalTransCtxState::OB_THREAD_LOCAL_CTX_READY) {}
  ~ObThreadLocalTransCtx() { destroy(); }
  void reset()
  {
    state_ = ObThreadLocalTransCtxState::OB_THREAD_LOCAL_CTX_INVALID;
  }
  void destroy();
public:
  memtable::ObMemtableCtx memtable_ctx_;
  ObThreadLocalTransCtxState state_;
} CACHE_ALIGNED;

class ObTransService : public common::ObLinkQueueThreadPool
{
public:
  ObTransService();
  virtual ~ObTransService() { destroy(); }
  static int server_module_init(ObTransService* &trans_service);
  int init(const ObAddr &self,
           ObIGtiSource *gti_source,
           ObTsMgr *ts_mgr,
           share::schema::ObMultiVersionSchemaService *schema_service);
  int start();
  void stop();
  void wait() { wait_(); }
  int wait_();
  void destroy();
  int push(LinkTask *task);
  virtual void handle(LinkTask *task) override;
public:
  ObReadOnlyTxChecker &get_read_tx_checker() { return read_only_checker_; }
  int64_t get_unique_seq()
  { return ATOMIC_AAF(&tx_debug_seq_, 1); }
  int calculate_trans_cost(const ObTransID &tid, uint64_t &cost);
  int get_min_uncommit_prepare_version(share::SCN &min_prepare_version);
  //get the memory used condition of transaction module
  int get_trans_start_session_id(const ObTransID &tx_id, uint32_t &session_id);
  int remove_callback_for_uncommited_txn(const memtable::ObMemtableSet *memtable_set);
  
  const common::ObAddr &get_server() { return self_; }
  ObTransTimer &get_trans_timer() { return timer_; }
  common::ObMySQLProxy *get_mysql_proxy() { return GCTX.sql_proxy_; }
  bool is_running() const { return is_running_; }
  ObTsMgr *get_ts_mgr() { return ts_mgr_; }
  ObTxTimestampWaiter &get_tx_timestamp_waiter() { return tx_timestamp_waiter_; }
  share::schema::ObMultiVersionSchemaService *get_schema_service() { return schema_service_; }
  ObTxVersionMgr &get_tx_version_mgr() { return tx_version_mgr_; }
  int register_mds_into_tx(ObTxDesc &tx_desc,
                           const ObTxDataSourceType &type,
                           const char *buf,
                           const int64_t buf_len,
                           const ObRegisterMdsFlag &register_flag = ObRegisterMdsFlag(),
                           const transaction::ObTxSEQ seq_no = transaction::ObTxSEQ());
  ObTxELRUtil &get_tx_elr_util() { return elr_util_; }
#ifdef ENABLE_DEBUG_LOG
  transaction::ObDefensiveCheckMgr *get_defensive_check_mgr() { return defensive_check_mgr_; }
#endif
private:
  void check_env_();
  int register_mds_into_ctx_(ObTxDesc &tx_desc,
                             const ObTxDataSourceType &type,
                             const char *buf,
                             const int64_t buf_len,
                             const transaction::ObTxSEQ seq_no,
                             const ObRegisterMdsFlag &register_flag);
private:
  int handle_batch_msg_(const int type, const char *buf, const int32_t size);
public:
  int get_max_commit_version(share::SCN &commit_version) const;
  #include "ob_trans_service_v4.h"
private:
  static const int64_t END_STMT_MORE_TIME_US = 100 * 1000;
  // max task count in message process queue
  static const int64_t MAX_MSG_TASK_CNT = 1000 * 1000;
  static const int64_t MSG_TASK_CNT_PER_GB = 50 * 1000;
  static const int64_t MAX_BIG_TRANS_WORKER = 8;
  static const int64_t MAX_BIG_TRANS_TASK = 100 * 1000;
  // max time bias between any two machine
  static const int64_t MAX_TIME_INTERVAL_BETWEEN_MACHINE_US = 200 * 1000;
  static const int64_t CHANGING_LEADER_TXN_PER_ROUND = 200;
public:
  ObIGtiSource *gti_source_;
  ObGtiSource gti_source_def_;
protected:
  bool is_inited_;
  bool is_running_;
  // for ObTransID
  common::ObAddr self_;
  
  // transaction timer
  ObTransTimer timer_;
  ObTxTimestampWaiter tx_timestamp_waiter_;
  ObTxVersionMgr tx_version_mgr_;
protected:
  // the adapter between transaction and clog
  share::schema::ObMultiVersionSchemaService *schema_service_;
private:
  ObTsMgr *ts_mgr_;
  // account task qeuue's inqueue and dequeue
  uint32_t input_queue_count_;
  uint32_t output_queue_count_;
#ifdef ENABLE_DEBUG_LOG
  transaction::ObDefensiveCheckMgr *defensive_check_mgr_;
#endif
  // txDesc's manager
  ObTxDescMgr tx_desc_mgr_;

  ObTxELRUtil elr_util_;
  // tenant level atomic inc seq, just for debug
  int64_t tx_debug_seq_;
  ObReadOnlyTxChecker read_only_checker_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObTransService);
};

} // transaction
} // oceanbase

#endif // OCEANBASE_TRANSACTION_OB_TRANS_SERVICE_
