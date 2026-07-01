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

#include "observer/table_load/ob_table_load_partition_location.h"
#include "share/rc/ob_module_provider.h"
#include "observer/ob_server.h"
#include "observer/table_load/ob_table_load_utils.h"
#include "storage/tx_storage/ob_ls_service.h"
#include "share/tablet/ob_tablet_to_ls_operator.h"

namespace oceanbase
{
namespace observer
{
using namespace common;
using namespace common::hash;
using namespace share;
using namespace storage;
using namespace table;

int ObTableLoadPartitionLocation::check_tablet_has_same_leader(const ObTableLoadPartitionLocation &other, bool &result)
{
  int ret = OB_SUCCESS;
  result = true;
  if (tablet_ids_.count() != other.tablet_ids_.count()) {
    result = false;
  }
  for (int64_t i = 0; OB_SUCC(ret) && result &&  i < tablet_ids_.count(); i ++) {
    PartitionLocationInfo info1;
    PartitionLocationInfo info2;
    if (OB_FAIL(partition_map_.get_refactored(tablet_ids_.at(i), info1))) {
    } else if (OB_FAIL(other.partition_map_.get_refactored(other.tablet_ids_.at(i), info2))) {
    } else if (info1.leader_addr_ != info2.leader_addr_) {
      result = false;
    }
  }
  return ret;
}

int ObTableLoadPartitionLocation::init_partition_location(
                              const ObIArray<ObTableLoadPartitionId> &partition_ids,
                              const ObIArray<ObTableLoadPartitionId> &target_partition_ids,
                              ObTableLoadPartitionLocation &partition_location,
                              ObTableLoadPartitionLocation &target_partition_location)
{
  int ret = OB_SUCCESS;
  int retry = 0;
  bool flag = false;
  while (retry < 3 && OB_SUCC(ret)) {
    partition_location.reset();
    target_partition_location.reset();
    // init partition_location_
    if (OB_FAIL(partition_location.init(partition_ids))) {
    } else if (OB_FAIL(target_partition_location.init(target_partition_ids))) {
    } else if (OB_FAIL(partition_location.check_tablet_has_same_leader(target_partition_location, flag))) {
    }
    if (OB_SUCC(ret)) {
      if (flag) {
        break;
      } else {
        LOG_WARN("invalid leader info, maybe change master");
      }
    }
    retry ++;
  }

  if (OB_SUCC(ret)) {
    if (!flag) {
      ret = OB_EAGAIN;
      LOG_WARN("invalid leader info", KR(ret));
    }
  }

  return ret;
}

int ObTableLoadPartitionLocation::fetch_ls_id(const ObTabletID &tablet_id,
                                              ObLSID &ls_id)
{
  int ret = OB_SUCCESS;
  ObLocationService &location_service = OBSERVER.get_location_service();
  const int64_t cluster_id = GCONF.cluster_id.get_value();
  MAKE_TENANT_SWITCH_SCOPE_GUARD(tenant_guard);
  bool is_cache_hit = false;
  if (OB_FAIL(tenant_guard.switch_to())) {
  } else if (OB_FAIL(location_service.get(tablet_id, INT64_MAX, is_cache_hit, ls_id))) {
  }
  return ret;
}

int ObTableLoadPartitionLocation::fetch_ls_location(const ObTabletID &tablet_id,
                                                    ObLSLocation &ls_location, ObLSID &ls_id)
{
  int ret = OB_SUCCESS;
  ObLocationService &location_service = OBSERVER.get_location_service();
  const int64_t cluster_id = GCONF.cluster_id.get_value();
  MAKE_TENANT_SWITCH_SCOPE_GUARD(tenant_guard);
  bool is_cache_hit = false;
  if (OB_FAIL(tenant_guard.switch_to())) {
  } else if (OB_FAIL(location_service.get(tablet_id, INT64_MAX, is_cache_hit, ls_id))) {
  } else if (OB_FAIL(location_service.get(cluster_id, ls_id, INT64_MAX, is_cache_hit,
                                          ls_location))) {
  }
  return ret;
}

int ObTableLoadPartitionLocation::fetch_ls_locations(const ObIArray<ObTableLoadPartitionId> &partition_ids)
{
  int ret = OB_SUCCESS;
  ObArray<ObLSID> ls_ids;
  

  for (int64_t i = 0; OB_SUCC(ret) && (i < partition_ids.count()); ++i) {
    const ObTabletID &tablet_id = partition_ids.at(i).tablet_id_;
    if (OB_FAIL(tablet_ids_.push_back(tablet_id))) {
    }
  }

  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(ObTabletToLSTableOperator::batch_get_ls(*(GCTX.sql_proxy_), tablet_ids_, ls_ids))) {
    if (OB_LIKELY(OB_ITEM_NOT_MATCH == ret)) {
      ret = OB_SCHEMA_NOT_UPTODATE;
    }
    LOG_WARN("table_load_partition failed to batch get ls", KR(ret));
  } else {
    ObLSLocation location;
    ObHashMap<ObLSID, ObAddr> ls_location_map;
    ObLocationService &location_service = OBSERVER.get_location_service();
    const int64_t cluster_id = GCONF.cluster_id.get_value();
    MAKE_TENANT_SWITCH_SCOPE_GUARD(tenant_guard);
    bool is_cache_hit = false;

    if (OB_FAIL(tenant_guard.switch_to())) {
    } else if (OB_FAIL(ls_location_map.create(1024, "TLD_PartLoc", "TLD_PartLoc"))) {
    } else {
      // avoid redundant location info lookups
      for (int64_t i = 0; OB_SUCC(ret) && i < partition_ids.count(); ++i) {
        const ObLSID &ls_id = ls_ids.at(i);
        PartitionLocationInfo info;
        info.partition_id_.part_tablet_id_ = partition_ids.at(i);
        info.partition_id_.ls_id_ = ls_id;

        if (OB_FAIL(ls_location_map.get_refactored(ls_id, info.leader_addr_))) {
          if (ret != OB_HASH_NOT_EXIST) {
            LOG_WARN("failed to get refactored", K(ret), K(i), K(ls_id));
          } else if (OB_FAIL(location_service.get(cluster_id, ls_id, INT64_MAX, is_cache_hit, location))) {
          } else if (OB_FAIL(location.get_leader(info.leader_addr_))) {
          } else if (OB_FAIL(ls_location_map.set_refactored(ls_id, info.leader_addr_))) {
          }
        }

        if (OB_FAIL(ret)) {
        } else if (OB_FAIL(partition_map_.set_refactored(tablet_ids_.at(i), info))) {
        }
      }
    }
  }

  return ret;
}


int ObTableLoadPartitionLocation::fetch_tablet_handle(const ObLSID &ls_id,
                                                      const ObTabletID &tablet_id,
                                                      ObTabletHandle &tablet_handle)
{
  int ret = OB_SUCCESS;
  ObLSService *ls_svr = nullptr;
  ObLSHandle ls_handle;
  ObLS *ls = nullptr;
  ObLSTabletService *tablet_service = nullptr;
  if (OB_ISNULL(ls_svr = share::g_mp->ls_service())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("MTL ObLSService failed", KR(ret));
  } else if (OB_FAIL(ls_svr->get_ls(ls_id, ls_handle, ObLSGetMod::STORAGE_MOD))) {
    if (OB_UNLIKELY(OB_LS_NOT_EXIST == ret)) {
      LOG_WARN("get ls handle failed", KR(ret), "log_stream_id", ls_id.id());
    }
  } else if (OB_ISNULL(ls = ls_handle.get_ls())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ls_handle.get_ls() is nullptr", KR(ret));
  } else if (OB_ISNULL(tablet_service = ls->get_tablet_svr())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet service should not be NULL", KR(ret), KP(tablet_service));
  } else if (OB_FAIL(tablet_service->get_tablet(tablet_id, tablet_handle))) {
  }
  return ret;
}


int ObTableLoadPartitionLocation::init(
    const ObIArray<ObTableLoadPartitionId> &partition_ids)
{
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ObTableLoadPartitionLocation init twice", KR(ret));
  } else if (OB_UNLIKELY(partition_ids.empty())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", KR(ret), K(partition_ids));
  } else {
    if (OB_FAIL(partition_map_.create(1024, "TLD_PartLoc", "TLD_PartLoc"))) {
    } else if (OB_FAIL(init_all_partition_location(partition_ids))) {
    } else if (OB_FAIL(init_all_leader_info())) {
    } else {
      is_inited_ = true;
    }
  }
  return ret;
}

int ObTableLoadPartitionLocation::init_all_partition_location(
  const ObIArray<ObTableLoadPartitionId> &partition_ids)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(fetch_ls_locations(partition_ids))) {
  }
  return ret;
}

int ObTableLoadPartitionLocation::init_all_leader_info()
{
  int ret = OB_SUCCESS;
  ObArenaAllocator tmp_allocator("TLD_PL_Tmp");
  ObHashMap<ObAddr, ObIArray<ObTableLoadLSIdAndPartitionId> *> addr_map;
  ObHashMap<ObAddr, ObIArray<ObTableLoadLSIdAndPartitionId> *>::const_iterator addr_iter;
  int64_t pos = 0;
  
  // Store all addr in the set
  if (OB_FAIL(addr_map.create(64, "TLD_PL_Tmp", "TLD_PL_Tmp"))) {
  } else {
    ObHashMap<ObTabletID, PartitionLocationInfo>::const_iterator iter;
    for (int64_t i = 0; OB_SUCC(ret) && i < tablet_ids_.count(); i ++) {
      PartitionLocationInfo info;
      if (OB_FAIL(partition_map_.get_refactored(tablet_ids_.at(i), info))) {
      }
      const ObTableLoadLSIdAndPartitionId &partition_id = info.partition_id_;
      const ObAddr &addr = info.leader_addr_;
      ObIArray<ObTableLoadLSIdAndPartitionId> *partition_id_array = nullptr;
      if (OB_SUCC(ret)) {
        if (OB_FAIL(addr_map.get_refactored(addr, partition_id_array))) {
          if (OB_UNLIKELY(OB_HASH_NOT_EXIST != ret)) {
            LOG_WARN("fail to get refactored", KR(ret), K(addr));
          } else {
            ObArray<ObTableLoadLSIdAndPartitionId> *new_array = nullptr;
            if (OB_ISNULL(new_array =
                            OB_NEWx(ObArray<ObTableLoadLSIdAndPartitionId>, (&tmp_allocator)))) {
              ret = OB_ALLOCATE_MEMORY_FAILED;
              LOG_WARN("fail to new array", KR(ret));
            } else if (OB_FAIL(addr_map.set_refactored(addr, new_array))) {
            } else {
              
              partition_id_array = new_array;
            }
            if (OB_FAIL(ret)) {
              if (nullptr != new_array) {
                new_array->~ObArray<ObTableLoadLSIdAndPartitionId>();
                tmp_allocator.free(new_array);
                new_array = nullptr;
              }
            }
          }
        }
        if (OB_SUCC(ret)) {
          if (OB_FAIL(partition_id_array->push_back(partition_id))) {
          }
        }
      }
    }
  }
	// Store the addr in set to array
	if (OB_SUCC(ret)) {
		if (OB_FAIL(all_leader_addr_array_.create(addr_map.size(), allocator_))) {
		} else if (OB_FAIL(all_leader_info_array_.create(addr_map.size(), allocator_))) {
		}
	}
  ObArray<LeaderInfoForSort> sort_array;
  
  for (addr_iter = addr_map.begin(); OB_SUCC(ret) && addr_iter != addr_map.end(); ++pos, ++addr_iter) {
    LeaderInfoForSort item;
    item.addr_ = addr_iter->first;
    item.partition_id_array_ptr_ = addr_iter->second;
    if (OB_FAIL(sort_array.push_back(item))) {
    }
  }
  if (OB_SUCC(ret)) {
    lib::ob_sort(sort_array.begin(), sort_array.end(), [](const ObTableLoadPartitionLocation::LeaderInfoForSort &a,
                 ObTableLoadPartitionLocation::LeaderInfoForSort &b) {
                return a.addr_ < b.addr_;
              });
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < sort_array.count(); i ++) {
    const ObAddr &addr = sort_array.at(i).addr_;
    ObIArray<ObTableLoadLSIdAndPartitionId> *partition_id_array = sort_array.at(i).partition_id_array_ptr_;
    all_leader_addr_array_[i] = addr;
    LeaderInfo &leader_info = all_leader_info_array_[i];
    leader_info.addr_ = addr;
    if (OB_FAIL(ObTableLoadUtils::deep_copy(*partition_id_array, leader_info.partition_id_array_,
                                            allocator_))) {
    }
    partition_id_array->~ObIArray<ObTableLoadLSIdAndPartitionId>();
    tmp_allocator.free(partition_id_array);
  }

  return ret;
}

int ObTableLoadPartitionLocation::get_leader(ObTabletID tablet_id, PartitionLocationInfo &info) const
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTableLoadPartitionLocation not init", KR(ret));
  } else {
    if (OB_FAIL(partition_map_.get_refactored(tablet_id, info))) {
    }
  }
  return ret;
}

int ObTableLoadPartitionLocation::get_all_leader(ObTableLoadArray<ObAddr> &addr_array) const
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTableLoadPartitionLocation not init", KR(ret));
  } else {
    addr_array = all_leader_addr_array_;
  }
  return ret;
}

int ObTableLoadPartitionLocation::get_all_leader_info(ObTableLoadArray<LeaderInfo> &info_array) const
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTableLoadPartitionLocation not init", KR(ret));
  } else {
    info_array = all_leader_info_array_;
  }
  return ret;
}

}  // namespace observer
}  // namespace oceanbase
