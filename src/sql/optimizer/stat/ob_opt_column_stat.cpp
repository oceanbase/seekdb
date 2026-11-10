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

#define USING_LOG_PREFIX SQL_OPT

#include "ob_opt_column_stat.h"
#include "common/datum/datum_parameter_codec.h"
#include "common/datum/datum_parameter_array.h"
#include "share/datum/typed_datum_compare.h"
#include "sql/engine/expr/ob_expr.h"
#include "sql/engine/aggregate/ob_aggregate_processor.h"
namespace oceanbase {
namespace common {
using namespace sql;

namespace {
class StatValueBufferAllocator final : public ObIAllocator
{
public:
  StatValueBufferAllocator(char *buffer, int64_t length, int64_t &pos)
      : buffer_(buffer), length_(length), pos_(pos) {}
  void *alloc(int64_t bytes) override
  {
    void *result = nullptr;
    if (buffer_ != nullptr && bytes >= 0 && pos_ >= 0 && pos_ <= length_) {
      const int64_t padding = (8 - (reinterpret_cast<uintptr_t>(buffer_ + pos_) & 7)) & 7;
      if (padding <= length_ - pos_ && bytes <= length_ - pos_ - padding) {
        pos_ += padding; result = buffer_ + pos_; pos_ += bytes;
      }
    }
    return result;
  }
  void *alloc(int64_t bytes, const ObMemAttr &) override { return alloc(bytes); }
  void free(void *) override {}
private:
  char *buffer_;
  int64_t length_;
  int64_t &pos_;
};
static_assert(alignof(DatumParameter) <= 8 && alignof(DatumParameterArray) <= 8,
              "Column statistic native payload alignment");
// Each clone allocation reserves up to seven padding bytes. SQL arrays need
// independent descriptors and recursive leaves, even though normal collected
// column extrema are scalar/string values.
int64_t legacy_stat_value_copy_size(const DatumValue &value, int depth = 0)
{
  int64_t size = 0;
  alignas(8) char scratch[64];
  ObDatum physical;
  if (value.export_legacy(physical, scratch, sizeof(scratch)) != OB_SUCCESS) { return INT64_MAX; }
  if (depth > 64 || value.get_type() < ObNullType || value.get_type() >= ObMaxType
      || (!value.is_null() && physical.len_ > 0 && physical.ptr_ == nullptr)) {
    size = INT64_MAX;
  } else if (!value.is_null()) {
    size = MAX(1, physical.len_) + 7;
    if (value.is_ext_sql_array()) {
      if (physical.ptr_ == nullptr || physical.len_ != sizeof(int64_t)) { return INT64_MAX; }
      const DatumParameterArray *array = reinterpret_cast<const DatumParameterArray *>(physical.get_int());
      if (array == nullptr || array->count_ < 0
          || array->count_ > (INT64_MAX - sizeof(DatumParameterArray) - 7) / sizeof(DatumParameter)
          || (array->count_ > 0 && array->data_ == nullptr)) { return INT64_MAX; }
      const int64_t descriptor = sizeof(DatumParameterArray) + array->count_ * sizeof(DatumParameter) + 7;
      if (descriptor > INT64_MAX - size) { return INT64_MAX; }
      size += descriptor;
      for (int64_t i = 0; i < array->count_; ++i) {
        const int64_t child = legacy_stat_value_copy_size(array->data_[i].value_view(), depth + 1);
        if (child > INT64_MAX - size) { return INT64_MAX; }
        size += child;
      }
    }
  }
  return size;
}
int export_stat_value(const Datum &value, const SqlTypeMeta &meta, DatumValue &output,
                      void *scratch, int64_t capacity)
{
  ObDatum physical;
  int ret = DatumParameterCodec::export_schema_legacy_value(value, meta, physical, scratch, capacity);
  if (ret == OB_SUCCESS) { ret = output.assign_legacy_borrowed(physical, meta); }
  return ret;
}
int copy_stat_value(const Datum &value, const SqlTypeMeta &meta,
                    DatumValue &output, ObIAllocator &owner)
{
  alignas(8) char scratch[64];
  DatumValue view;
  const int ret = export_stat_value(value, meta, view, scratch, sizeof(scratch));
  return ret == OB_SUCCESS ? clone_typed_value(view, output, owner) : ret;
}
int64_t stat_value_copy_size(const Datum &value, const SqlTypeMeta &meta)
{
  alignas(8) char scratch[64];
  DatumValue view;
  if (export_stat_value(value, meta, view, scratch, sizeof(scratch)) != OB_SUCCESS) {
    return INT64_MAX;
  }
  // Retain a conservative payload/alignment allowance for present scalars.
  // Their native clone allocates nothing. SQL-array children still use their
  // existing domain descriptors and require the recursive reservation above.
  return value.is_present() ? legacy_stat_value_copy_size(view) : 0;
}
int clone_stat_value(const Datum &value, const SqlTypeMeta &meta, ObIAllocator &owner,
                     Datum &output, SqlTypeMeta &output_meta)
{
  Datum candidate;
  const int ret = DatumParameterCodec::clone_schema_native_value(value, meta, owner, candidate);
  if (ret == OB_SUCCESS) { output = candidate; output_meta = meta; }
  return ret;
}
int copy_stat_value_to_buffer(const Datum &source, const SqlTypeMeta &source_meta,
                              Datum &target, SqlTypeMeta &target_meta,
                              char *buffer, int64_t length, int64_t &pos)
{
  int ret = OB_SUCCESS;
  int64_t next = pos;
  const int64_t size = stat_value_copy_size(source, source_meta);
  StatValueBufferAllocator owner(buffer, length, next);
  if (buffer == nullptr || pos < 0 || pos > length || size == INT64_MAX) { ret = OB_INVALID_ARGUMENT; }
  else if (size > length - pos) { ret = OB_BUF_NOT_ENOUGH; }
  else if (OB_FAIL(clone_stat_value(source, source_meta, owner, target, target_meta))) {
    if (ret == OB_ALLOCATE_MEMORY_FAILED) { ret = OB_BUF_NOT_ENOUGH; }
  } else { pos = next; }
  return ret;
}
int64_t stat_value_wire_size(const Datum &value, const SqlTypeMeta &meta)
{
  int64_t size = 0;
  const int ret = DatumParameterCodec::schema_native_value_size(value, meta, size);
  if (ret != OB_SUCCESS) {
    LOG_ERROR("invalid native column statistics value", K(ret), K(value), K(meta));
  }
  return size;
}
}

int ObOptColumnStat::set_min_value(const DatumValue &value)
{
  Datum candidate;
  const int ret = DatumParameterCodec::clone_schema_native_value(value.datum_, value.meta_, allocator_, candidate);
  if (ret == OB_SUCCESS) { min_value_ = candidate; min_meta_ = value.meta_; }
  return ret;
}
int ObOptColumnStat::set_max_value(const DatumValue &value)
{
  Datum candidate;
  const int ret = DatumParameterCodec::clone_schema_native_value(value.datum_, value.meta_, allocator_, candidate);
  if (ret == OB_SUCCESS) { max_value_ = candidate; max_meta_ = value.meta_; }
  return ret;
}
int ObOptColumnStat::set_min_native(const Datum &value, const SqlTypeMeta &meta)
{ return clone_stat_value(value, meta, allocator_, min_value_, min_meta_); }
int ObOptColumnStat::set_max_native(const Datum &value, const SqlTypeMeta &meta)
{ return clone_stat_value(value, meta, allocator_, max_value_, max_meta_); }
int ObOptColumnStat::export_min_value(DatumValue &output, void *scratch, int64_t capacity) const
{ return export_stat_value(min_value_, min_meta_, output, scratch, capacity); }
int ObOptColumnStat::export_max_value(DatumValue &output, void *scratch, int64_t capacity) const
{ return export_stat_value(max_value_, max_meta_, output, scratch, capacity); }
int ObOptColumnStat::copy_min_value(DatumValue &output, ObIAllocator &owner) const
{ return copy_stat_value(min_value_, min_meta_, output, owner); }
int ObOptColumnStat::copy_max_value(DatumValue &output, ObIAllocator &owner) const
{ return copy_stat_value(max_value_, max_meta_, output, owner); }

int ObHistBucket::set_endpoint_borrowed(const DatumValue &value)
{
  Datum candidate;
  alignas(8) char scratch[64];
  ObDatum physical;
  int ret = value.export_legacy(physical, scratch, sizeof(scratch));
  if (ret == OB_SUCCESS) { ret = DatumParameterCodec::borrow_schema_native_value(physical, value.meta_, candidate); }
  if (ret == OB_SUCCESS) { endpoint_value_ = candidate; endpoint_meta_ = value.meta_; }
  return ret;
}
int ObHistBucket::set_endpoint_native_borrowed(const Datum &value, const SqlTypeMeta &meta)
{
  alignas(8) char scratch[64];
  DatumValue view;
  const int ret = export_stat_value(value, meta, view, scratch, sizeof(scratch));
  return ret == OB_SUCCESS ? set_endpoint_borrowed(view) : ret;
}
int ObHistBucket::export_endpoint_value(DatumValue &output, void *scratch, int64_t capacity) const
{ return export_stat_value(endpoint_value_, endpoint_meta_, output, scratch, capacity); }
int ObHistBucket::copy_endpoint_value(DatumValue &output, ObIAllocator &owner) const
{ return copy_stat_value(endpoint_value_, endpoint_meta_, output, owner); }
int64_t ObHistBucket::deep_copy_size() const
{ return stat_value_copy_size(endpoint_value_, endpoint_meta_); }
int ObHistBucket::deep_copy(const ObHistBucket &source, char *buffer, int64_t length, int64_t &pos)
{
  const int ret = copy_stat_value_to_buffer(source.endpoint_value_, source.endpoint_meta_,
      endpoint_value_, endpoint_meta_, buffer, length, pos);
  if (ret == OB_SUCCESS) { endpoint_repeat_count_ = source.endpoint_repeat_count_; endpoint_num_ = source.endpoint_num_; }
  return ret;
}
int ObHistBucket::deep_copy(ObIAllocator &owner, const ObHistBucket &source)
{
  const int ret = clone_stat_value(source.endpoint_value_, source.endpoint_meta_, owner,
                                  endpoint_value_, endpoint_meta_);
  if (ret == OB_SUCCESS) { endpoint_repeat_count_ = source.endpoint_repeat_count_; endpoint_num_ = source.endpoint_num_; }
  return ret;
}

DEF_TO_STRING(ObHistBucket)
{
  int64_t pos = 0;
  alignas(8) char scratch[64];
  DatumValue endpoint_value;
  const int ret = export_endpoint_value(endpoint_value, scratch, sizeof(scratch));
  J_OBJ_START();
  if (ret == OB_SUCCESS) { J_KV(K(endpoint_value), K_(endpoint_repeat_count), K_(endpoint_num)); }
  else { J_KV("endpoint_error", ret, K_(endpoint_value), K_(endpoint_meta)); }
  J_OBJ_END();
  return pos;
}

DEF_TO_STRING(ObOptColumnStat)
{
  int64_t pos = 0;
  alignas(8) char min_scratch[64], max_scratch[64];
  DatumValue min_value, max_value;
  const int min_ret = export_min_value(min_value, min_scratch, sizeof(min_scratch));
  const int max_ret = export_max_value(max_value, max_scratch, sizeof(max_scratch));
  J_OBJ_START();
  J_KV(K_(table_id), K_(partition_id), K_(column_id), K_(last_analyzed), K_(object_type),
       K_(num_distinct), K_(num_null));
  J_COMMA();
  if (min_ret == OB_SUCCESS && max_ret == OB_SUCCESS) { J_KV(K(min_value), K(max_value)); }
  else { J_KV(K(min_ret), K(max_ret), K_(min_value), K_(min_meta), K_(max_value), K_(max_meta)); }
  J_COMMA();
  J_KV(K_(num_not_null), K_(avg_length), K_(cs_type), K_(total_col_len),
       K_(llc_bitmap_size), K_(llc_bitmap), K_(histogram));
  J_OBJ_END();
  return pos;
}

void ObHistogram::reset()
{
  type_ = ObHistType::INVALID_TYPE;
  sample_size_ = -1;
  density_ = -1;
  bucket_cnt_ = 0;
  buckets_.reset();
}

const char *ObHistogram::get_type_name() const
{
  if (ObHistType::FREQUENCY == type_) {
    return "Frequence Histogram";
  } else if (ObHistType::HEIGHT_BALANCED == type_) {
    return "Height Balanced Histogram";
  } else if (ObHistType::TOP_FREQUENCY == type_) {
    return "Top Frequence Histogram";
  } else if (ObHistType::HYBIRD == type_) {
    return "Hybrid Histogram";
  } else {
    return "Invalid";
  }
}

int64_t ObHistogram::deep_copy_size() const
{
  int64_t size = sizeof(*this) + (buckets_.empty() ? 0 : alignof(ObHistBucket) - 1);
  for (int64_t i = 0; i < buckets_.count(); ++i) {
    size += sizeof(ObHistBucket) + buckets_.at(i).deep_copy_size();
  }
  return size;
}

int ObHistogram::deep_copy(const ObHistogram &src, char *buf, const int64_t buf_len, int64_t &pos)
{
  int ret = OB_SUCCESS;
  type_ = src.type_;
  sample_size_ = src.sample_size_;
  density_ = src.density_;
  bucket_cnt_ = src.bucket_cnt_;
  int64_t copy_size = src.deep_copy_size();
  if (OB_UNLIKELY(pos < 0 || pos > buf_len || copy_size > buf_len - pos)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (!src.buckets_.empty()) {
    const int64_t alignment = alignof(ObHistBucket);
    const int64_t padding = (alignment - (reinterpret_cast<uintptr_t>(buf + pos) % alignment)) % alignment;
    pos += padding;
    ObHistBucket *new_buckets = new (buf + pos) ObHistBucket[src.buckets_.count()];
    buckets_ = ObArrayWrap<ObHistBucket>(new_buckets, src.buckets_.count());
    pos += sizeof(ObHistBucket) * src.buckets_.count();
    for (int64_t i = 0; OB_SUCC(ret) && i < buckets_.count(); ++i) {
      if (OB_FAIL(buckets_.at(i).deep_copy(src.buckets_.at(i), buf, buf_len, pos))) {
      }
    }
  }
  return ret;
}

int ObHistogram::deep_copy(ObIAllocator &allocator, const ObHistogram &src)
{
  int ret = OB_SUCCESS;
  const int64_t count = src.buckets_.count();
  ObHistBucket *new_buckets = nullptr;
  if (count > 0) {
    void *ptr = allocator.alloc(sizeof(ObHistBucket) * count);
    if (ptr == nullptr) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to alloc memory", K(count));
    } else {
      new_buckets = new (ptr) ObHistBucket[count];
      for (int64_t i = 0; OB_SUCC(ret) && i < count; ++i) {
        ret = new_buckets[i].deep_copy(allocator, src.buckets_.at(i));
      }
    }
  }
  if (OB_SUCC(ret)) {
    type_ = src.type_;
    sample_size_ = src.sample_size_;
    density_ = src.density_;
    bucket_cnt_ = src.bucket_cnt_;
    buckets_ = ObArrayWrap<ObHistBucket>(new_buckets, count);
  }
  return ret;
}

int ObHistogram::prepare_allocate_buckets(ObIAllocator &allocator, const int64_t bucket_size)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(buckets_.allocate_array(allocator, bucket_size))) {
  }
  return ret;
}

int ObHistogram::add_bucket(const ObHistBucket &bucket, ObIAllocator &owner)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(bucket_cnt_ >= buckets_.count())) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    ObHistBucket owned;
    if (OB_FAIL(owned.deep_copy(owner, bucket))) {
    } else { buckets_.at(bucket_cnt_++) = owned; }
  }
  return ret;
}

int ObHistogram::assign_buckets(const ObIArray<ObHistBucket> &buckets, ObIAllocator &owner)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(buckets_.count() != buckets.count() || bucket_cnt_ != buckets.count())) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < buckets.count(); ++i) {
      ret = buckets_.at(i).deep_copy(owner, buckets.at(i));
    }
  }
  return ret;
}

int ObHistogram::assign(const ObHistogram &other, ObIAllocator &owner)
{
  const int ret = deep_copy(owner, other);
  if (ret == OB_SUCCESS) { pop_freq_ = other.pop_freq_; pop_count_ = other.pop_count_; }
  return ret;
}

ObOptColumnStat::ObOptColumnStat()
    : table_id_(OB_INVALID_ID),
      partition_id_(0),
      column_id_(0),
      object_type_(StatLevel::INVALID_LEVEL),
      version_(0),
      num_null_(0),
      num_not_null_(0),
      num_distinct_(0),
      avg_length_(0),
      min_value_(),
      max_value_(),
      min_meta_(),
      max_meta_(),
      llc_bitmap_size_(0),
      llc_bitmap_(NULL),
      histogram_(),
      last_analyzed_(0),
      cs_type_(CS_TYPE_INVALID),
      total_col_len_(0),
      inner_allocator_("ObOptColumnStat"),
      allocator_(inner_allocator_)
{
  clear_min_value();
  clear_max_value();
}

ObOptColumnStat::ObOptColumnStat(ObIAllocator &allocator)
    : table_id_(OB_INVALID_ID),
      partition_id_(0),
      column_id_(0),
      object_type_(StatLevel::INVALID_LEVEL),
      version_(0),
      num_null_(0),
      num_not_null_(0),
      num_distinct_(0),
      avg_length_(0),
      min_value_(),
      max_value_(),
      min_meta_(),
      max_meta_(),
      llc_bitmap_size_(0),
      llc_bitmap_(NULL),
      histogram_(),
      last_analyzed_(0),
      cs_type_(CS_TYPE_INVALID),
      total_col_len_(0),
      inner_allocator_("ObOptColumnStat"),
      allocator_(allocator)
{
  clear_min_value();
  clear_max_value();
  if (NULL == (llc_bitmap_ = static_cast<char*>(allocator.alloc(ObOptColumnStat::NUM_LLC_BUCKET)))) {
    COMMON_LOG_RET(WARN, OB_ALLOCATE_MEMORY_FAILED, "allocate memory for llc_bitmap_ failed.");
  } else {
    llc_bitmap_size_ = ObOptColumnStat::NUM_LLC_BUCKET;
    MEMSET(llc_bitmap_, 0, llc_bitmap_size_);
  }
}

void ObOptColumnStat::reset()
{
  table_id_ = 0;
  partition_id_ = 0;
  column_id_ = 0;
  object_type_ = StatLevel::INVALID_LEVEL;
  version_ = 0;
  num_null_ = 0;
  num_not_null_ = 0;
  num_distinct_ = 0;
  avg_length_ = 0;
  llc_bitmap_size_ = 0;
  llc_bitmap_ = NULL;
  last_analyzed_ = 0;
  cs_type_ = CS_TYPE_INVALID;
  total_col_len_ = 0;
  histogram_.reset();
}

int64_t ObOptColumnStat::size() const
{
  int64_t base_size = sizeof(ObOptColumnStat);
  const int64_t minimum_size = stat_value_copy_size(min_value_, min_meta_);
  const int64_t maximum_size = stat_value_copy_size(max_value_, max_meta_);
  if (minimum_size == INT64_MAX || maximum_size > INT64_MAX - minimum_size - base_size) { return INT64_MAX; }
  base_size += minimum_size + maximum_size;
  base_size += histogram_.deep_copy_size();
  base_size += llc_bitmap_size_;

  return base_size;
}

int ObOptColumnStat::deep_copy(char *buf, const int64_t buf_len, ObIKVCacheValue *&value) const
{
  int ret = OB_SUCCESS;
  if (nullptr == buf || buf_len < size()) {
    ret = OB_INVALID_ARGUMENT;
    COMMON_LOG(WARN, "invalid arguments.",
               KP(buf), K(buf_len), K(size()), K(ret));
  } else {
    ObOptColumnStat *stat = new (buf) ObOptColumnStat();
    int64_t pos = sizeof(*this);
    if (OB_FAIL(stat->deep_copy(*this, buf, buf_len, pos))) {
    } else {
      value = stat;
    }
  }
  return ret;
}

int ObOptColumnStat::deep_copy(const ObOptColumnStat &src)
{
  int ret = OB_SUCCESS;
  table_id_ = src.table_id_;
  partition_id_ = src.partition_id_;
  column_id_ = src.column_id_;
  object_type_ = src.object_type_;
  version_ = src.version_;
  num_null_ = src.num_null_;
  num_not_null_ = src.num_not_null_;
  num_distinct_ = src.num_distinct_;
  avg_length_ = src.avg_length_;
  last_analyzed_ = src.last_analyzed_;
  cs_type_ = src.cs_type_;
  llc_bitmap_size_ = src.llc_bitmap_size_;
  total_col_len_ = src.total_col_len_;
  if (OB_FAIL(set_min_native(src.min_value_, src.min_meta_))) {
  } else if (OB_FAIL(set_max_native(src.max_value_, src.max_meta_))) {
  } else if (OB_FAIL(histogram_.deep_copy(allocator_, src.histogram_))) {
  } else if (src.llc_bitmap_size_ != 0 && src.llc_bitmap_ != NULL) {
    char* ptr = static_cast<char*>(allocator_.alloc(src.llc_bitmap_size_));
    if (OB_ISNULL(ptr)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to allocate memory for llc_bitmap_");
    } else {
      MEMCPY(ptr, src.llc_bitmap_, src.llc_bitmap_size_);
      llc_bitmap_ = ptr;
    }
  }
  return ret;
}

int ObOptColumnStat::deep_copy_llc_bitmap(const char *bitmap, const int64_t size)
{
  int ret = OB_SUCCESS;
  if (size != 0 && bitmap != NULL) {
    if (OB_ISNULL(llc_bitmap_ = static_cast<char*>(allocator_.alloc(size)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to allocate memory for llc_bitmap_");
    } else {
      MEMCPY(llc_bitmap_, bitmap, size);
      llc_bitmap_size_ = size;
    }
  }
  return ret;
}

int ObOptColumnStat::deep_copy_histogram(const ObHistogram &hist)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(histogram_.deep_copy(allocator_, hist))) {
  }
  return ret;
}

int ObOptColumnStat::deep_copy(const ObOptColumnStat &src, char *buf, const int64_t size, int64_t &pos)
{
  int ret = OB_SUCCESS;

  table_id_ = src.table_id_;
  partition_id_ = src.partition_id_;
  column_id_ = src.column_id_;
  object_type_ = src.object_type_;
  version_ = src.version_;
  num_null_ = src.num_null_;
  num_not_null_ = src.num_not_null_;
  num_distinct_ = src.num_distinct_;
  avg_length_ = src.avg_length_;
  last_analyzed_ = src.last_analyzed_;
  cs_type_ = src.cs_type_;
  total_col_len_ = src.total_col_len_;
  if (!src.is_valid() || nullptr == buf || size <= 0) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(copy_stat_value_to_buffer(src.min_value_, src.min_meta_, min_value_, min_meta_, buf, size, pos))) {
  } else if (OB_FAIL(copy_stat_value_to_buffer(src.max_value_, src.max_meta_, max_value_, max_meta_, buf, size, pos))) {
  } else if (OB_FAIL(histogram_.deep_copy(src.histogram_, buf, size, pos))) {
  } else if (pos + src.llc_bitmap_size_ > size) {
    ret = OB_BUF_NOT_ENOUGH;
  } else {
    llc_bitmap_ = buf + pos;
    llc_bitmap_size_ = src.llc_bitmap_size_;
    MEMCPY(llc_bitmap_, src.llc_bitmap_, src.llc_bitmap_size_);
    pos += llc_bitmap_size_;
  }
  return ret;
}

int ObOptColumnStat::assign(const ObOptColumnStat &other)
{
  int ret = OB_SUCCESS;
  table_id_ = other.table_id_;
  partition_id_ = other.partition_id_;
  column_id_ = other.column_id_;
  object_type_ = other.object_type_;
  version_ = other.version_;
  num_null_ = other.num_null_;
  num_not_null_ = other.num_not_null_;
  num_distinct_ = other.num_distinct_;
  avg_length_ = other.avg_length_;
  if (OB_FAIL(set_min_native(other.min_value_, other.min_meta_))) {
  } else if (OB_FAIL(set_max_native(other.max_value_, other.max_meta_))) {
  }
  llc_bitmap_size_ = other.llc_bitmap_size_;
  llc_bitmap_ = other.llc_bitmap_;
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(histogram_.assign(other.histogram_, allocator_))) {
  } else {
    last_analyzed_ = other.last_analyzed_;
    cs_type_ = other.cs_type_;
    total_col_len_ = other.total_col_len_;
  }
  return ret;
}

/**
 * @brief ObHistogram::calc_density
 *  basically, we assume non-popular values are uniformly distributed
 * @param row_count
 * @param pop_row_count
 * @param ndv
 * @param pop_ndv
 */
void ObHistogram::calc_density(ObHistType hist_type,
                               const int64_t row_count,
                               const int64_t pop_row_count,
                               const int64_t ndv,
                               const int64_t pop_ndv)
{
  if (ObHistType::FREQUENCY == hist_type ||
      ObHistType::TOP_FREQUENCY == hist_type ||
      ndv <= pop_ndv || row_count == pop_row_count ) {
    density_ = 1.0 / (row_count * 2);
  } else {
    density_ = (1.0 * (row_count - pop_row_count)) / ((ndv - pop_ndv) * row_count);
  }
}

int ObOptColumnStat::merge_column_stat(const ObOptColumnStat &other)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(get_table_id() != other.get_table_id() ||
                  get_partition_id() != other.get_partition_id() ||
                  get_column_id() != other.get_column_id())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("the key not match", K(get_table_id()), K(get_partition_id()), K(get_column_id()));
  } else {
    if (OB_FAIL(merge_min_max(min_value_, min_meta_, other.min_value_, other.min_meta_, true))) {
    } else if (OB_FAIL(merge_min_max(max_value_, max_meta_, other.max_value_, other.max_meta_, false))) {
    } else {
      add_num_null(other.get_num_null());
      add_num_not_null(other.get_num_not_null());
      add_col_len(other.get_total_col_len());
      calc_avg_len();
      if (get_llc_bitmap_size() == other.get_llc_bitmap_size()) {
        ObGlobalNdvEval::update_llc(get_llc_bitmap(), other.get_llc_bitmap());
      }
    }
  }
  return ret;
}

/**
 * @brief 
 * 
 * @param cur 
 * @param other 
 * @param is_cmp_min true: merge min, false: merge max
 * @return int 
 */
int ObOptColumnStat::merge_min_max(Datum &cur, SqlTypeMeta &cur_meta, const Datum &other,
                                   const SqlTypeMeta &other_meta, bool is_cmp_min)
{
  int ret = OB_SUCCESS;
  int cmp = 0;
  if (cur.is_null()) {
    ret = clone_stat_value(other, other_meta, allocator_, cur, cur_meta);
  } else if (!other.is_null()) {
    alignas(8) char cur_scratch[64], other_scratch[64];
    DatumValue current_view, other_view;
    if (OB_FAIL(export_stat_value(other, other_meta, other_view, other_scratch, sizeof(other_scratch)))) {
    } else if (OB_FAIL(export_stat_value(cur, cur_meta, current_view, cur_scratch, sizeof(cur_scratch)))) {
    } else if (OB_FAIL(compare_datum_values(other_view, current_view, cmp))) {
    } else if ((is_cmp_min && cmp < 0) || (!is_cmp_min && cmp > 0)) {
      ret = clone_stat_value(other, other_meta, allocator_, cur, cur_meta);
    }
  }
  return ret;
}

ObOptColumnStat *ObOptColumnStat::malloc_new_column_stat(common::ObIAllocator &allocator)
{
  ObOptColumnStat *new_col_stat = OB_NEWx(ObOptColumnStat, (&allocator), allocator);
  if (new_col_stat != NULL) {
    if (OB_ISNULL(new_col_stat->get_llc_bitmap())) {
      new_col_stat->~ObOptColumnStat();
      allocator.free(new_col_stat);
      new_col_stat = NULL;
    }
  }
  return new_col_stat;
}

OB_DEF_SERIALIZE(ObOptColumnStat) {
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_ENCODE, table_id_, partition_id_, column_id_, num_distinct_, num_null_, num_not_null_);
  if (OB_SUCC(ret)) { ret = DatumParameterCodec::encode_schema_native_value(min_value_, min_meta_, buf, buf_len, pos); }
  if (OB_SUCC(ret)) { ret = DatumParameterCodec::encode_schema_native_value(max_value_, max_meta_, buf, buf_len, pos); }
  LST_DO_CODE(OB_UNIS_ENCODE, llc_bitmap_size_, avg_length_, object_type_);
  if (llc_bitmap_size_ != 0 && llc_bitmap_size_ < buf_len - pos) {
    MEMCPY(buf + pos, llc_bitmap_, llc_bitmap_size_);
    pos += llc_bitmap_size_;
  }
  OB_UNIS_ENCODE(total_col_len_);
  return ret;
}

OB_DEF_SERIALIZE_SIZE(ObOptColumnStat) {
  int64_t len = 0;
  LST_DO_CODE(OB_UNIS_ADD_LEN, table_id_, partition_id_, column_id_, num_distinct_, num_null_, num_not_null_);
  len += stat_value_wire_size(min_value_, min_meta_) + stat_value_wire_size(max_value_, max_meta_);
  LST_DO_CODE(OB_UNIS_ADD_LEN, llc_bitmap_size_, avg_length_, object_type_);
  if (llc_bitmap_size_ != 0) { len += llc_bitmap_size_; }
  OB_UNIS_ADD_LEN(total_col_len_);
  return len;
}

OB_DEF_DESERIALIZE(ObOptColumnStat) {
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_DECODE, table_id_, partition_id_, column_id_, num_distinct_, num_null_, num_not_null_);
  if (OB_SUCC(ret) && pos < data_len) {
    ret = DatumParameterCodec::decode_unis_native_value(buf, data_len, pos, allocator_, min_value_, min_meta_);
  }
  if (OB_SUCC(ret) && pos < data_len) {
    ret = DatumParameterCodec::decode_unis_native_value(buf, data_len, pos, allocator_, max_value_, max_meta_);
  }
  LST_DO_CODE(OB_UNIS_DECODE, llc_bitmap_size_, avg_length_, object_type_);
  if (llc_bitmap_size_ != 0 && data_len - pos >= llc_bitmap_size_) {
    if (OB_ISNULL(llc_bitmap_)) { ret = OB_ERR_UNEXPECTED; }
    else { MEMCPY(llc_bitmap_, buf + pos, llc_bitmap_size_); pos += llc_bitmap_size_; }
  }
  OB_UNIS_DECODE(total_col_len_);
  return ret;
}

}
}
