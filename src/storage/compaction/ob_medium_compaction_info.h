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

#ifndef OB_STORAGE_COMPACTION_MEDIUM_COMPACTION_INFO_H_
#define OB_STORAGE_COMPACTION_MEDIUM_COMPACTION_INFO_H_

#include "lib/ob_errno.h"
#include "storage/ob_storage_schema.h"
#include "lib/container/ob_array_array.h"
#include "storage/compaction/ob_partition_merge_policy.h"
#include "storage/multi_data_source/mds_key_serialize_util.h"
#include "storage/compaction/ob_mds_filter_info.h"

namespace oceanbase
{
namespace storage
{
class ObTablet;
}
namespace compaction
{
struct ObParallelMergeInfo
{
public:
  ObParallelMergeInfo()
   : list_size_(0),
     parallel_datum_rowkey_list_(nullptr)
  {}
  ~ObParallelMergeInfo();
  int init(common::ObIAllocator &allocator, const ObParallelMergeInfo &other);
  void destroy(common::ObIAllocator &allocator);
  void clear()
  {
    list_size_ = 0;
    parallel_datum_rowkey_list_ = nullptr;
  }
  int64_t get_size() const { return list_size_; }
  bool is_valid() const
  {
    return list_size_ == 0
      || (list_size_ < UINT8_MAX && nullptr != parallel_datum_rowkey_list_);
  }

  template<typename T>
  int deep_copy_list(common::ObIAllocator &allocator, const T *src, T *&dst);
  template<typename T>
  void destroy(common::ObIAllocator &allocator, T *&array);
  // serialize & deserialize
  int serialize(char *buf, const int64_t buf_len, int64_t &pos) const;
  int deserialize(
      common::ObIAllocator &allocator,
      const char *buf,
      const int64_t data_len,
      int64_t &pos);
  int64_t get_serialize_size() const;
  int generate_from_range_array(
      ObIAllocator &allocator,
      common::ObArrayArray<ObStoreRange> &paral_range);
  int deep_copy_datum_rowkey(
    const int64_t idx,
    ObIAllocator &allocator,
    blocksstable::ObDatumRowkey &rowkey) const;
public:
  int64_t to_string(char* buf, const int64_t buf_len) const;
  static const int64_t MAX_PARALLEL_RANGE_SERIALIZE_LEN = 1 * 1024 * 1024;
  static const int64_t VALID_CONCURRENT_CNT = 1;
private:
  int generate_datum_rowkey_list(
    ObIAllocator &allocator,
    ObArrayArray<ObStoreRange> &paral_range);
  uint32_t list_size_;
  blocksstable::ObDatumRowkey *parallel_datum_rowkey_list_;
};

struct ObMediumCompactionInfoKey final
{
public:
  OB_UNIS_VERSION(1);
  static constexpr uint8_t MAGIC_NUMBER = 0xFF;
public:
  ObMediumCompactionInfoKey()
    : medium_snapshot_(0)
  {}
  ObMediumCompactionInfoKey(const ObMediumCompactionInfoKey &other)
    : medium_snapshot_(other.medium_snapshot_)
  {}
  ObMediumCompactionInfoKey(const int64_t medium_snapshot)
    : medium_snapshot_(medium_snapshot)
  {}
  ObMediumCompactionInfoKey &operator=(const ObMediumCompactionInfoKey &other)
  {
    medium_snapshot_ = other.medium_snapshot_;
    return *this;
  }
  ~ObMediumCompactionInfoKey() = default;

  void reset() { medium_snapshot_ = 0; }
  bool is_valid() const { return medium_snapshot_ > 0; }
  ObMediumCompactionInfoKey &operator=(const int64_t medium_snapshot)
  {
    medium_snapshot_ = medium_snapshot;
    return *this;
  }

  int64_t get_medium_snapshot() const { return medium_snapshot_; }

  int mds_serialize(char *buf, const int64_t buf_len, int64_t &pos) const {
    int ret = OB_SUCCESS;
    if (pos >= buf_len) {
      ret = OB_BUF_NOT_ENOUGH;
    } else {
      buf[pos++] = MAGIC_NUMBER;
      ret = storage::mds::ObMdsSerializeUtil::mds_key_serialize(medium_snapshot_, buf, buf_len, pos);
    }
    return ret;
  }
  int mds_deserialize(const char *buf, const int64_t buf_len, int64_t &pos) {
    int ret = OB_SUCCESS;
    int64_t tmp = 0;
    uint8_t magic_number = 0;
    if (pos >= buf_len) {
      ret = OB_BUF_NOT_ENOUGH;
    } else {
      magic_number = buf[pos++];
      if (magic_number != MAGIC_NUMBER) {
        ret = common::OB_ERR_UNEXPECTED;
      } else {
        ret = storage::mds::ObMdsSerializeUtil::mds_key_deserialize(buf, buf_len, pos, tmp);
      }
    }
    if (OB_SUCC(ret)) {
      medium_snapshot_ = tmp;
    }
    return ret;
  }
  int64_t mds_get_serialize_size() const { return sizeof(MAGIC_NUMBER) + storage::mds::ObMdsSerializeUtil::mds_key_get_serialize_size(medium_snapshot_); }

  TO_STRING_KV(K_(medium_snapshot));
private:
  int64_t medium_snapshot_;
};

struct ObMediumCompactionInfo final : public common::ObDLinkBase<ObMediumCompactionInfo>
{
public:
  enum ObCompactionType
  {
    MEDIUM_COMPACTION = 0,
    MAJOR_COMPACTION = 1,
    COMPACTION_TYPE_MAX,
  };
  static const char *ObCompactionTypeStr[];
  static const char *get_compaction_type_str(enum ObCompactionType type);
public:
  ObMediumCompactionInfo();
  ObMediumCompactionInfo(ObIAllocator &allocator);
  ~ObMediumCompactionInfo();

  int assign(ObIAllocator &allocator, const ObMediumCompactionInfo &medium_info);
  int init(ObIAllocator &allocator, const ObMediumCompactionInfo &medium_info);
  void set_basic_info(
    const ObCompactionType type,
    const ObAdaptiveMergePolicy::AdaptiveMergeReason merge_reason,
    const int64_t medium_snapshot)
  {
    compaction_type_ = type;
    medium_merge_reason_ = merge_reason;
    medium_snapshot_ = medium_snapshot;
  }
  int gene_parallel_info(
      common::ObArrayArray<ObStoreRange> &paral_range);
  static inline bool is_valid_compaction_type(const ObCompactionType type) { return MEDIUM_COMPACTION <= type && type < COMPACTION_TYPE_MAX; }
  static inline bool is_medium_compaction(const ObCompactionType type) { return MEDIUM_COMPACTION == type; }
  static inline bool is_major_compaction(const ObCompactionType type) { return MAJOR_COMPACTION == type; }
  inline bool is_major_compaction() const { return is_major_compaction((ObCompactionType)compaction_type_); }
  inline bool is_medium_compaction() const { return is_medium_compaction((ObCompactionType)compaction_type_); }
  void clear_parallel_range()
  {
    parallel_merge_info_.clear();
    contain_parallel_range_ = false;
  }
  void reset();
  bool is_valid() const;
  // serialize & deserialize
  int serialize(char *buf, const int64_t buf_len, int64_t &pos) const;
  int deserialize(
      common::ObIAllocator &allocator,
      const char *buf,
      const int64_t data_len,
      int64_t &pos);
  int64_t get_serialize_size() const;
  void gene_info(char* buf, const int64_t buf_len, int64_t &pos) const;
  int64_t to_string(char* buf, const int64_t buf_len) const;
public:
  static const int64_t DEFAULT_ENCODING_ROWS_LIMIT = 65536;
public:
  uint8_t compaction_type_;
  bool contain_parallel_range_;
  uint8_t medium_merge_reason_;
  bool is_schema_changed_;
  bool is_skip_database_major_;
  bool contain_mds_filter_info_;

  uint64_t data_version_;
  int64_t medium_snapshot_;
  int64_t last_medium_snapshot_;
  storage::ObStorageSchema storage_schema_;
  ObParallelMergeInfo parallel_merge_info_;
  uint64_t encoding_granularity_;
  ObMdsFilterInfo mds_filter_info_;
  ObIAllocator *allocator_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObMediumCompactionInfo);
};

} // namespace compaction
} // namespace oceanbase

#endif // OB_STORAGE_COMPACTION_MEDIUM_COMPACTION_INFO_H_
