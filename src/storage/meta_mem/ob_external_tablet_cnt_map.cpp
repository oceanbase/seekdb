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

#include "storage/meta_mem/ob_external_tablet_cnt_map.h"
#include "storage/tablet/ob_tablet.h"

namespace oceanbase
{
// using namespace common::hash;
namespace storage
{

ObExternalTabletCntMap::ObExternalTabletCntMap()
 : is_inited_(false),
   bucket_lock_(),
   ex_tablet_map_()
{ 
}

int ObExternalTabletCntMap::init(const int64_t bucket_num)
{
  int ret = OB_SUCCESS;
  if (is_inited_) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret));
  } else if (bucket_num <= 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(bucket_num));
  } else if (OB_FAIL(ex_tablet_map_.create(bucket_num, "ExTabletCntMap", "ExTabletCntMap"))) {
  } else if (OB_FAIL(bucket_lock_.init(bucket_num, ObLatchIds::DEFAULT_BUCKET_LOCK, ObMemAttr("ExTabletMapLk")))) {
  } else {
    is_inited_ = true;
  }
  return ret;
}

int ObExternalTabletCntMap::check_exist(const ObDieingTabletMapKey &key, bool &exist)
{
  int ret = OB_SUCCESS;
  exist = false;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("did not inited", K(ret));
  } else if (!key.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(key));
  } else {
    ObBucketHashRLockGuard lock_guard(bucket_lock_, key.hash());
    const Tablets *tablets = ex_tablet_map_.get(key);
    if (tablets == nullptr) {
      exist = false;
    } else if (tablets->empty()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected empty external tablet entry", K(ret), K(key));
    } else {
      exist = true;
    }
  }
  return ret;
}

int ObExternalTabletCntMap::reg_tablet(ObTablet &tablet)
{
  int ret = OB_SUCCESS;
  const ObDieingTabletMapKey key(tablet.get_tablet_id().id());
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("did not inited", K(ret));
  } else if (!key.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(key));
  } else {
    ObBucketHashWLockGuard lock_guard(bucket_lock_, key.hash());
    Tablets *tablets = ex_tablet_map_.get(key);
    if (tablets == nullptr) {
      Tablets first;
      if (OB_FAIL(first.push_back(&tablet))) {
      } else if (OB_FAIL(ex_tablet_map_.set_refactored(key, first))) {
      }
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < tablets->count(); ++i) {
        if (tablets->at(i) == &tablet) { ret = OB_ENTRY_EXIST; }
      }
      if (OB_SUCC(ret)) {
        tablets->set_block_size(8 * sizeof(ObTablet *));
        ret = tablets->push_back(&tablet);
      }
    }
  }
  return ret;
}

int ObExternalTabletCntMap::unreg_tablet(ObTablet &tablet)
{
  int ret = OB_SUCCESS;
  const ObDieingTabletMapKey key(tablet.get_tablet_id().id());
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("did not inited", K(ret));
  } else if (!key.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(key));
  } else {
    ObBucketHashWLockGuard lock_guard(bucket_lock_, key.hash());
    Tablets *tablets = ex_tablet_map_.get(key);
    int64_t found = -1;
    if (tablets != nullptr) {
      for (int64_t i = 0; found < 0 && i < tablets->count(); ++i) {
        if (tablets->at(i) == &tablet) { found = i; }
      }
    }
    if (found < 0) {
      ret = OB_ENTRY_NOT_EXIST;
    } else if (OB_FAIL(tablets->remove(found))) {
    } else if (tablets->empty()) {
      ret = ex_tablet_map_.erase_refactored(key);
    }
  }
  return ret;
}

int ObExternalTabletCntMap::scan(const std::function<int(const ObTablet &)> &visit)
{
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
  } else if (!visit) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    ObBucketTryRLockAllGuard guard(bucket_lock_);
    if (OB_FAIL(guard.get_ret())) {
    } else {
      auto each = [&](auto &entry) {
        int rc = OB_SUCCESS;
        for (int64_t i = 0; rc == OB_SUCCESS && i < entry.second.count(); ++i) {
          rc = visit(*entry.second.at(i));
        }
        return rc;
      };
      ret = ex_tablet_map_.foreach_refactored(each);
    }
  }
  return ret;
}

void ObExternalTabletCntMap::destroy()
{
  is_inited_ = false;
  bucket_lock_.destroy();
  ex_tablet_map_.destroy();
}

} // end namespace storage
} // end namespace oceanbase
