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

#include "storage/tablet/ob_tablet_iterator.h"
#include "share/rc/ob_server_runtime.h"
#include "storage/ls/ob_ls.h"

namespace oceanbase
{
namespace storage
{
ObLSTabletIterator::ObLSTabletIterator(const ObMDSGetTabletMode mode)
  : ls_tablet_service_(nullptr),
    tablet_ids_(),
    idx_(0),
    mode_(mode)
{
}

ObLSTabletIterator::~ObLSTabletIterator()
{
  reset();
}

void ObLSTabletIterator::reset()
{
  ls_tablet_service_ = nullptr;
  tablet_ids_.reset();
  idx_ = 0;
}

bool ObLSTabletIterator::is_valid() const
{
  return nullptr != ls_tablet_service_
      && mode_ >= ObMDSGetTabletMode::READ_ALL_COMMITED;
}

int ObLSTabletIterator::get_next_tablet(ObTabletHandle &handle)
{
  int ret = OB_SUCCESS;

  handle.reset();
  if (OB_ISNULL(ls_tablet_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ls tablet service is nullptr", K(ret), KP(ls_tablet_service_));
  } else {
    do {
      if (OB_UNLIKELY(tablet_ids_.count() == idx_)) {
        ret = OB_ITER_END;
      } else {
        const common::ObTabletID &tablet_id = tablet_ids_.at(idx_);
        if (OB_FAIL(ls_tablet_service_->get_tablet(tablet_id, handle, ObTabletCommon::DEFAULT_GET_TABLET_DURATION_10_S, mode_))
            && OB_TABLET_NOT_EXIST != ret) {
          LOG_WARN("fail to get tablet", K(ret), K(idx_), K(tablet_id), K_(mode));
        } else {
          handle.set_wash_priority(WashTabletPriority::WTP_LOW);
          ++idx_;
        }
      }
    } while (OB_TABLET_NOT_EXIST == ret);
  }

  return ret;
}

int ObLSTabletIterator::get_next_ddl_kv_mgr(ObDDLKvMgrHandle &ddl_kv_mgr_handle)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(ls_tablet_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ls tablet service is nullptr", K(ret), KP(ls_tablet_service_));
  } else {
    ObStorageMetaMemMgr *t3m = ::oceanbase::share::server_service<::oceanbase::storage::ObStorageMetaMemMgr>();
    do {
      ObTabletMapKey key;
      if (OB_UNLIKELY(tablet_ids_.count() == idx_)) {
        ret = OB_ITER_END;
      } else {
        key.tablet_id_ = tablet_ids_.at(idx_);

        if (OB_FAIL(t3m->get_tablet_ddl_kv_mgr(key, ddl_kv_mgr_handle))
            && OB_ENTRY_NOT_EXIST != ret) {
          LOG_WARN("fail to get tablet ddl kv mgr", K(ret), K(idx_), K(key));
        } else {
          ++idx_;
        }
      }
    } while (OB_ENTRY_NOT_EXIST == ret);
  }

  return ret;
}


int ObLSTabletIterator::get_tablet_ids(ObIArray<ObTabletID> &ids) const
{
  int ret = OB_SUCCESS;
  ids.reset();
  if (OB_FAIL(ids.assign(tablet_ids_))) {
  }
  return ret;
}

ObLSTabletFastIter::ObLSTabletFastIter(ObITabletFilterOp &op, const ObMDSGetTabletMode mode)
  : ls_tablet_service_(nullptr),
    tablet_ids_(),
    idx_(0),
    mode_(mode),
    op_(op)
{
}

bool ObLSTabletFastIter::is_valid() const
{
  return nullptr != ls_tablet_service_
      && mode_ <= ObMDSGetTabletMode::READ_WITHOUT_CHECK; // READ_READABLE_COMMITED is not supported
}

void ObLSTabletFastIter::reset()
{
  ls_tablet_service_ = nullptr;
  tablet_ids_.reset();
  idx_ = 0;
}


ObLSTabletAddrIterator::ObLSTabletAddrIterator()
  : ls_tablet_service_(nullptr),
    tablet_ids_(),
    idx_(0)
{
}

ObLSTabletAddrIterator::~ObLSTabletAddrIterator()
{
  reset();
}

// for write_checkpoint in SN and active_tablet_arr in SS
int ObLSTabletAddrIterator::get_next_tablet_addr(ObTabletMapKey &key, ObMetaDiskAddr &addr)
{
  int ret = OB_SUCCESS;

  if (OB_ISNULL(ls_tablet_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ls tablet service is nullptr", K(ret), KP(ls_tablet_service_));
  } else {
    do {
      if (OB_UNLIKELY(tablet_ids_.count() == idx_)) {
        ret = OB_ITER_END;
      } else {
        key.tablet_id_ = tablet_ids_.at(idx_);

        if (OB_FAIL(ls_tablet_service_->get_tablet_addr(key, addr))
            && OB_ENTRY_NOT_EXIST != ret) {
          LOG_WARN("fail to get tablet address", K(ret), K(idx_), K(key));
        } else {
          ++idx_;
        }
      }
    } while (OB_ENTRY_NOT_EXIST == ret);
  }

  return ret;
}

} // namespace storage
} // namespace oceanbase
