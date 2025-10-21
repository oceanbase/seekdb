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


#include "ob_ls_handle.h"
#include "storage/tx_storage/ob_ls_map.h"

namespace oceanbase
{
namespace storage
{
ObLSHandle::ObLSHandle()
  : ls_map_(nullptr),
    ls_(nullptr),
    mod_(ObLSGetMod::INVALID_MOD)
{
  INIT_OBJ_LEAK_DEBUG_NODE(node_, this, share::LEAK_CHECK_OBJ_LS_HANDLE, MTL_ID());
}

ObLSHandle::ObLSHandle(const ObLSHandle &other)
  : ls_map_(nullptr),
    ls_(nullptr),
    mod_(ObLSGetMod::INVALID_MOD)
{
  INIT_OBJ_LEAK_DEBUG_NODE(node_, this, share::LEAK_CHECK_OBJ_LS_HANDLE, MTL_ID());
  *this = other;
}

ObLSHandle::~ObLSHandle()
{
  reset();
}

bool ObLSHandle::is_valid() const
{
  return (nullptr != ls_ && nullptr != ls_map_);
}

ObLSHandle &ObLSHandle::operator=(const ObLSHandle &other)
{
  int ret = OB_SUCCESS;
  if (&other != this) {
    if (nullptr != ls_ && nullptr != ls_map_) {
      reset();
    }
    if (nullptr != other.ls_ && nullptr != other.ls_map_ && OB_SUCC(other.ls_->get_ref_mgr().inc(other.mod_))) {
      ls_ = other.ls_;
      ls_map_ = other.ls_map_;
      mod_ = other.mod_;
    } else {
      LOG_WARN("ls assign fail", K(ret), K(other), K(ls_), K(ls_map_));
    }
  }
  return *this;
}

int ObLSHandle::set_ls(const ObLSMap &ls_map, ObLS &ls, const ObLSGetMod &mod)
{
  int ret = OB_SUCCESS;
  reset();
  if (OB_SUCC(ls.get_ref_mgr().inc(mod))) {
    ls_map_ = &ls_map;
    ls_ = &ls;
    mod_ = mod;
  }
  return ret;
}

void ObLSHandle::reset()
{
  if (OB_NOT_NULL(ls_map_) && OB_NOT_NULL(ls_)) {
    ls_map_->revert_ls(ls_, mod_);
    ls_map_ = nullptr;
    ls_ = nullptr;
    mod_ = ObLSGetMod::INVALID_MOD;
  }
}
} // namespace storage
} // namespace oceanbase
