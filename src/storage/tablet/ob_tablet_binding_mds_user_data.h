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

#ifndef OCEANBASE_STORAGE_OB_TABLET_BINDING_MDS_USER_DATA
#define OCEANBASE_STORAGE_OB_TABLET_BINDING_MDS_USER_DATA

#include <stdint.h>
#include "lib/container/ob_se_array.h"
#include "lib/utility/ob_print_utils.h"
#include "share/scn.h"
#include "common/ob_tablet_id.h"
#include "storage/meta_mem/ob_storage_meta_cache.h"

namespace oceanbase
{
namespace common
{
class ObIAllocator;
}

namespace storage
{
class ObTabletBindingInfo;

class ObTabletBindingMdsUserData : public ObIStorageMetaObj
{
public:
  OB_UNIS_VERSION(1);

public:
  ObTabletBindingMdsUserData();
  virtual ~ObTabletBindingMdsUserData() = default;
  ObTabletBindingMdsUserData(const ObTabletBindingMdsUserData &) = delete;
  ObTabletBindingMdsUserData &operator=(const ObTabletBindingMdsUserData &) = delete;

public:
  virtual int deep_copy(char *buf, const int64_t buf_len, ObIStorageMetaObj *&value) const override;
  virtual int64_t get_deep_copy_size() const override;

public:
  bool is_valid() const;
  int assign(const ObTabletBindingMdsUserData &other);
  void on_commit(const share::SCN &commit_version, const share::SCN &commit_scn);

  void reset();
  void set_default_value();

  TO_STRING_KV(K_(redefined),
                K_(snapshot_version),
                K_(schema_version),
                K_(data_tablet_id),
                K_(hidden_tablet_id),
                K_(lob_meta_tablet_id),
                K_(lob_piece_tablet_id),
                K_(is_old_mds));

public:
  int64_t snapshot_version_; // if redefined it is max readable snapshot, else it is min readable snapshot.
  int64_t schema_version_;
  common::ObTabletID data_tablet_id_;
  common::ObTabletID hidden_tablet_id_;
  common::ObTabletID lob_meta_tablet_id_;
  common::ObTabletID lob_piece_tablet_id_;
  bool redefined_;
  bool is_old_mds_;
};
} // namespace storage
} // namespace oceanbase

#endif // OCEANBASE_STORAGE_OB_TABLET_BINDING_MDS_USER_DATA
