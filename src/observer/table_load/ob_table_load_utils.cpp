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

#define USING_LOG_PREFIX SERVER

#include "observer/table_load/ob_table_load_utils.h"
#include "observer/ob_server.h"

namespace oceanbase
{
namespace observer
{
using namespace blocksstable;
using namespace common;
using namespace table;
using namespace observer;
using namespace share::schema;


int ObTableLoadUtils::deep_copy(const ObString &src, ObString &dest, ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ob_write_string(allocator, src, dest))) {
    LOG_WARN("fail to deep copy str", KR(ret));
  }
  return ret;
}

int ObTableLoadUtils::deep_copy(const ObObj &src, ObObj &dest, ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  if (!src.need_deep_copy()) {
    dest = src;
  } else {
    const int64_t size = src.get_deep_copy_size();
    char *buf = nullptr;
    int64_t pos = 0;
    if (OB_ISNULL(buf = static_cast<char *>(allocator.alloc(size)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("fail to allocate memory", KR(ret));
    } else {
      if (OB_FAIL(dest.deep_copy(src, buf, size, pos))) {
        LOG_WARN("fail to deep copy obj", KR(ret), K(src));
      }
      if (OB_FAIL(ret)) {
        allocator.free(buf);
      }
    }
  }
  return ret;
}


int ObTableLoadUtils::deep_copy(const ObStoreRowkey &src, ObStoreRowkey &dest, ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(src.deep_copy(dest, allocator))) {
    LOG_WARN("fail to deep copy store rowkey", KR(ret), K(src));
  }
  return ret;
}




int ObTableLoadUtils::deep_copy(const ObDatumRowkey &src, ObDatumRowkey &dest, ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(src.deep_copy(dest, allocator))) {
    LOG_WARN("fail to deep copy datum rowkey", KR(ret), K(src));
  } else if (OB_FAIL(deep_copy(src.store_rowkey_, dest.store_rowkey_, allocator))) {
    LOG_WARN("fail to deep copy store rowkey", KR(ret), K(src));
  }
  return ret;
}


int ObTableLoadUtils::deep_copy(const sql::ObSQLSessionInfo &src, sql::ObSQLSessionInfo &dest, ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  char *buf = nullptr;
  int64_t buf_size = src.get_serialize_size();
  int64_t data_len = 0;
  int64_t pos = 0;
  if (OB_ISNULL(buf = static_cast<char *>(allocator.alloc(buf_size)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to allocate buffer", KR(ret), K(buf_size));
  } else if (OB_FAIL(src.serialize(buf, buf_size, pos))) {
    LOG_WARN("serialize session info failed", KR(ret));
  } else {
    data_len = pos;
    pos = 0;
  }
  if (OB_SUCC(ret)) {
    if (OB_FAIL(dest.deserialize(buf, data_len, pos))) {
      LOG_WARN("deserialize session info failed", KR(ret));
    }
  }
  return ret;
}

bool ObTableLoadUtils::is_local_addr(const ObAddr &addr)
{
  return (ObServer::get_instance().get_self() == addr);
}

int ObTableLoadUtils::create_session_info(sql::ObSQLSessionInfo *&session_info, sql::ObFreeSessionCtx &free_session_ctx)
{
  int ret = OB_SUCCESS;
  const uint64_t tenant_id = MTL_ID();
  uint32_t sid = sql::ObSQLSessionInfo::INVALID_SESSID;
  uint64_t proxy_sid = 0;
  if (OB_FAIL(GCTX.session_mgr_->create_sessid(sid))) {
    LOG_WARN("alloc session id failed", KR(ret));
  } else if (OB_FAIL(GCTX.session_mgr_->create_session(
              tenant_id, sid, proxy_sid, ObTimeUtility::current_time(), session_info))) {
    GCTX.session_mgr_->mark_sessid_unused(sid);
    session_info = nullptr;
    LOG_WARN("create session failed", KR(ret), K(sid));
  } else {
    free_session_ctx.sessid_ = sid;
    free_session_ctx.proxy_sessid_ = proxy_sid;
  }
  return ret;
}

void ObTableLoadUtils::free_session_info(sql::ObSQLSessionInfo *session_info, const sql::ObFreeSessionCtx &free_session_ctx)
{
  int ret = OB_SUCCESS;
  if (session_info == nullptr || free_session_ctx.sessid_ == sql::ObSQLSessionInfo::INVALID_SESSID) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", KR(ret), KP(session_info), K(free_session_ctx));
  } else {
    session_info->set_session_sleep();
    GCTX.session_mgr_->revert_session(session_info);
    GCTX.session_mgr_->free_session(free_session_ctx);
    GCTX.session_mgr_->mark_sessid_unused(free_session_ctx.sessid_);
    session_info = nullptr;
  }
}



}  // namespace observer
}  // namespace oceanbase
