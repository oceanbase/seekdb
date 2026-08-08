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

#ifndef OCEANBASE_STORAGE_TABLET_OB_TABLET_AUTOINCREMENT_STATE_H_
#define OCEANBASE_STORAGE_TABLET_OB_TABLET_AUTOINCREMENT_STATE_H_

#include "share/ob_tablet_autoincrement_param.h"
#include "storage/memtable/ob_i_multi_source_data_unit.h"
#include "storage/meta_mem/ob_i_storage_meta_obj.h"

namespace oceanbase
{
namespace storage
{

// Persistent Storage representation. Public Share callers only see the value
// vocabulary in ob_tablet_autoincrement_param.h.
class ObTabletAutoincSeq : public memtable::ObIMultiSourceDataUnit, public ObIStorageMetaObj
{
public:
  const int32_t AUTOINC_SEQ_VERSION = 1;

  ObTabletAutoincSeq();
  ~ObTabletAutoincSeq();

  int assign(common::ObIAllocator &allocator, const ObTabletAutoincSeq &other);
  int deep_copy(
      const memtable::ObIMultiSourceDataUnit *src,
      common::ObIAllocator *allocator) override;
  int deep_copy(
      char *dst_buf,
      const int64_t buf_size,
      ObIStorageMetaObj *&value) const override;
  int64_t get_deep_copy_size() const override
  {
    return sizeof(ObTabletAutoincSeq)
        + sizeof(share::ObTabletAutoincInterval) * intervals_count_;
  }
  void reset() override;
  bool is_valid() const override;
  int64_t get_data_size() const override { return get_deep_copy_size(); }
  memtable::MultiSourceDataUnitType type() const override
  {
    return memtable::MultiSourceDataUnitType::TABLET_SEQ;
  }
  int get_autoinc_seq_value(uint64_t &autoinc_seq) const;
  int set_autoinc_seq_value(
      common::ObArenaAllocator &allocator,
      const uint64_t autoinc_seq);
  const share::ObTabletAutoincInterval *get_intervals() const { return intervals_; }
  int64_t get_intervals_count() const { return intervals_count_; }

  int serialize(char *buf, const int64_t buf_len, int64_t &pos) const;
  int deserialize(
      common::ObIAllocator &allocator,
      const char *buf,
      const int64_t data_len,
      int64_t &pos);
  int64_t get_serialize_size() const;

  TO_STRING_KV(K_(version), K_(intervals_count), KPC_(intervals));
private:
  int deserialize_(
      common::ObIAllocator &allocator,
      const char *buf,
      const int64_t data_len,
      int64_t &pos);
  int serialize_(char *buf, const int64_t buf_len, int64_t &pos) const;
  int64_t get_serialize_size_(void) const;
private:
  int64_t version_;
  common::ObIAllocator *allocator_;
  share::ObTabletAutoincInterval *intervals_;
  int64_t intervals_count_;
  DISALLOW_COPY_AND_ASSIGN(ObTabletAutoincSeq);
};

} // namespace storage
} // namespace oceanbase

#endif // OCEANBASE_STORAGE_TABLET_OB_TABLET_AUTOINCREMENT_STATE_H_
