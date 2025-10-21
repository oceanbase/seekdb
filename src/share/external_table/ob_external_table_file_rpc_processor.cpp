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

#define USING_LOG_PREFIX SQL
#include "share/external_table/ob_external_table_file_mgr.h"
#include "share/external_table/ob_external_table_file_rpc_processor.h"
namespace oceanbase
{
namespace share
{

int ObFlushExternalTableKVCacheP::process() 
{
  int ret = OB_SUCCESS;
  ObFlushExternalTableFileCacheReq &req = arg_;
  ObFlushExternalTableFileCacheRes &res = result_;
  if (OB_FAIL(ObExternalTableFileManager::get_instance().flush_cache(req.tenant_id_, req.table_id_, req.partition_id_))) {
    LOG_WARN("erase kvcache result failed", K(ret));
  }
  res.rcode_.rcode_ = ret;
  return OB_SUCCESS;
}

int ObAsyncLoadExternalTableFileListP::process() 
{
  int ret = OB_SUCCESS;
  ObLoadExternalFileListReq &req = arg_;
  ObLoadExternalFileListRes &res = result_;
  ObSEArray<ObString, 16> file_urls;
  ObString access_info;
  ObArenaAllocator allocator;
  if (OB_FAIL(ObExternalTableFileManager::get_instance().get_external_file_list_on_device(req.location_,
                                                                                          req.pattern_,
                                                                                          req.regexp_vars_,
                                                                                          file_urls,
                                                                                          res.file_sizes_,
                                                                                          access_info,
                                                                                          allocator))) {
    LOG_WARN("get external table file on device failed", K(ret));
  }
  for (int64_t i =0 ; OB_SUCC(ret) && i < file_urls.count(); i++) {
    ObString tmp;
    OZ(ob_write_string(res.get_alloc(), file_urls.at(i), tmp));
    OZ(res.file_urls_.push_back(tmp));
  }
  res.rcode_.rcode_ = ret;
  LOG_DEBUG("get external table file", K(ret), K(req.location_), K(req.pattern_), K(file_urls), K(res.file_urls_));
  return ret;
}

void ObRpcAsyncLoadExternalTableFileCallBack::on_timeout()
{
  int ret = OB_TIMEOUT;
  int64_t current_ts = ObTimeUtility::current_time();
  int64_t timeout_ts = get_send_ts() + timeout_;
  if (current_ts < timeout_ts) {
    LOG_DEBUG("rpc return OB_TIMEOUT before actual timeout, change error code to OB_RPC_CONNECT_ERROR", KR(ret),
              K(timeout_ts), K(current_ts));
    ret = OB_RPC_CONNECT_ERROR;
  }
  LOG_WARN("async task timeout", KR(ret));
  result_.rcode_.rcode_ = ret;
  context_->inc_concurrency_limit_with_signal();
}

void ObRpcAsyncLoadExternalTableFileCallBack::on_invalid()
{
  int ret = OB_SUCCESS;
  // a valid packet on protocol level, but can't decode it.
  result_.rcode_.rcode_ = OB_INVALID_ERROR;
  LOG_WARN("async task invalid", K(result_.rcode_.rcode_));
  context_->inc_concurrency_limit_with_signal();
}

int ObRpcAsyncLoadExternalTableFileCallBack::process()
{
  int ret = OB_SUCCESS;
  LOG_DEBUG("async access callback process", K_(result));
  if (OB_FAIL(get_rcode())) {
    result_.rcode_.rcode_ = get_rcode();
    LOG_WARN("async rpc execution failed", K(get_rcode()), K_(result));
  }
  context_->inc_concurrency_limit_with_signal();
  return ret;
}

oceanbase::rpc::frame::ObReqTransport::AsyncCB *ObRpcAsyncLoadExternalTableFileCallBack::clone(
    const oceanbase::rpc::frame::SPAlloc &alloc) const {
  UNUSED(alloc);
  return const_cast<rpc::frame::ObReqTransport::AsyncCB *>(
      static_cast<const rpc::frame::ObReqTransport::AsyncCB * const>(this));
}

void ObRpcAsyncFlushExternalTableKVCacheCallBack::on_timeout()
{
  int ret = OB_TIMEOUT;
  int64_t current_ts = ObTimeUtility::current_time();
  int64_t timeout_ts = get_send_ts() + timeout_;
  if (current_ts < timeout_ts) {
    LOG_DEBUG("rpc return OB_TIMEOUT before actual timeout, change error code to OB_RPC_CONNECT_ERROR", KR(ret),
              K(timeout_ts), K(current_ts));
    ret = OB_RPC_CONNECT_ERROR;
  }
  LOG_WARN("async task timeout", KR(ret));
  result_.rcode_.rcode_ = ret;
  context_->inc_concurrency_limit_with_signal();
}


void ObRpcAsyncFlushExternalTableKVCacheCallBack::on_invalid()
{
  int ret = OB_SUCCESS;
  // a valid packet on protocol level, but can't decode it.
  result_.rcode_.rcode_ = OB_INVALID_ERROR;
  LOG_WARN("async task invalid", K(result_.rcode_.rcode_));
  context_->inc_concurrency_limit_with_signal();
}

int ObRpcAsyncFlushExternalTableKVCacheCallBack::process()
{
  int ret = OB_SUCCESS;
  LOG_DEBUG("async access callback process", K_(result));
  if (OB_FAIL(get_rcode())) {
    result_.rcode_.rcode_ = get_rcode();
    // we need to clear op results because they are not decoded from das async rpc due to rpc error.
    LOG_WARN("async rpc execution failed", K(get_rcode()), K_(result));
  }
  context_->inc_concurrency_limit_with_signal();
  return ret;
}

oceanbase::rpc::frame::ObReqTransport::AsyncCB *ObRpcAsyncFlushExternalTableKVCacheCallBack::clone(
    const oceanbase::rpc::frame::SPAlloc &alloc) const {
  UNUSED(alloc);
  return const_cast<rpc::frame::ObReqTransport::AsyncCB *>(
      static_cast<const rpc::frame::ObReqTransport::AsyncCB * const>(this));
}


}  // namespace share
}  // namespace oceanbase
