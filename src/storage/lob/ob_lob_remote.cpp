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

#define USING_LOG_PREFIX STORAGE

#include "ob_lob_remote.h"
#include "share/rc/ob_module_provider.h"
#include "storage/tx_storage/ob_ls_service.h"
#include "storage/lob/ob_lob_manager.h"
#include "storage/lob/ob_lob_iterator.h"

namespace oceanbase
{
namespace storage
{

/**********ObLobRemoteQueryCtx****************/

ObLobRemoteQueryCtx::~ObLobRemoteQueryCtx()
{
  if (OB_NOT_NULL(query_iter_)) {
    // Release the iterator in the same server runtime in which it was allocated.
    int ret = OB_SUCCESS;
    SERVER_MODULE_SCOPE {
      query_iter_->reset();
      OB_DELETE(ObLobQueryIter, "unused", query_iter_);
    }
    query_iter_ = nullptr;
  }
}

int ObLobRemoteQueryCtx::get_next_block(ObString &data)
{
  int ret = OB_SUCCESS;
  if (qtype_ != ObLobQueryArg::QueryType::READ) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get_next_block only valid for READ", K(ret), K(qtype_));
  } else if (OB_ISNULL(query_iter_) || OB_ISNULL(read_buf_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("query iter not inited", K(ret), KP(query_iter_), KP(read_buf_));
  } else {
    // The iterator reads local database storage and therefore runs in server runtime scope.
    SERVER_MODULE_SCOPE {
      ObString out;
      out.assign_buffer(read_buf_, read_buf_len_);
      if (OB_FAIL(query_iter_->get_next_row(out))) {
        if (OB_ITER_END != ret) {
          LOG_WARN("failed to get next lob query block", K(ret));
        }
      } else {
        data.assign_ptr(out.ptr(), out.length());
      }
    }
  }
  return ret;
}

/**********ObLobRemoteUtil****************/

int ObLobRemoteUtil::query(ObLobAccessParam& param, const ObLobQueryArg::QueryType qtype, const ObAddr &dst_addr, ObLobRemoteQueryCtx *&remote_ctx)
{
  int ret = OB_SUCCESS;
  UNUSED(dst_addr);
  if (param.from_rpc_ && ! param.enable_remote_retry_) {
    ret = OB_NOT_MASTER;
    LOG_WARN("call from rpc, but remote again", K(ret), K(dst_addr), K(param));
  } else if (OB_FAIL(remote_query_init_ctx(param, qtype, remote_ctx))) {
    LOG_WARN("fail to init remote query ctx", K(ret));
  } else {
    // Run the same local LOB query as ObLobQueryP::process, in process.
    
    SERVER_MODULE_SCOPE {
      ObLobManager *lob_mngr = share::g_mp->lob_manager();
      if (OB_ISNULL(lob_mngr)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("lob mngr is null", K(ret));
      } else if (OB_ISNULL(param.lob_locator_) || !param.lob_locator_->is_valid()) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("lob locator is invalid", K(ret), KP(param.lob_locator_));
      } else if (!param.lob_locator_->is_persist_lob()) {
        ret = OB_NOT_SUPPORTED;
        LOG_WARN("unsupport remote query non-persist lob.", K(ret), KPC(param.lob_locator_));
      } else {
        // build a fresh param exactly as the OB_LOB_QUERY processor did (from_rpc_ = true)
        ObLobAccessParam query_param;
        query_param.scan_backward_ = param.scan_backward_;
        query_param.from_rpc_ = true;
        query_param.enable_remote_retry_ = param.enable_remote_retry_;
        const int64_t timeout = param.timeout_;
        if (OB_FAIL(lob_mngr->build_lob_param(query_param, *param.allocator_, param.coll_type_,
            param.offset_, param.len_, timeout, *param.lob_locator_))) {
          LOG_WARN("failed to build lob param", K(ret));
        } else if (qtype == ObLobQueryArg::QueryType::READ) {
          ObLobQueryIter *iter = nullptr;
          if (OB_FAIL(lob_mngr->query(query_param, iter))) {
            LOG_WARN("failed to query lob.", K(ret), K(query_param));
          } else {
            remote_ctx->query_iter_ = iter;
          }
        } else if (qtype == ObLobQueryArg::QueryType::GET_LENGTH) {
          uint64_t len = 0;
          if (OB_FAIL(lob_mngr->getlength(query_param, len))) {
            LOG_WARN("failed to getlength lob.", K(ret), K(query_param));
          } else {
            remote_ctx->length_ = len;
          }
        } else {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("invalid arg qtype.", K(ret), K(qtype));
        }
      }
    }
    if (OB_SUCC(ret)) {
      LOG_TRACE("remote(in-process) query start", KPC(param.lob_data_), K(dst_addr), K(qtype), K(lbt()));
    }
  }
  if (OB_FAIL(ret) && OB_NOT_NULL(remote_ctx)) {
    remote_ctx->~ObLobRemoteQueryCtx();
    remote_ctx = nullptr;
  }
  return ret;
}

int ObLobRemoteUtil::remote_query_init_ctx(ObLobAccessParam &param, const ObLobQueryArg::QueryType qtype, ObLobRemoteQueryCtx *&ctx)
{
  int ret = OB_SUCCESS;
  ObLobRemoteQueryCtx *remote_ctx = nullptr;
  if (ctx != nullptr) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ctx is null null", K(ret), K(param), KP(ctx), K(qtype));
  } else if (OB_ISNULL(param.lob_locator_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("lob locator is null", K(ret), K(param));
  } else if (OB_ISNULL(param.allocator_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("allocator is null", K(ret), K(param));
  } else if (OB_ISNULL(remote_ctx = OB_NEWx(ObLobRemoteQueryCtx, param.allocator_))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to alloc lob remote query ctx", K(ret), K(param));
  } else {
    
    remote_ctx->qtype_ = qtype;
    if (qtype == ObLobQueryArg::QueryType::READ) {
      // output buffer for ObLobQueryIter::get_next_row, sized as the OB_LOB_QUERY processor did.
      const int64_t buf_len = ObLobQueryArg::OB_LOB_QUERY_BUFFER_LEN - sizeof(ObLobQueryBlock);
      char *buf = reinterpret_cast<char*>(param.allocator_->alloc(buf_len));
      if (OB_ISNULL(buf)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("fail to alloc read buffer", K(ret), K(buf_len));
      } else {
        remote_ctx->read_buf_ = buf;
        remote_ctx->read_buf_len_ = buf_len;
      }
    }
    if (OB_SUCC(ret)) {
      ctx = remote_ctx;
    }
  }

  if (OB_FAIL(ret) && OB_NOT_NULL(remote_ctx)) {
    remote_ctx->~ObLobRemoteQueryCtx();
    remote_ctx = nullptr;
  }
  return ret;
}

} // storage
} // oceanbase
