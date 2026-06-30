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

#ifndef OCEANBASE_TRANSACTION_OB_GTI_RPC_
#define OCEANBASE_TRANSACTION_OB_GTI_RPC_

#include "observer/ob_server_struct.h"
#include "rpc/frame/ob_result_code.h"
#include "storage/tx/ob_gti_source.h"

namespace oceanbase
{

namespace transaction
{

class ObGtiRequest
{
  OB_UNIS_VERSION(1);
public:
  ObGtiRequest() : range_(0) {}
  ~ObGtiRequest() {}
  int init(const int64_t range);
  bool is_valid() const;
public:
  
  int64_t get_range() const { return range_; }
  TO_STRING_KV(K_(range));
private:
  int64_t range_;
};

} //transaction

namespace obcall
{

class ObGtiRpcResult
{
  OB_UNIS_VERSION(1);
public:
  ObGtiRpcResult() { reset(); }
  virtual ~ObGtiRpcResult() {}
  int init(const int status, const int64_t start_id, const int64_t end_id);
  
  int get_status() const { return status_; }
  int64_t get_start_id() const { return start_id_; }
  int64_t get_end_id() const { return end_id_; }
  void reset();
  bool is_valid() const;
  TO_STRING_KV(K_(status), K_(start_id), K_(end_id));
public:
  static const int64_t OB_GTI_RPC_TIMEOUT = 1 * 1000 * 1000;
private:
  int status_;
  int64_t start_id_;
  int64_t end_id_;
};

class ObGtiRPCCB
{
public:
  ObGtiRPCCB() : is_inited_(false), gti_source_(NULL) {}
  ~ObGtiRPCCB() {}
  int init(transaction::ObGtiSource *gti_source)
  {
    int ret = common::OB_SUCCESS;
    if (is_inited_) {
      ret = OB_INIT_TWICE;
      TRANS_LOG(WARN, "ObGtiRPCCB inited twice", KR(ret));
    } else if (NULL == gti_source) {
      ret = common::OB_INVALID_ARGUMENT;
      TRANS_LOG(WARN, "invalid argument", KR(ret), KP(gti_source));
    } else {
      gti_source_ = gti_source;
      is_inited_ = true;
    }
    return ret;
  }
  int process(const obcall::ObGtiRpcResult &result, const common::ObAddr &dst,
              rpc::frame::ObResultCode &rcode)
  {
    return process_(result, dst, rcode);
  }
  
private:
  int process_(const obcall::ObGtiRpcResult &result, const common::ObAddr &dst,
               rpc::frame::ObResultCode &rcode)
  {
    int ret = OB_SUCCESS;
    int status = OB_SUCCESS;
    bool update = false;

    if (!is_inited_) {
      TRANS_LOG(WARN, "ObGtiRPCCB not inited");
      ret = OB_NOT_INIT;
    } else if (!true) {
      TRANS_LOG(WARN, "invalid argument", K(dst));
      ret = OB_ERR_UNEXPECTED;
    } else {
      MOD_SCOPE {
        if (OB_SUCCESS != rcode.rcode_) {
          status = rcode.rcode_;
          TRANS_LOG(WARN, "gti rpc error", K(rcode), K(dst));
          if (OB_NOT_MASTER == status
              || OB_TENANT_NOT_IN_SERVER == status) {
            if (OB_FAIL(gti_source_->refresh_gti_location())) {
            }
          }
        } else {
          status = result.get_status();
          if (OB_SUCCESS == status) {
            if (OB_FAIL(gti_source_->update_trans_id(result.get_start_id(),
                                                            result.get_end_id()))) {
            }
          } else if (OB_NOT_MASTER == status) {
            if (OB_FAIL(gti_source_->refresh_gti_location())) {
            }
          }
          TRANS_LOG(INFO, "gti request callback", KR(ret), K(result), K(rcode));
        }
      } else {
        TRANS_LOG(WARN, "tenant switch fail", K(dst));
      }
    }
    return ret;
  }
  bool is_inited_;
  transaction::ObGtiSource *gti_source_;
};

} // obcall

namespace transaction
{

class ObGtiRequestRpc
{
public:
  ObGtiRequestRpc() : is_inited_(false), is_running_(false) {}
  ~ObGtiRequestRpc() { destroy(); }
  int init(const common::ObAddr &self, ObGtiSource *gti_source);
  int start();
  int stop();
  int wait();
  void destroy();
public:
  int post(const ObGtiRequest &msg);
private:
  bool is_inited_;
  bool is_running_;
  obcall::ObGtiRPCCB gti_request_cb_;
  common::ObAddr self_;
};

} // transaction

} // oceanbase

#endif // OCEANBASE_TRANSACTION_OB_GTI_RPC_
