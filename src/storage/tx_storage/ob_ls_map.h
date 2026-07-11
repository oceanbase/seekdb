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

#ifndef OCEANBASE_STORAGE_OB_LS_MAP_
#define OCEANBASE_STORAGE_OB_LS_MAP_

#include "lib/oblog/ob_log_module.h"
#include "lib/allocator/ob_concurrent_fifo_allocator.h"
#include "lib/container/ob_iarray.h"
#include "lib/lock/ob_qsync_lock.h"
#include "storage/ls/ob_ls.h"
#include "share/leak_checker/obj_leak_checker.h"

namespace oceanbase
{
namespace storage
{
class ObLS;
class ObLSHandle;

class ObLSMap
{
public:
  friend class ObLSIterator;
  ObLSMap()
   : is_inited_(false),
    ls_allocator_(nullptr),
    ls_cnt_(0),
    ls_(nullptr),
    lock_()
  {
  }
  ~ObLSMap() { destroy(); }
  void reset();
  int init(common::ObIAllocator *ls_allocator);
  void destroy();
  int add_ls(ObLS &ls);
  int del_ls(const share::ObLSID &ls_id);
  int get_all_ls_id(ObIArray<ObLSID> &ls_id_array);
  int get_ls(const share::ObLSID &ls_id,
             ObLSHandle &handle,
             ObLSGetMod mod) const;
  OB_INLINE void revert_ls(ObLS *ls, ObLSGetMod mod) const;
  template <typename Function>
  int operate_ls(const share::ObLSID &ls_id, Function &fn);
  bool is_empty() const { return 0 == ATOMIC_LOAD(&ls_cnt_); }
  int64_t get_ls_count() const { return ATOMIC_LOAD(&ls_cnt_); }
  static TCRef &get_tcref()
  {
    static TCRef tcref(16);
    return tcref;
  }
private:
  OB_INLINE void free_ls(ObLS *ls) const;
  void del_ls_impl(ObLS *ls);
private:
  bool is_inited_;
  
  common::ObIAllocator *ls_allocator_;
  // Seekdb keeps only one LS in current server of current tenant.
  int64_t ls_cnt_;
  ObLS *ls_;
  mutable common::ObQSyncLock lock_;
};

// Iterate the only LS in seekdb.
class ObLSIterator
{
public:
  ObLSIterator();
  virtual ~ObLSIterator();
  virtual int get_next(ObLS *&ls);
  void reset();
  void set_ls_map(ObLSMap &ls_map, ObLSGetMod mod) {
    ls_map_ = &ls_map;
    mod_ = mod;
  }
  TO_STRING_KV("has_ls", OB_NOT_NULL(ls_), K_(returned));
private:
  ObLS *ls_;
  bool returned_;
  ObLSMap *ls_map_;
  ObLSGetMod mod_;
};

OB_INLINE void ObLSMap::free_ls(ObLS *ls) const
{
  int ret = OB_SUCCESS;
  ls->~ObLS();
  ls_allocator_->free(ls);
}

OB_INLINE void ObLSMap::revert_ls(ObLS *ls, ObLSGetMod mod) const
{
  if (OB_NOT_NULL(ls)) {
    if (ls->get_ref_mgr().dec(mod)) {
      STORAGE_LOG(INFO, "ObLSMap free ls", KP(ls), K(mod), K(ls->get_ls_id()));
      free_ls(ls);
    }
  }
}

template <typename Function>
int ObLSMap::operate_ls(const share::ObLSID &ls_id,
                               Function &fn)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    STORAGE_LOG(WARN, "ObLSMap not init", K(ret), K(ls_id));
  } else {
    common::ObQSyncLockReadGuard guard(lock_);
    if (OB_ISNULL(ls_) || ls_->get_ls_id() != ls_id) {
      ret = OB_LS_NOT_EXIST;
    } else {
      ret = fn(ls_id, ls_);
    }
  }
  return ret;
}

}
}
#endif // OCEANBASE_STORAGE_OB_LS_MAP_
