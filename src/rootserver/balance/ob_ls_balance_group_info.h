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

#ifndef OCEANBASE_ROOTSERVER_OB_LS_BALANCE_GROUP_INFO_H
#define OCEANBASE_ROOTSERVER_OB_LS_BALANCE_GROUP_INFO_H

#include "share/transfer/ob_transfer_info.h"  //ObTransferPartList, ObTransferPartInfo
#include "share/ob_ls_id.h"                   //ObLSID
#include "lib/hash/ob_hashmap.h"              //ObHashMap
#include "lib/allocator/page_arena.h"         //ObArenaAllocator
#include "ob_balance_group_define.h"          //ObBalanceGroupID
#include "ob_balance_group_info.h"            //ObBalanceGroupInfo

namespace oceanbase
{
namespace rootserver
{

// LS Balance Statistic Info
class ObLSBalanceGroupInfo final
{
public:
  ObLSBalanceGroupInfo() :
      inited_(false),
      ls_id_(),
      alloc_("LSBGInfo", common::OB_MALLOC_NORMAL_BLOCK_SIZE, MTL_ID()),
      bg_map_(),
      orig_part_group_cnt_map_()
  {}
  ~ObLSBalanceGroupInfo() { destroy(); }

  int init(const share::ObLSID &ls_id);
  void destroy();

  // append partition at the newest partition group in target balance group.
  // create new partition group in balance group if needed.
  //
  // NOTE: if balance group not exist, it will create a new balance group automatically
  //
  // @param [in] bg_id                        target balance group id
  // @param [in] part                         target partition info which will be added
  // @param [in] data_size                    partition data size
  // @param [in] part_group_uid               target partition group unique id
  //
  // @return OB_SUCCESS         success
  // @return OB_ENTRY_EXIST     no partition group found
  // @return other              fail
  int append_part_into_balance_group(const ObBalanceGroupID &bg_id,
      share::ObTransferPartInfo &part,
      const int64_t data_size,
      const uint64_t part_group_uid);

  ////////////////////////////////////////////////
  // Transfer out partition groups by specified factor
  //
  // NOTE: This function can be called only if all partitions are added.
  int transfer_out_by_factor(const float factor, share::ObTransferPartList &part_list);

  TO_STRING_KV(K_(inited), K_(ls_id), "balance_group_count", bg_map_.size());

private:
  int create_new_balance_group_(const ObBalanceGroupID &bg_id,
      ObBalanceGroupInfo *&bg);

private:
  static const int64_t MAP_BUCKET_NUM = 4096;

  bool                      inited_;
  share::ObLSID             ls_id_;
  common::ObArenaAllocator  alloc_;
  // map for all balance groups on this LS
  common::hash::ObHashMap<ObBalanceGroupID, ObBalanceGroupInfo *> bg_map_;
  // map for all balance groups' original partition group count
  // This original count will be maintained during adding partitions into balance group.
  // When all partitions are added, the original count will not change anymore.
  common::hash::ObHashMap<ObBalanceGroupID, int64_t> orig_part_group_cnt_map_;
};

}
}
#endif /* !OCEANBASE_ROOTSERVER_OB_LS_BALANCE_GROUP_INFO_H */
