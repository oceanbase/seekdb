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

#ifndef OCEABASE_STORAGE_LS_TRANSFER_INFO_
#define OCEABASE_STORAGE_LS_TRANSFER_INFO_

#include "share/ob_define.h"
#include "lib/utility/ob_unify_serialize.h"
#include "share/scn.h"
#include "lib/container/ob_array_serialization.h"
#include "share/schema/ob_schema_struct.h"

namespace oceanbase
{
namespace storage
{

struct ObLSTransferInfo final
{
public:
  ObLSTransferInfo();
  ~ObLSTransferInfo() = default;
  int init(
      const share::ObLSID &ls_id,
      const share::SCN &transfer_start_scn);
  void reset();
  bool is_valid() const;
  bool already_enable_replay() const;

  TO_STRING_KV(K_(ls_id), K_(transfer_start_scn));
public:
  share::ObLSID ls_id_;
  share::SCN transfer_start_scn_;
private:
  static const int64_t TRANSFER_INIT_LS_ID = 0;
};

class ObTransferInTransStatus final
{
public:
  enum STATUS : uint8_t
  {
    NONE = 0,
    PREPARE = 1,
    ABORT = 2,
    MAX
  };
public:
  ObTransferInTransStatus() = default;
  ~ObTransferInTransStatus() = default;
public:
  static bool is_valid(const ObTransferInTransStatus::STATUS &status);
  static int check_can_change_status(
      const ObTransferInTransStatus::STATUS &cur_status,
      const ObTransferInTransStatus::STATUS &change_status,
      bool &can_change);
};

struct ObTransferTabletIDArray final
{
  OB_UNIS_VERSION(1);
public:
  ObTransferTabletIDArray();
  ~ObTransferTabletIDArray();
  int assign(const common::ObIArray<common::ObTabletID> &tablet_id_array);
  int push_back(const common::ObTabletID &tablet_id);
  int get_tablet_id_array(common::ObIArray<common::ObTabletID> &tablet_id_array);

  inline const common::ObTabletID &at(int64_t idx) const
  {
    OB_ASSERT(idx >= 0 && idx < count_);
    return id_array_[idx];
  }
  inline common::ObTabletID &at(int64_t idx)
  {
    OB_ASSERT(idx >= 0 && idx < count_);
    return id_array_[idx];
  }
  inline int64_t count() const { return count_; }
  inline bool empty() const { return 0 == count(); }
  void reset() { count_ = 0; }

  int64_t to_string(char* buf, const int64_t buf_len) const
  {
    int64_t pos = 0;
    J_OBJ_START();
    J_NAME("id_array");
    J_COLON();
    (void)databuff_print_obj_array(buf, buf_len, pos, id_array_, count_);
    J_OBJ_END();
    return pos;
  }
private:
  static const int64_t MAX_TABLET_COUNT = share::schema::OB_MAX_TRANSFER_BINDING_TABLET_CNT;
  int64_t count_;
  common::ObTabletID id_array_[MAX_TABLET_COUNT];
};

struct ObLSTransferMetaInfo final
{
  OB_UNIS_VERSION(1);
public:
  ObLSTransferMetaInfo();
  ~ObLSTransferMetaInfo() = default;
  int set_transfer_info(
      const share::ObLSID &src_ls,
      const share::SCN &src_scn,
      const ObTransferInTransStatus::STATUS &trans_status,
      const common::ObIArray<common::ObTabletID> &tablet_id_array,
      const uint64_t data_version);
  int cleanup_transfer_info();

  void reset();
  bool is_valid() const;
  bool allow_src_ls_gc();
  int check_tablet_in_list(
      const common::ObTabletID &tablet_id,
      bool &is_exist);
  bool is_trans_status_same(const ObTransferInTransStatus::STATUS &trans_status);
  int get_tablet_id_array(common::ObIArray<ObTabletID> &tablet_id_array);
  int64_t to_string(char *buf, const int64_t buf_len) const;

private:
  int update_trans_status_(
      const ObTransferInTransStatus::STATUS &trans_status,
      const bool skip_check_trans_status);

public:
  share::ObLSID src_ls_;
  share::SCN src_scn_;  //transfer start scn
  ObTransferInTransStatus::STATUS trans_status_;
  ObTransferTabletIDArray tablet_id_array_;
  uint64_t data_version_;
private:
  static const int64_t TRANSFER_INIT_LS_ID = 0;
};

}
}
#endif
