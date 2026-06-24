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

#ifndef OCEANBASE_TRANSACTION_OB_GTS_RPC_
#define OCEANBASE_TRANSACTION_OB_GTS_RPC_

#include "common/ob_queue_thread.h"
#include "lib/utility/ob_unify_serialize.h"
#include "lib/utility/utility.h"
#include "share/ob_define.h"
#include "rpc/frame/ob_result_code.h"
#include "share/ob_rpc_struct.h"
#include "storage/tx/ob_gts_msg.h"
#include "storage/tx/ob_ts_worker.h"
#include "storage/tx/ob_ts_response_handler.h"
#include "storage/tx/ob_ts_mgr.h"
#include "share/config/ob_server_config.h"
#include "observer/ob_server_struct.h"

namespace oceanbase
{

namespace transaction
{
class ObTsMgr;
class ObTsWorker;
}

namespace obcall
{
class ObGtsRpcResult
{
  OB_UNIS_VERSION(1);
public:
  ObGtsRpcResult() { reset(); }
  virtual ~ObGtsRpcResult() {}
  int init(const int status,
           const transaction::MonotonicTs srr, const int64_t gts_start, const int64_t gts_end);
  
  int get_status() const { return status_; }
  transaction::MonotonicTs get_srr() const { return srr_; }
  int64_t get_gts_start() const { return gts_start_; }
  int64_t get_gts_end() const { return gts_end_; }
  void reset();
  bool is_valid() const;
  TO_STRING_KV(K_(status), K_(srr), K_(gts_start), K_(gts_end));
public:
  static const int64_t OB_GTS_RPC_TIMEOUT = 1 * 1000 * 1000;
private:
  int status_;
  transaction::MonotonicTs srr_;
  int64_t gts_start_;
  int64_t gts_end_;
};

class ObGtsRPCCB
{
public:
  ObGtsRPCCB() : is_inited_(false), ts_mgr_(NULL), ts_worker_(NULL) {}
  ~ObGtsRPCCB() {}
  int init(transaction::ObTsMgr *ts_mgr,
           transaction::ObTsWorker *ts_worker)
  {
    int ret = common::OB_SUCCESS;
    if (is_inited_) {
      ret = OB_INIT_TWICE;
      TRANS_LOG(WARN, "ObGtsRPCCB inited twice", KR(ret));
    } else if (NULL == ts_mgr || NULL == ts_worker) {
      ret = common::OB_INVALID_ARGUMENT;
      TRANS_LOG(WARN, "invalid argument", KR(ret), KP(ts_mgr), KP(ts_worker));
    } else {
      ts_mgr_ = ts_mgr;
      ts_worker_ = ts_worker;
      is_inited_ = true;
    }
    return ret;
  }
  int process(const obcall::ObGtsRpcResult &result, const common::ObAddr &dst,
              rpc::frame::ObResultCode &rcode)
  {
    return process_(result, dst, rcode);
  }
  
private:
  int process_(const obcall::ObGtsRpcResult &result, const common::ObAddr &dst,
               rpc::frame::ObResultCode &rcode)
  {
    int ret = OB_SUCCESS;
    int status = OB_SUCCESS;
    bool update = false;

    if (!is_inited_) {
      TRANS_LOG(WARN, "ObGtsRPCCB not inited");
      ret = OB_NOT_INIT;
    } else if (!true) {
      TRANS_LOG(WARN, "invalid argument", K(dst));
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_SUCCESS != rcode.rcode_) {
      status = rcode.rcode_;
      TRANS_LOG(WARN, "gts rpc error", K(rcode), K(dst));
      if (EXECUTE_COUNT_PER_SEC(16)) {
        TRANS_LOG(INFO, "get gts need refresh gts location", K(status), K(result));
      }
      if (NULL == ts_mgr_) {
        ret = OB_ERR_UNEXPECTED;
        TRANS_LOG(WARN, "gts local cache mgr is NULL", K(ret));
      } else if (OB_FAIL(ts_mgr_->refresh_gts_location())) {
        TRANS_LOG(WARN, "refresh gts location fail", K(ret));
      } else {
        // do nothing
      }
    } else {
      status = result.get_status();
      if (OB_SUCCESS == status) {
        if (NULL == ts_mgr_) {
          ret = OB_ERR_UNEXPECTED;
          TRANS_LOG(WARN, "gts local cache mgr is NULL", KR(ret));
        } else if (NULL == ts_worker_) {
          ret = OB_ERR_UNEXPECTED;
          TRANS_LOG(WARN, "gts worker is NULL", KR(ret));
        } else if (OB_FAIL(ts_mgr_->update_gts(result.get_srr(),
                                               result.get_gts_start(),
                                               transaction::TS_SOURCE_GTS,
                                               update))) {
        } else if (!update) {
          if (EXECUTE_COUNT_PER_SEC(16)) {
            TRANS_LOG(INFO, "gts local cache not updated", K(result));
          }
        } else {
          transaction::ObTsResponseTask *task = NULL;
          for (int64_t i = 0; OB_SUCC(ret) && i < transaction::ObGtsSource::TOTAL_GTS_QUEUE_COUNT; ++i) {
            if (NULL == (task = transaction::ObTsResponseTaskFactory::alloc())) {
              ret = OB_ALLOCATE_MEMORY_FAILED;
              TRANS_LOG(ERROR, "alloc memory failed", KR(ret), KP(task));
            } else {
              if (OB_FAIL(task->init(i, ts_mgr_, transaction::TS_SOURCE_GTS))) {
                TRANS_LOG(WARN, "gts task init error", KR(ret), KP(task), K(i), K(result));
              } else if (OB_FAIL(ts_worker_->push_task(task))) {
                TRANS_LOG(WARN, "push gts task failed", KR(ret), KP(task), K(result));
              } else {
                TRANS_LOG(DEBUG, "push gts task success", KP(task), K(result));
              }
              if (OB_SUCCESS != ret) {
                transaction::ObTsResponseTaskFactory::free(task);
                task = NULL;
              }
            }
          }
        }
      }
      TRANS_LOG(DEBUG, "gts request callback", KR(ret), K(result), K(rcode));
    }
    return ret;
  }
  bool is_inited_;
  transaction::ObTsMgr *ts_mgr_;
  transaction::ObTsWorker *ts_worker_;
};

} // obcall

namespace transaction
{

class ObIGtsRequestRpc
{
public:
  ObIGtsRequestRpc() {}
  virtual ~ObIGtsRequestRpc() {}
  virtual int start() = 0;
  virtual int stop() = 0;
  virtual int wait() = 0;
  virtual void destroy() = 0;
public:
  virtual int post(const common::ObAddr &server,
      const ObGtsRequest &msg) = 0;
};

class ObGtsRequestRpc : public ObIGtsRequestRpc
{
public:
  ObGtsRequestRpc() : is_inited_(false), is_running_(false), ts_mgr_(NULL) {}
  ~ObGtsRequestRpc() { destroy(); }
  int init(const common::ObAddr &self,
           transaction::ObTsMgr *ts_mgr,
           transaction::ObTsWorker *ts_worker);
  int start();
  int stop();
  int wait();
  void destroy();
public:
  int post(const common::ObAddr &server, const ObGtsRequest &msg);
private:
  bool is_inited_;
  bool is_running_;
  obcall::ObGtsRPCCB gts_request_cb_;
  common::ObAddr self_;
  transaction::ObTsMgr *ts_mgr_;
};

} // transaction

} // oceanbase

#endif // OCEANBASE_TRANSACTION_OB_GTS_RPC_
