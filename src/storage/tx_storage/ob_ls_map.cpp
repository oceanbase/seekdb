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


#include "ob_ls_map.h"
#include "storage/tx_storage/ob_ls_service.h"

using namespace oceanbase::share;
using namespace oceanbase::common;

namespace oceanbase
{
namespace storage
{
// ------------------- ObLSIterator -------------------- //
ObLSIterator::ObLSIterator()
  : ls_(nullptr),
    returned_(false),
    ls_map_(NULL),
    mod_(ObLSGetMod::INVALID_MOD)
{
}

ObLSIterator::~ObLSIterator()
{
  reset();
}

void ObLSIterator::reset()
{
  if (OB_NOT_NULL(ls_map_) && OB_NOT_NULL(ls_)) {
    ls_map_->revert_ls(ls_, mod_);
  }
  ls_ = nullptr;
  returned_ = false;
  ls_map_ = nullptr;
  mod_ = ObLSGetMod::INVALID_MOD;
}

int ObLSIterator::get_next(ObLS *&ls)
{
  int ret = OB_SUCCESS;
  ls = nullptr;

  if (OB_ISNULL(ls_map_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("The ls service is NULL, ", K(ret));
  } else if (returned_) {
    reset();
    ret = OB_ITER_END;
  } else {
    ObQSyncLockReadGuard guard(ls_map_->lock_);
    if (OB_ISNULL(ls_map_->ls_)) {
      ret = OB_ITER_END;
    } else if (OB_FAIL(ls_map_->ls_->get_ref_mgr().inc(mod_))) {
      LOG_WARN("ls inc ref fail", K(ret));
    } else {
      ls_ = ls_map_->ls_;
      ls = ls_;
      returned_ = true;
    }
  }
  return ret;
}

// ------------------- ObLSMap -------------------- //
void ObLSMap::reset()
{
  if (OB_NOT_NULL(ls_)) {
    ObLS *ls = ls_;
    ls_ = nullptr;
    ls->next_ = nullptr;
    ls->get_ref_mgr().set_delete();
    // here mod must be the same as add_ls
    revert_ls(ls, ObLSGetMod::TXSTORAGE_MOD);
  }
  lock_.destroy();
  ls_cnt_ = 0;
  ls_allocator_ = nullptr;
  is_inited_ = false;
}

int ObLSMap::init(ObIAllocator *ls_allocator)
{
  int ret = OB_SUCCESS;
  const char *OB_LS_MAP = "LSMap";
  ObMemAttr mem_attr(OB_LS_MAP);

  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ObLSMap init twice", K(ret));
  } else if (OB_ISNULL(ls_allocator)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret));
  } else if (OB_FAIL(lock_.init(mem_attr))) {
    LOG_WARN("lock init fail", K(ret));
  } else {
    ls_allocator_ = ls_allocator;
    ls_ = nullptr;
    ls_cnt_ = 0;
    is_inited_ = true;
  }

  return ret;
}

void ObLSMap::destroy()
{
  reset();
}

int ObLSMap::add_ls(
    ObLS &ls)
{
  int ret = OB_SUCCESS;
  const ObLSID &ls_id = ls.get_ls_id();
  LOG_INFO("ls map add ls",
           K(ls_id), KP(&ls), "ref", ls.get_ref_mgr().get_total_ref_cnt());

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObLSMap not init", K(ret), K(ls_id));
  } else {
    ObQSyncLockWriteGuard guard(lock_);
    if (OB_ISNULL(ls_)) {
      if (OB_FAIL(ls.get_ref_mgr().inc(ObLSGetMod::TXSTORAGE_MOD))) {
        LOG_WARN("ls inc ref fail", K(ret), K(ls_id));
      } else {
        ls.next_ = nullptr;
        ls_ = &ls;
        ATOMIC_STORE(&ls_cnt_, 1);
      }
    } else if (ls_->get_ls_id() == ls_id) {
      ret = OB_ENTRY_EXIST;
    } else {
      ret = OB_ERR_UNEXPECTED;
      LOG_ERROR("only one ls is supported in seekdb", K(ret), K(ls_id), KPC(ls_));
    }
  }

  LOG_INFO("ls map finish add ls",
           K(ls_id), KP(&ls), "ref", ls.get_ref_mgr().get_total_ref_cnt(), K(ret));
  return ret;
}

int ObLSMap::del_ls(const share::ObLSID &ls_id)
{
  int ret = OB_SUCCESS;
  ObLS *ls = NULL;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObLSMap not init", K(ret), K(ls_id));
  } else {
    {
      // Remove ls from map before releasing its ref to avoid holding lock during free.
      ObQSyncLockWriteGuard guard(lock_);
      if (OB_ISNULL(ls_) || ls_->get_ls_id() != ls_id) {
        ret = OB_LS_NOT_EXIST;
      } else {
        ls = ls_;
        LOG_INFO("ls service del ls", K(ls_id),
                 KP(ls), "ref", ls->get_ref_mgr().get_total_ref_cnt());
        ls_ = nullptr;
        ls->next_ = nullptr;
      }
    }
    del_ls_impl(ls);
  }

  return ret;
}

void ObLSMap::del_ls_impl(ObLS *ls)
{
  int ret = OB_SUCCESS;
  if (nullptr != ls) {
    const ObLSID &ls_id = ls->get_ls_id();
    ATOMIC_STORE(&ls_cnt_, 0);
    ls->get_ref_mgr().set_delete();
    // here mod must the same with add_ls
    revert_ls(ls, ObLSGetMod::TXSTORAGE_MOD);
  }
}

int ObLSMap::get_ls(const share::ObLSID &ls_id,
                    ObLSHandle &handle,
                    ObLSGetMod mod) const
{
  int ret = OB_SUCCESS;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObLSMap not init", K(ret), K(ls_id));
  } else {
    ObQSyncLockReadGuard guard(lock_);
    if (OB_ISNULL(ls_) || ls_->get_ls_id() != ls_id) {
      ret = OB_LS_NOT_EXIST;
    } else if (OB_FAIL(handle.set_ls(*this, *ls_, mod))) {
      LOG_WARN("get_ls fail", K(ret), K(ls_id));
    }
  }
  return ret;
}

int ObLSMap::get_all_ls_id(ObIArray<ObLSID> &ls_id_array)
{
  int ret = OB_SUCCESS;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObLSMap not init", K(ret));
  } else {
    ObQSyncLockReadGuard guard(lock_);
    if (OB_NOT_NULL(ls_) && OB_FAIL(ls_id_array.push_back(ls_->get_ls_id()))) {
      LOG_WARN("failed to push back ls id", K(ret), KP_(ls));
    }
  }
  return ret;
}

} // end namespace storage
}; // end namespace oceanbase
