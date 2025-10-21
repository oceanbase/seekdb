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

#ifndef OCEANBASE_ROOTSERVER_OB_TENANT_LS_BALANCE_GROUP_INFO_H
#define OCEANBASE_ROOTSERVER_OB_TENANT_LS_BALANCE_GROUP_INFO_H

#include "share/ob_ls_id.h"                 //ObLSID
#include "lib/hash/ob_hashmap.h"            //ObHashMap
#include "lib/ob_define.h"                  // OB_INVALID_TENANT_ID
#include "lib/allocator/page_arena.h"       // ObArenaAllocator

#include "ob_all_balance_group_builder.h"   // ObAllBalanceGroupBuilder
#include "ob_ls_balance_group_info.h"       // ObLSBalanceGroupInfo

namespace oceanbase
{
namespace rootserver
{

// Tenant All LS Balance Group Info
//
// Build current balance group info on every LS.
//
// NOTE: if partitions are not balanced, partitions of same partition group may locate on different LS
class ObTenantLSBalanceGroupInfo final : public ObAllBalanceGroupBuilder::NewPartitionCallback
{
public:
  ObTenantLSBalanceGroupInfo() : inited_(false), tenant_id_(OB_INVALID_TENANT_ID), ls_bg_map_() {}
  ~ObTenantLSBalanceGroupInfo() { destroy(); }

  int init(const uint64_t tenant_id);
  void destroy();

  // build All LS Balance Group Info
  int build(const char *mod,
      common::ObMySQLProxy &sql_proxy,
      share::schema::ObMultiVersionSchemaService &schema_service);

  int get(const share::ObLSID &ls_id, ObLSBalanceGroupInfo *&ls_bg_info) const
  {
    return ls_bg_map_.get_refactored(ls_id, ls_bg_info);
  }

public:
  // for ObAllBalanceGroupBuilder
  // Handle new partition when building balance group
  virtual int on_new_partition(
      const ObBalanceGroup &bg,
      const common::ObObjectID table_id,
      const common::ObObjectID part_object_id,
      const common::ObTabletID tablet_id,
      const share::ObLSID &src_ls_id,
      const share::ObLSID &dest_ls_id,
      const int64_t tablet_size,
      const bool in_new_partition_group,
      const uint64_t part_group_uid);

  TO_STRING_KV(K_(inited), K_(tenant_id), "valid_ls_count", ls_bg_map_.size());

private:
  int create_new_ls_bg_info_(const share::ObLSID ls_id,
      ObLSBalanceGroupInfo *&ls_bg_info);

private:
  static const int64_t MAP_BUCKET_NUM = 100;

  bool                      inited_;
  uint64_t                  tenant_id_;
  common::ObArenaAllocator  alloc_;

  // map for all balance groups on tenant every LS
  // If LS is empty, it does not exist in this map
  common::hash::ObHashMap<share::ObLSID, ObLSBalanceGroupInfo *> ls_bg_map_;
};

}
}
#endif /* !OCEANBASE_ROOTSERVER_OB_TENANT_LS_BALANCE_GROUP_INFO_H */
