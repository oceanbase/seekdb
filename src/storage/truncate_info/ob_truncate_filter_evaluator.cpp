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

#define USING_LOG_PREFIX STORAGE
#include "storage/truncate_info/ob_truncate_filter_evaluator.h"
#include "data_plane/truncate_info/ob_truncate_info.h"
#include "data_plane/truncate_info/ob_truncate_info_array.h"
#include "share/schema/ob_column_schema.h"
#include "storage/blocksstable/ob_datum_row.h"
#include "storage/blocksstable/ob_micro_block_row_scanner.h"
#include "storage/ob_storage_util.h"

namespace oceanbase
{
using namespace common;
using namespace share::schema;
using namespace blocksstable;

namespace storage
{

struct ObTruncateFilterEvaluator::Impl
{
  struct CompiledPartition
  {
    CompiledPartition()
      : type_(ObTruncatePartition::PART_TYPE_MAX),
        op_(ObTruncatePartition::PART_OP_MAX),
        key_count_(0),
        row_indexes_(nullptr),
        projected_indexes_(nullptr),
        compare_functions_(nullptr),
        low_unbounded_(false),
        high_unbounded_(false),
        low_values_(nullptr),
        high_values_(nullptr),
        list_row_count_(0),
        list_values_(nullptr),
        hash_function_(nullptr),
        hash_bucket_count_(0),
        hash_bucket_heads_(nullptr),
        hash_next_(nullptr),
        has_null_list_value_(false)
    {}

    ObTruncatePartition::TruncatePartType type_;
    ObTruncatePartition::TruncatePartOp op_;
    int64_t key_count_;
    int32_t *row_indexes_;
    int32_t *projected_indexes_;
    ObDatumCmpFuncType *compare_functions_;
    bool low_unbounded_;
    bool high_unbounded_;
    ObStorageDatum *low_values_;
    ObStorageDatum *high_values_;
    int64_t list_row_count_;
    ObStorageDatum *list_values_;
    ObDatumHashFuncType hash_function_;
    int64_t hash_bucket_count_;
    int32_t *hash_bucket_heads_;
    int32_t *hash_next_;
    bool has_null_list_value_;
  };

  struct CompiledInfo
  {
    CompiledInfo()
      : commit_version_(0),
        version_row_index_(-1),
        version_projected_index_(-1),
        has_subpartition_(false),
        partition_(),
        subpartition_()
    {}

    int64_t commit_version_;
    int32_t version_row_index_;
    int32_t version_projected_index_;
    bool has_subpartition_;
    CompiledPartition partition_;
    CompiledPartition subpartition_;
    TO_STRING_KV(K_(commit_version), K_(version_row_index), K_(version_projected_index),
                 K_(has_subpartition));
  };

  explicit Impl(ObIAllocator &allocator)
    : allocator_(allocator),
      schema_rowkey_count_(-1),
      referenced_columns_(),
      infos_()
  {
    referenced_columns_.set_attr(ObMemAttr("TruncRefCols"));
    infos_.set_attr(ObMemAttr("TruncFilters"));
  }

  template <typename T>
  T *allocate_array(const int64_t count)
  {
    T *array = nullptr;
    if (count > 0) {
      void *buf = allocator_.alloc(sizeof(T) * count);
      if (nullptr != buf) {
        array = static_cast<T *>(buf);
        for (int64_t i = 0; i < count; ++i) {
          new (array + i) T();
        }
      }
    }
    return array;
  }

  int projected_index(const int32_t row_index, int32_t &projected_index)
  {
    int ret = OB_SUCCESS;
    projected_index = -1;
    for (int64_t i = 0; i < referenced_columns_.count(); ++i) {
      if (referenced_columns_.at(i) == row_index) {
        projected_index = static_cast<int32_t>(i);
        break;
      }
    }
    if (projected_index < 0) {
      if (OB_FAIL(referenced_columns_.push_back(row_index))) {
      } else {
        projected_index = static_cast<int32_t>(referenced_columns_.count() - 1);
      }
    }
    return ret;
  }

  int compile_values(
      const ObRowkey &rowkey,
      const int64_t key_count,
      ObStorageDatum *&values)
  {
    int ret = OB_SUCCESS;
    values = nullptr;
    if (OB_UNLIKELY(rowkey.get_obj_cnt() != key_count)) {
      ret = OB_INVALID_DATA;
      LOG_WARN("truncate bound does not match partition key count", K(ret), K(rowkey), K(key_count));
    } else if (OB_ISNULL(values = allocate_array<ObStorageDatum>(key_count))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to allocate compiled truncate values", K(ret), K(key_count));
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < key_count; ++i) {
      values[i].reuse();
      if (OB_FAIL(values[i].from_obj(rowkey.get_obj_ptr()[i]))) {
      } else if (OB_UNLIKELY(values[i].is_null_or_nop())) {
        ret = OB_INVALID_DATA;
        LOG_WARN("truncate range bound contains null or nop", K(ret), K(i), K(rowkey));
      }
    }
    return ret;
  }

  int compile_partition(
      const ObTruncatePartition &source,
      const ObIArray<ObColDesc> &columns,
      CompiledPartition &compiled)
  {
    int ret = OB_SUCCESS;
    compiled.type_ = source.part_type_;
    compiled.op_ = source.part_op_;
    compiled.key_count_ = source.part_key_idxs_.count();
    if (OB_UNLIKELY(!source.is_valid() || compiled.key_count_ <= 0)) {
      ret = OB_INVALID_DATA;
      LOG_WARN("invalid truncate partition", K(ret), K(source));
    } else if (OB_ISNULL(compiled.row_indexes_ = allocate_array<int32_t>(compiled.key_count_)) ||
               OB_ISNULL(compiled.projected_indexes_ = allocate_array<int32_t>(compiled.key_count_)) ||
               OB_ISNULL(compiled.compare_functions_ = allocate_array<ObDatumCmpFuncType>(compiled.key_count_))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to allocate compiled truncate partition", K(ret), K_(compiled.key_count));
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < compiled.key_count_; ++i) {
      const int64_t row_index = source.part_key_idxs_.at(i);
      if (OB_UNLIKELY(row_index < 0 || row_index >= columns.count())) {
        ret = OB_INVALID_DATA;
        LOG_WARN("truncate partition column is out of range", K(ret), K(row_index), K(columns.count()));
      } else {
        compiled.row_indexes_[i] = static_cast<int32_t>(row_index);
        compiled.compare_functions_[i] = get_datum_cmp_func(
            columns.at(row_index).col_type_,
            columns.at(row_index).col_type_);
        if (OB_ISNULL(compiled.compare_functions_[i])) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("failed to resolve truncate comparison function", K(ret), K(row_index));
        } else if (OB_FAIL(projected_index(
                       compiled.row_indexes_[i],
                       compiled.projected_indexes_[i]))) {
        }
      }
    }

    if (OB_FAIL(ret)) {
    } else if (ObTruncatePartition::is_range_part(source.part_type_)) {
      compiled.low_unbounded_ = source.low_bound_val_.is_min_row();
      compiled.high_unbounded_ = source.high_bound_val_.is_max_row();
      if (!compiled.low_unbounded_ &&
          OB_FAIL(compile_values(source.low_bound_val_, compiled.key_count_, compiled.low_values_))) {
        LOG_WARN("failed to compile truncate lower bound", K(ret));
      } else if (!compiled.high_unbounded_ &&
                 OB_FAIL(compile_values(source.high_bound_val_, compiled.key_count_, compiled.high_values_))) {
        LOG_WARN("failed to compile truncate upper bound", K(ret));
      }
    } else if (ObTruncatePartition::is_list_part(source.part_type_)) {
      compiled.list_row_count_ = source.list_row_values_.count();
      if (ObTruncatePartition::ALL == source.part_op_) {
      } else if (OB_UNLIKELY(compiled.list_row_count_ <= 0 ||
                             nullptr == source.list_row_values_.get_values())) {
        ret = OB_INVALID_DATA;
        LOG_WARN("truncate list partition has no values", K(ret), K(source));
      } else if (OB_ISNULL(compiled.list_values_ = allocate_array<ObStorageDatum>(
                     compiled.list_row_count_ * compiled.key_count_))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("failed to allocate compiled truncate list", K(ret));
      } else {
        int64_t datum_index = 0;
        const ObNewRow *rows = source.list_row_values_.get_values();
        for (int64_t i = 0; OB_SUCC(ret) && i < compiled.list_row_count_; ++i) {
          if (OB_UNLIKELY(rows[i].get_count() != compiled.key_count_)) {
            ret = OB_INVALID_DATA;
            LOG_WARN("truncate list row has wrong arity", K(ret), K(i), K(rows[i]), K_(compiled.key_count));
          }
          for (int64_t j = 0; OB_SUCC(ret) && j < compiled.key_count_; ++j, ++datum_index) {
            compiled.list_values_[datum_index].reuse();
            if (OB_FAIL(compiled.list_values_[datum_index].from_obj(rows[i].get_cell(j)))) {
            } else if (OB_UNLIKELY(compiled.list_values_[datum_index].is_nop())) {
              ret = OB_INVALID_DATA;
              LOG_WARN("truncate list contains nop", K(ret), K(i), K(j));
            }
          }
        }
        if (OB_SUCC(ret) && 1 == compiled.key_count_) {
          int64_t bucket_count = 1;
          while (bucket_count < compiled.list_row_count_ * 2) {
            bucket_count <<= 1;
          }
          compiled.hash_function_ = get_datum_hash_func(columns.at(compiled.row_indexes_[0]).col_type_);
          compiled.hash_bucket_count_ = bucket_count;
          if (OB_ISNULL(compiled.hash_function_)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("failed to resolve truncate list hash function", K(ret), K(compiled.row_indexes_[0]));
          } else if (OB_ISNULL(compiled.hash_bucket_heads_ = allocate_array<int32_t>(bucket_count)) ||
                     OB_ISNULL(compiled.hash_next_ = allocate_array<int32_t>(compiled.list_row_count_))) {
            ret = OB_ALLOCATE_MEMORY_FAILED;
            LOG_WARN("failed to allocate truncate list hash table", K(ret), K(bucket_count));
          } else {
            for (int64_t i = 0; i < bucket_count; ++i) {
              compiled.hash_bucket_heads_[i] = -1;
            }
            for (int64_t i = 0; OB_SUCC(ret) && i < compiled.list_row_count_; ++i) {
              compiled.hash_next_[i] = -1;
              const ObStorageDatum &datum = compiled.list_values_[i];
              if (datum.is_null()) {
                compiled.has_null_list_value_ = true;
              } else {
                uint64_t hash = 0;
                if (OB_FAIL(compiled.hash_function_(datum, 0, hash, nullptr))) {
                } else {
                  const int64_t bucket = hash & (bucket_count - 1);
                  compiled.hash_next_[i] = compiled.hash_bucket_heads_[bucket];
                  compiled.hash_bucket_heads_[bucket] = static_cast<int32_t>(i);
                }
              }
            }
          }
        }
      }
    } else {
      ret = OB_INVALID_DATA;
      LOG_WARN("unknown truncate partition type", K(ret), K(source.part_type_));
    }
    return ret;
  }

  int compile(
      const int64_t schema_rowkey_count,
      const ObIArray<ObColDesc> &columns,
      const ObTruncateInfoArray &truncate_infos)
  {
    int ret = OB_SUCCESS;
    if (OB_UNLIKELY(schema_rowkey_count <= 0 ||
                    schema_rowkey_count >= columns.count() ||
                    truncate_infos.empty())) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid truncate evaluator input", K(ret), K(schema_rowkey_count), K(columns.count()), K(truncate_infos));
    } else {
      schema_rowkey_count_ = schema_rowkey_count;
      int32_t version_projected_index = -1;
      if (OB_FAIL(projected_index(static_cast<int32_t>(schema_rowkey_count), version_projected_index))) {
      }
      for (int64_t i = 0; OB_SUCC(ret) && i < truncate_infos.count(); ++i) {
        const ObTruncateInfo *source = truncate_infos.at(i);
        CompiledInfo *compiled = nullptr;
        if (OB_ISNULL(source) || OB_UNLIKELY(!source->is_valid())) {
          ret = OB_INVALID_DATA;
          LOG_WARN("invalid truncate info", K(ret), K(i), KPC(source));
        } else if (OB_ISNULL(compiled = allocate_array<CompiledInfo>(1))) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          LOG_WARN("failed to allocate compiled truncate info", K(ret));
        } else {
          compiled->commit_version_ = source->commit_version_;
          compiled->version_row_index_ = static_cast<int32_t>(schema_rowkey_count);
          compiled->version_projected_index_ = version_projected_index;
          compiled->has_subpartition_ = source->is_sub_part_;
          if (OB_FAIL(compile_partition(source->truncate_part_, columns, compiled->partition_))) {
          } else if (compiled->has_subpartition_ &&
                     OB_FAIL(compile_partition(source->truncate_subpart_, columns, compiled->subpartition_))) {
            LOG_WARN("failed to compile truncate subpartition", K(ret), K(i));
          } else if (OB_FAIL(infos_.push_back(compiled))) {
          }
        }
      }
    }
    return ret;
  }

  const ObStorageDatum &datum_at(
      const ObStorageDatum *datums,
      const CompiledPartition &partition,
      const int64_t key_index,
      const bool projected) const
  {
    const int32_t index = projected
        ? partition.projected_indexes_[key_index]
        : partition.row_indexes_[key_index];
    return datums[index];
  }

  int compare_tuple(
      const ObStorageDatum *datums,
      const CompiledPartition &partition,
      const ObStorageDatum *values,
      const bool projected,
      int &comparison) const
  {
    int ret = OB_SUCCESS;
    comparison = 0;
    for (int64_t i = 0; OB_SUCC(ret) && i < partition.key_count_ && 0 == comparison; ++i) {
      const ObStorageDatum &datum = datum_at(datums, partition, i, projected);
      if (OB_UNLIKELY(datum.is_nop())) {
        ret = OB_INVALID_DATA;
        LOG_WARN("truncate key datum is nop", K(ret), K(i));
      } else if (datum.is_null()) {
        comparison = -1;
      } else if (OB_FAIL(partition.compare_functions_[i](
                     datum, values[i], comparison, nullptr))) {
      }
    }
    return ret;
  }

  int matches_partition(
      const ObStorageDatum *datums,
      const CompiledPartition &partition,
      const bool projected,
      bool &matches) const
  {
    int ret = OB_SUCCESS;
    matches = false;
    if (ObTruncatePartition::is_range_part(partition.type_)) {
      bool above_low = partition.low_unbounded_;
      bool below_high = partition.high_unbounded_;
      int comparison = 0;
      if (!above_low && OB_FAIL(compare_tuple(datums, partition, partition.low_values_, projected, comparison))) {
        LOG_WARN("failed to compare truncate lower bound", K(ret));
      } else if (!above_low) {
        above_low = comparison >= 0;
      }
      if (OB_SUCC(ret) && !below_high &&
          OB_FAIL(compare_tuple(datums, partition, partition.high_values_, projected, comparison))) {
        LOG_WARN("failed to compare truncate upper bound", K(ret));
      } else if (OB_SUCC(ret) && !below_high) {
        below_high = comparison < 0;
      }
      if (OB_SUCC(ret)) {
        matches = above_low && below_high;
      }
    } else if (ObTruncatePartition::ALL == partition.op_) {
      matches = true;
    } else if (1 == partition.key_count_ && nullptr != partition.hash_bucket_heads_) {
      const ObStorageDatum &datum = datum_at(datums, partition, 0, projected);
      if (OB_UNLIKELY(datum.is_nop())) {
        ret = OB_INVALID_DATA;
        LOG_WARN("truncate key datum is nop", K(ret));
      } else if (datum.is_null()) {
        matches = partition.has_null_list_value_;
      } else {
        uint64_t hash = 0;
        if (OB_FAIL(partition.hash_function_(datum, 0, hash, nullptr))) {
        } else {
          int32_t entry = partition.hash_bucket_heads_[hash & (partition.hash_bucket_count_ - 1)];
          while (OB_SUCC(ret) && entry >= 0 && !matches) {
            int comparison = 0;
            if (OB_FAIL(partition.compare_functions_[0](
                    datum, partition.list_values_[entry], comparison, nullptr))) {
            } else {
              matches = 0 == comparison;
              entry = partition.hash_next_[entry];
            }
          }
        }
      }
    } else {
      for (int64_t row_index = 0;
           OB_SUCC(ret) && row_index < partition.list_row_count_ && !matches;
           ++row_index) {
        matches = true;
        for (int64_t key_index = 0;
             OB_SUCC(ret) && key_index < partition.key_count_ && matches;
             ++key_index) {
          const ObStorageDatum &datum = datum_at(datums, partition, key_index, projected);
          const ObStorageDatum &value = partition.list_values_[
              row_index * partition.key_count_ + key_index];
          if (OB_UNLIKELY(datum.is_nop())) {
            ret = OB_INVALID_DATA;
            LOG_WARN("truncate key datum is nop", K(ret), K(key_index));
          } else if (datum.is_null() || value.is_null()) {
            matches = datum.is_null() && value.is_null();
          } else {
            int comparison = 0;
            if (OB_FAIL(partition.compare_functions_[key_index](
                    datum, value, comparison, nullptr))) {
            } else {
              matches = 0 == comparison;
            }
          }
        }
      }
    }
    // EXCEPT is a set operation on the complete partition predicate.  The old
    // query-owned range implementation negated each bound separately, making
    // a bounded EXCEPT range unsatisfiable; the metadata contract requires
    // complementing membership as a whole.
    if (OB_SUCC(ret) && ObTruncatePartition::EXCEPT == partition.op_) {
      matches = !matches;
    }
    return ret;
  }

  int filter(
      const ObStorageDatum *datums,
      const int64_t datum_count,
      const bool projected,
      bool &filtered) const
  {
    int ret = OB_SUCCESS;
    filtered = false;
    const int64_t required_count = projected ? referenced_columns_.count() : schema_rowkey_count_ + 1;
    if (OB_UNLIKELY(nullptr == datums || datum_count < required_count)) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("truncate row is too short", K(ret), KP(datums), K(datum_count), K(required_count));
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < infos_.count() && !filtered; ++i) {
      const CompiledInfo &info = *infos_.at(i);
      const int32_t version_index = projected
          ? info.version_projected_index_
          : info.version_row_index_;
      const ObStorageDatum &version_datum = datums[version_index];
      if (OB_UNLIKELY(version_datum.is_null_or_nop())) {
        ret = OB_INVALID_DATA;
        LOG_WARN("truncate version datum is null or nop", K(ret), K(i));
      } else {
        const int64_t stored_version = version_datum.get_int();
        if (OB_UNLIKELY(INT64_MIN == stored_version)) {
          ret = OB_INVALID_DATA;
          LOG_WARN("invalid truncate row version", K(ret), K(stored_version));
        } else {
          const int64_t version = stored_version < 0 ? -stored_version : stored_version;
          if (version <= info.commit_version_) {
            bool partition_matches = false;
            if (OB_FAIL(matches_partition(datums, info.partition_, projected, partition_matches))) {
            } else if (partition_matches && info.has_subpartition_) {
              if (OB_FAIL(matches_partition(datums, info.subpartition_, projected, partition_matches))) {
              }
            }
            if (OB_SUCC(ret)) {
              filtered = partition_matches;
            }
          }
        }
      }
    }
    return ret;
  }

  ObIAllocator &allocator_;
  int64_t schema_rowkey_count_;
  ObSEArray<int32_t, 8> referenced_columns_;
  ObSEArray<CompiledInfo *, 4> infos_;
};

ObTruncateFilterEvaluator::ObTruncateFilterEvaluator()
  : allocator_("TruncateEval", OB_MALLOC_NORMAL_BLOCK_SIZE),
    impl_(nullptr)
{}

ObTruncateFilterEvaluator::~ObTruncateFilterEvaluator()
{
  reset_impl();
}

void ObTruncateFilterEvaluator::reset_impl()
{
  if (nullptr != impl_) {
    impl_->~Impl();
    impl_ = nullptr;
  }
  allocator_.reuse();
}

int ObTruncateFilterEvaluator::rebuild(
    const int64_t schema_rowkey_count,
    const ObIArray<ObColDesc> &columns,
    const ObTruncateInfoArray &truncate_infos)
{
  int ret = OB_SUCCESS;
  reset_impl();
  void *buf = allocator_.alloc(sizeof(Impl));
  if (OB_ISNULL(buf)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("failed to allocate truncate evaluator", K(ret));
  } else {
    impl_ = new (buf) Impl(allocator_);
    if (OB_FAIL(impl_->compile(schema_rowkey_count, columns, truncate_infos))) {
      LOG_WARN("failed to compile truncate evaluator", K(ret));
      reset_impl();
    }
  }
  return ret;
}

int ObTruncateFilterEvaluator::init(
    const int64_t schema_rowkey_count,
    const ObIArray<ObColDesc> &columns,
    const ObTruncateInfoArray &truncate_infos)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(nullptr != impl_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("truncate evaluator initialized twice", K(ret));
  } else if (OB_FAIL(rebuild(schema_rowkey_count, columns, truncate_infos))) {
  }
  return ret;
}

int ObTruncateFilterEvaluator::switch_info(
    const int64_t schema_rowkey_count,
    const ObIArray<ObColDesc> &columns,
    const ObTruncateInfoArray &truncate_infos)
{
  return rebuild(schema_rowkey_count, columns, truncate_infos);
}

void ObTruncateFilterEvaluator::reuse()
{
  reset_impl();
}

int ObTruncateFilterEvaluator::filter(const ObDatumRow &row, bool &filtered) const
{
  int ret = OB_SUCCESS;
  filtered = false;
  if (OB_ISNULL(impl_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("truncate evaluator is not initialized", K(ret));
  } else if (row.row_flag_.is_delete() || row.row_flag_.is_lock()) {
  } else if (OB_FAIL(impl_->filter(row.storage_datums_, row.count_, false, filtered))) {
  }
  return ret;
}

int ObTruncateFilterEvaluator::filter_projected(
    const ObStorageDatum *datums,
    const int64_t datum_count,
    bool &filtered) const
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(impl_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("truncate evaluator is not initialized", K(ret));
  } else if (OB_FAIL(impl_->filter(datums, datum_count, true, filtered))) {
  }
  return ret;
}

int64_t ObTruncateFilterEvaluator::referenced_column_count() const
{
  return nullptr == impl_ ? 0 : impl_->referenced_columns_.count();
}

int32_t ObTruncateFilterEvaluator::referenced_column(const int64_t index) const
{
  OB_ASSERT(nullptr != impl_ && index >= 0 && index < impl_->referenced_columns_.count());
  return impl_->referenced_columns_.at(index);
}

int ObTruncateFilterEvaluator::execute(sql::ObExternalFilterExecutionContext &context)
{
  int ret = OB_SUCCESS;
  blocksstable::ObIMicroBlockRowScanner *scanner =
      static_cast<blocksstable::ObIMicroBlockRowScanner *>(context.native_batch());
  if (OB_ISNULL(scanner)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("external truncate batch is null", K(ret));
  } else if (OB_FAIL(scanner->filter_truncate_evaluator(
                 *this,
                 context.start(),
                 context.count(),
                 context.candidate_rows(),
                 context.result()))) {
  }
  return ret;
}

} // namespace storage
} // namespace oceanbase
