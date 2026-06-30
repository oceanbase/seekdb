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

#include "observer/virtual_table/ob_all_virtual_dtl_first_cached_buffer.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;
using namespace oceanbase::sql::dtl;
using namespace oceanbase::observer;
using namespace oceanbase::share;

ObAllVirtualDtlFirstCachedBufferIterator::ObAllVirtualDtlFirstCachedBufferIterator(ObArenaAllocator *allocator) :
  done_(false),
  cur_buffer_idx_(0),
  iter_allocator_(allocator),
  buffer_infos_()
{}

ObAllVirtualDtlFirstCachedBufferIterator::~ObAllVirtualDtlFirstCachedBufferIterator()
{
  destroy();
}

void ObAllVirtualDtlFirstCachedBufferIterator::reset()
{
  
  done_ = false;
  cur_buffer_idx_ = 0;
  buffer_infos_.reset();
  iter_allocator_->reuse();
}

void ObAllVirtualDtlFirstCachedBufferIterator::destroy()
{
  buffer_infos_.reset();
  iter_allocator_ = nullptr;
}

int ObAllVirtualDtlFirstCachedBufferIterator::prepare_tenants()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(NULL == GCTX.omt_)) {
    ret = OB_NOT_INIT;
    SERVER_LOG(WARN, "GCTX.omt_ shouldn't be NULL",
        K_(GCTX.omt), K(GCTX), K(ret));
  }
  return ret;
}

int ObAllVirtualDtlFirstCachedBufferIterator::init()
{
  int ret = OB_SUCCESS;
  buffer_infos_.set_block_allocator(ObWrapperAllocator(iter_allocator_));
  if (OB_FAIL(prepare_tenants())) {
  }
  return ret;
}

int ObAllVirtualDtlFirstCachedBufferIterator::get_tenant_buffer_infos()
{
  return OB_SUCCESS;
}

int ObAllVirtualDtlFirstCachedBufferIterator::get_next_tenant_buffer_infos()
{
  int ret = OB_SUCCESS;
  if (0 != buffer_infos_.count()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("mem pool infos must be empty", K(ret));
  } else if (!done_) {
    done_ = true;
    if (OB_FAIL(get_tenant_buffer_infos())) {
    }
  } else {
    ret = OB_ITER_END;
  }

  if (OB_SUCC(ret)) {
    if (0 == buffer_infos_.count()) {
      ret = OB_ITER_END;
    }
  }
  return ret;
}

ObAllVirtualDtlFirstCachedBuffer::ObAllVirtualDtlFirstCachedBuffer() :
  ipstr_(),
  port_(0),
  arena_allocator_(ObModIds::OB_SQL_DTL),
  iter_(&arena_allocator_)
{}

ObAllVirtualDtlFirstCachedBuffer::~ObAllVirtualDtlFirstCachedBuffer()
{
  destroy();
}

void ObAllVirtualDtlFirstCachedBuffer::destroy()
{
  iter_.reset();
  arena_allocator_.reuse();
  arena_allocator_.reset();
}

void ObAllVirtualDtlFirstCachedBuffer::reset()
{
  port_ = 0;
  ipstr_.reset();
  iter_.reset();
  arena_allocator_.reuse();
  start_to_read_ = false;
}

int ObAllVirtualDtlFirstCachedBuffer::inner_open()
{
  int ret = OB_SUCCESS;
  if (!start_to_read_) {
    if (OB_FAIL(iter_.init())) {
    } else {
      start_to_read_ = true;
      char ipbuf[common::OB_IP_STR_BUFF];
      const common::ObAddr &addr = GCTX.self_addr();
      if (!addr.ip_to_string(ipbuf, sizeof(ipbuf))) {
        SERVER_LOG(ERROR, "ip to string failed");
        ret = OB_ERR_UNEXPECTED;
      } else {
        ipstr_ = ObString::make_string(ipbuf);
        if (OB_FAIL(ob_write_string(*allocator_, ipstr_, ipstr_))) {
        }
        port_ = addr.get_port();
      }
    }
  }
  return ret;
}

int ObAllVirtualDtlFirstCachedBuffer::inner_get_next_row(ObNewRow *&row)
{
  return OB_ITER_END;
}

