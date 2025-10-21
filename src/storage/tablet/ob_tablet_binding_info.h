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

#ifndef OCEANBASE_STORAGE_OB_TABLET_BINDING_INFO
#define OCEANBASE_STORAGE_OB_TABLET_BINDING_INFO

#include <stdint.h>
#include "lib/utility/ob_print_utils.h"
#include "lib/utility/ob_unify_serialize.h"
#include "lib/container/ob_se_array.h"
#include "common/ob_tablet_id.h"
#include "storage/memtable/ob_multi_source_data.h"

namespace oceanbase
{
namespace storage
{
class ObTabletBindingInfo : public memtable::ObIMultiSourceDataUnit
{
public:
  OB_UNIS_VERSION_V(1);
public:
  ObTabletBindingInfo();
  virtual ~ObTabletBindingInfo() = default;
  int set_allocator(ObIAllocator &allocator);
  int assign(const ObTabletBindingInfo &arg);

  virtual int deep_copy(const memtable::ObIMultiSourceDataUnit *src, ObIAllocator *allocator = nullptr) override;
  virtual void reset() override;
  virtual bool is_valid() const override;
  virtual int64_t get_data_size() const override { return sizeof(ObTabletBindingInfo); }
  virtual memtable::MultiSourceDataUnitType type() const override { return memtable::MultiSourceDataUnitType::TABLET_BINDING_INFO; }

  TO_STRING_KV(K_(redefined), K_(snapshot_version), K_(schema_version), K_(data_tablet_id), K_(hidden_tablet_ids), K_(lob_meta_tablet_id), K_(lob_piece_tablet_id), K_(is_tx_end), K_(unsynced_cnt_for_multi_data));
public:
  bool redefined_;
  int64_t snapshot_version_; // if redefined it is max readable snapshot, else it is min readable snapshot.
  int64_t schema_version_;
  common::ObTabletID data_tablet_id_;
  common::ObSEArray<common::ObTabletID, 2> hidden_tablet_ids_;
  common::ObTabletID lob_meta_tablet_id_;
  common::ObTabletID lob_piece_tablet_id_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObTabletBindingInfo);
};
} // namespace storage
} // namespace oceanbase

#endif // OCEANBASE_STORAGE_OB_TABLET_BINDING_INFO
