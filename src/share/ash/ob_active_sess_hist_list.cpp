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

#define USING_LOG_PREFIX SHARE

#include "share/ash/ob_active_sess_hist_list.h"
#include "lib/allocator/ob_malloc.h"
#include "share/config/ob_server_config.h"
#include "lib/guard/ob_shared_guard.h"          // ObShareGuard
#include "lib/ob_running_mode.h"
namespace oceanbase
{
namespace common
{
share::ObActiveSessHistList* __attribute__((used)) lib_get_ash_list_instance() {
  return &share::ObActiveSessHistList::get_instance();
}
}
}
using namespace oceanbase::common;
using namespace oceanbase::share;

ObActiveSessHistList::ObActiveSessHistList()
    : ash_size_(0),
    mutex_(common::ObLatchIds::ASH_LOCK),
    ash_buffer_()
{
  if (GCONF.is_valid()) {
    ash_size_ = GCONF._ob_ash_size;
  }
  if (ash_size_ == 0) {
    if (lib::is_mini_mode()) {
      ash_size_ = 4 * 1024 * 1024;  // 4M
    } else {
      ash_size_ = 12 * 1024 * 1024;  // 12M
    }
  }
}

ObActiveSessHistList& ObActiveSessHistList::get_instance()
{
  static ObActiveSessHistList the_one;
  return the_one;
}


int ObActiveSessHistList::init()
{
  int ret = OB_SUCCESS;
  if (ash_buffer_.is_valid()) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ash buffer exist", KR(ret));
  } else if (OB_FAIL(mutex_.trylock())) {
    LOG_WARN("previous ash resize task is executing", KR(ret));
  } else {
    common::ObSharedGuard<ObAshBuffer> tmp;
    if (OB_FAIL(allocate_ash_buffer(ash_size_, tmp))) {
      LOG_WARN("failed to allocate ash buffer", KR(ret));
    } else {
      ash_buffer_ = tmp;
      LOG_INFO("ash buffer init OK", K_(ash_buffer));
    }
    mutex_.unlock();
  }
  return ret;
}

int ObActiveSessHistList::resize_ash_size()
{
  int ret = OB_SUCCESS;
  int64_t ash_size = GCONF._ob_ash_size;
  if (ash_size == 0) {
    if (lib::is_mini_mode()) {
      ash_size = 4 * 1024 * 1024;  // 4M
    } else {
      ash_size = 12 * 1024 * 1024;  // 12M
    }
  }
  if (ash_size != ash_size_) {
    LockGuard lock(mutex_);
    // allocator new
    common::ObSharedGuard<ObAshBuffer> tmp;
    if (OB_FAIL(allocate_ash_buffer(ash_size, tmp))) {
      LOG_WARN("failed to allocate ash buffer", KR(ret));
    } else {
      // copy old to new
      ForwardIterator iter = create_forward_iterator_no_lock();
      while (iter.has_next()) {
        const ObActiveSessionStatItem &stat = iter.next();
        if (iter.distance() <= tmp->size()) {
          tmp->copy_from_ash_buffer(stat);
        }
      }
      // swap old with new (with mutex protection)
      LOG_INFO("successfully resize ash buffer", K(ash_size), "prev_ash_buffer", ash_buffer_.get_ptr(), "prev_size", ash_size_);
      ash_buffer_ = tmp;
      ash_size_ = ash_size;
    }
  }
  return ret;
}

int ObActiveSessHistList::allocate_ash_buffer(int64_t ash_size, common::ObSharedGuard<ObAshBuffer> &ash_buffer)
{
  int ret = OB_SUCCESS;
  ObMemAttr attr;
  attr.label_ = "ash";
  attr.ctx_id_ = ObCtxIds::DEFAULT_CTX_ID;
  attr.tenant_id_ = OB_SYS_TENANT_ID;
  if (OB_FAIL(ob_make_shared<ObAshBuffer>(ash_buffer))) {
    LOG_WARN("failed to make ash buffer", KR(ret));
  } else {
    ash_buffer->set_label("ASHListBuffer");
    ash_buffer->set_tenant_id(OB_SYS_TENANT_ID);
    if (OB_FAIL(ash_buffer->prepare_allocate(ash_size / sizeof(ObActiveSessionStatItem)))) {
      LOG_WARN("fail init ASH circular buffer", K(ret));
    } else {
      LOG_INFO("init ASH circular buffer OK", "size", ash_buffer->size());
    }
  }
  return ret;
}
