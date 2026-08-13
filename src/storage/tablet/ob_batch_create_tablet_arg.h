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

/* Storage-owned RPC value: to_string instantiation requires the complete
 * ObCreateTabletSchema type. The obrpc namespace and wire format are stable. */
#ifndef OCEANBASE_STORAGE_TABLET_OB_BATCH_CREATE_TABLET_ARG_H_
#define OCEANBASE_STORAGE_TABLET_OB_BATCH_CREATE_TABLET_ARG_H_

#include "share/ob_rpc_struct.h"
#include "storage/ob_storage_schema.h"
#include "storage/tablet/ob_tablet_create_delete_mds_user_data.h"

namespace oceanbase
{
namespace obcall
{
struct ObBatchCreateTabletArg
{
  OB_UNIS_VERSION(2);
public:
  ObBatchCreateTabletArg()
  { reset(); }
  ~ObBatchCreateTabletArg() {}
  bool is_valid() const;
  bool is_inited() const;
  void reset();
  int assign(const ObBatchCreateTabletArg &arg);
  int init_create_tablet(const share::SCN &major_frozen_scn,
                         const bool need_check_tablet_cnt);
  int64_t get_tablet_count() const;
  int serialize_for_create_tablet_schemas(char *buf,
      const int64_t data_len,
      int64_t &pos) const;
  int64_t get_serialize_size_for_create_tablet_schemas() const;
  int deserialize_create_tablet_schemas(const char *buf,
      const int64_t data_len,
      int64_t &pos);
  bool set_binding_info_outside_create() const;
  DECLARE_TO_STRING;

public:
  share::SCN major_frozen_scn_;
  common::ObSArray<share::schema::ObTableSchema> table_schemas_;
  common::ObSArray<ObCreateTabletInfo> tablets_;
  bool need_check_tablet_cnt_;
  common::ObSArray<storage::ObCreateTabletSchema*> create_tablet_schemas_;
  ObArenaAllocator allocator_;
  common::ObSArray<ObCreateTabletExtraInfo> tablet_extra_infos_;
  share::SCN clog_checkpoint_scn_;
  storage::ObTabletMdsUserDataType create_type_;
  share::SCN mds_checkpoint_scn_;
};

}  // namespace obcall
}  // namespace oceanbase
#endif
