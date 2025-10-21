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

#define USING_LOG_PREFIX SQL_DTL
#include "ob_dtl_buf_allocator.h"
#include "sql/dtl/ob_dtl.h"
#include "sql/dtl/ob_dtl_basic_channel.h"

using namespace oceanbase::common;

namespace oceanbase {
namespace sql {
namespace dtl {

ObDtlLinkedBuffer *ObDtlBufAllocator::alloc_buf(ObDtlBasicChannel &ch, const int64_t payload_size)
{
  int ret = OB_SUCCESS;
  ObDtlLinkedBuffer *buf = nullptr;
  int64_t alloc_size = max(sys_buffer_size_, payload_size);
  ObDtlTenantMemManager *tenant_mem_mgr = DTL.get_dfc_server().get_tenant_mem_manager(tenant_id_);
  if (nullptr == tenant_mem_mgr) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("tenant_mem_mgr is null", K(ret), K(tenant_id_));
  } else {
    int64_t hash_val = ch.get_hash_val();
    buf = tenant_mem_mgr->alloc(hash_val, alloc_size);
    if (nullptr != buf) {
      alloc_buffer_cnt_++;
      ch.alloc_buffer_count();
      buf->set_timeout_ts(timeout_ts_);
      buf->set_size(alloc_size);
    }
  }
  LOG_DEBUG("allocate memory", K(ret), KP(ch.get_id()), K(buf), K(alloc_size));
  return buf;
}

void ObDtlBufAllocator::free_buf(ObDtlBasicChannel &ch, ObDtlLinkedBuffer *&buf)
{
  int ret = OB_SUCCESS;
  ObDtlTenantMemManager *tenant_mem_mgr = DTL.get_dfc_server().get_tenant_mem_manager(tenant_id_);
  if (nullptr == tenant_mem_mgr) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("tenant_mem_mgr is null", K(lbt()), K(tenant_id_), K(ret));
  } else if (OB_FAIL(tenant_mem_mgr->free(buf))) {
    LOG_WARN("failed to free buffer", K(ret), K(lbt()), K(tenant_id_));
  } else if (nullptr != buf) {
    free_buffer_cnt_++;
    ch.free_buffer_count();
    buf = nullptr;
  }
  if (nullptr != buf) {
    LOG_ERROR("fail to free dtl linked buffer", K(ret));
  }
  LOG_DEBUG("free memory", K(ret), K(buf), KP(buf), K(free_buffer_cnt_), K(alloc_buffer_cnt_));
}

}
}
}

