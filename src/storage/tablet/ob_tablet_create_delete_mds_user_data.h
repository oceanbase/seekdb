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

#ifndef OCEANBASE_STORAGE_OB_TABLET_CREATE_DELETE_MDS_USER_DATA
#define OCEANBASE_STORAGE_OB_TABLET_CREATE_DELETE_MDS_USER_DATA

#include <stdint.h>
#include "lib/utility/ob_print_utils.h"
#include "share/scn.h"
#include "storage/tablet/ob_tablet_status.h"

namespace oceanbase
{
namespace storage
{

enum class ObTabletMdsUserDataType : int64_t
{
  NONE = 0,
  //for create tablet
  CREATE_TABLET = 1,
  //for drop tablet
  REMOVE_TABLET = 2,
  MAX_TYPE,
};

class ObTabletCreateDeleteMdsUserData
{
  OB_UNIS_VERSION(1);
public:
  ObTabletCreateDeleteMdsUserData();
  ~ObTabletCreateDeleteMdsUserData() = default;
  ObTabletCreateDeleteMdsUserData(const ObTabletStatus::Status &status, const ObTabletMdsUserDataType &type, const int64_t create_commit_version);
  ObTabletCreateDeleteMdsUserData(const ObTabletCreateDeleteMdsUserData &) = delete;
  ObTabletCreateDeleteMdsUserData &operator=(const ObTabletCreateDeleteMdsUserData &) = delete;
public:
  void reset();
  bool is_valid() const;
  int assign(const ObTabletCreateDeleteMdsUserData &other);
  ObTabletStatus get_tablet_status() const;
  share::SCN get_create_scn() const;
  void on_init();
  void on_redo(const share::SCN &redo_scn);
  void on_commit(const share::SCN &commit_version, const share::SCN &commit_scn);
  // todo(zk250686): tablet shell
  static int set_tablet_gc_trigger();
  static int set_tablet_empty_shell_trigger();

  TO_STRING_KV(K_(tablet_status), K_(data_type),
      K_(create_commit_scn), K_(create_commit_version),
      K_(delete_commit_scn), K_(delete_commit_version));
private:
  void create_tablet_on_commit_(const share::SCN &commit_version, const share::SCN &commit_scn);
  void delete_tablet_on_commit_(const share::SCN &commit_version, const share::SCN &commit_scn);

public:
  ObTabletStatus tablet_status_;
  ObTabletMdsUserDataType data_type_;

  // create_commit_scn_ remain unchanged throughout the entire tablet lifecycle
  share::SCN create_commit_scn_; // tablet's first create tx commit log scn, set this in create_tablet_on_commit_
  int64_t create_commit_version_; // create tx commit trans version
  share::SCN delete_commit_scn_; // delete tx commit log scn
  int64_t delete_commit_version_; // delete tx commit trans version
};

inline bool ObTabletCreateDeleteMdsUserData::is_valid() const
{
  return tablet_status_.is_valid()
      && data_type_ >= ObTabletMdsUserDataType::NONE
      && data_type_ < ObTabletMdsUserDataType::MAX_TYPE;
}

inline ObTabletStatus ObTabletCreateDeleteMdsUserData::get_tablet_status() const
{
  return tablet_status_;
}

inline share::SCN ObTabletCreateDeleteMdsUserData::get_create_scn() const
{
  return create_commit_scn_;
}

} // namespace storage
} // namespace oceanbase

#endif // OCEANBASE_STORAGE_OB_TABLET_CREATE_DELETE_MDS_USER_DATA
