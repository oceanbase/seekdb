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

#ifndef OCEANBASE_STORAGE_OB_LOCK_MEMTABLE_MGR_
#define OCEANBASE_STORAGE_OB_LOCK_MEMTABLE_MGR_

#include "storage/ob_i_memtable_mgr.h"

namespace oceanbase
{
namespace common
{
class ObTabletID;
}

namespace share
{
class ObLSID;
}

namespace memtable
{
}

namespace storage
{
class ObIMemtable;
class ObFreezer;
class ObTenantMetaMemMgr;
}

namespace transaction
{
namespace tablelock
{
class ObLockMemtable;

class ObLockMemtableMgr : public storage::ObIMemtableMgr
{
public:
  ObLockMemtableMgr();
  virtual ~ObLockMemtableMgr();

  // ================== Unified Class Method ==================
  //
  // Init the memtable mgr, we use logstream id to fetch the ls_ctx_mgr and t3m
  // to alloc the memtable.
  virtual int init(const common::ObTabletID &tablet_id,
                   const share::ObLSID &ls_id,
                   storage::ObFreezer *freezer,
                   storage::ObTenantMetaMemMgr *t3m) override;
  virtual void destroy() override;

  virtual int create_memtable(const storage::CreateMemtableArg &arg) override;

  DECLARE_VIRTUAL_TO_STRING;
private:
  const ObLockMemtable *get_memtable_(const int64_t pos) const;
private:
  virtual int release_head_memtable_(storage::ObIMemtable *imemtable,
                                     const bool force = false) override;

private:
  share::ObLSID ls_id_;
  common::ObQSyncLock lock_def_;
};

} // namespace tablelock
} // transaction
} // oceanbase

#endif
