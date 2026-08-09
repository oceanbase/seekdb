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
#include "ob_truncate_partition_filter.h"
#include "share/schema/ob_list_row_values.h"
#include "query/engine/basic/ob_external_pushdown_filter.h"
#include "storage/compaction/ob_medium_compaction_func.h"
#include "storage/truncate_info/ob_truncate_info_array.h"
#include "storage/truncate_info/ob_truncate_filter_evaluator.h"
#include "storage/truncate_info/ob_mds_info_distinct_mgr.h"

namespace oceanbase
{
using namespace common;
using namespace sql;
using namespace share::schema;

namespace storage
{

ObTruncatePartitionFilter::ObTruncatePartitionFilter()
  : is_inited_(false),
    filter_type_(ObTruncateFilterType::FILTER_TYPE_MAX),
    schema_rowkey_cnt_(-1),
    base_version_(-1),
    has_combined_to_pd_filter_(false),
    filter_allocator_("TruncateFilter", OB_MALLOC_NORMAL_BLOCK_SIZE),
    truncate_info_allocator_("TruncateInfo", OB_MALLOC_NORMAL_BLOCK_SIZE),
    mds_info_mgr_(),
    truncate_info_array_(),
    evaluator_(nullptr),
    pushdown_runtime_(nullptr),
    outer_allocator_(nullptr),
    ref_column_idxs_()
{
  ref_column_idxs_.set_attr(ObMemAttr("TruncateFilter"));
}

ObTruncatePartitionFilter::~ObTruncatePartitionFilter()
{
  sql::destroy_external_pushdown_filter_runtime(filter_allocator_, pushdown_runtime_);
  if (nullptr != evaluator_) {
    evaluator_->~ObTruncateFilterEvaluator();
    filter_allocator_.free(evaluator_);
    evaluator_ = nullptr;
  }
}

int ObTruncatePartitionFilter::init(
    ObTablet &tablet,
    const ObIArray<ObColDesc> &cols_desc,
    const ObIArray<ObColumnParam *> *cols_param,
    const ObVersionRange &read_version_range,
    const bool has_truncate_flag,
    const bool has_truncate_info,
    ObIAllocator &outer_allocator)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret));
  } else if (FALSE_IT(outer_allocator_ = &outer_allocator)) {
  } else if (FALSE_IT(schema_rowkey_cnt_ = tablet.get_rowkey_read_info().get_schema_rowkey_count())) {
  } else if (has_truncate_flag && !has_truncate_info) {
    filter_type_ = ObTruncateFilterType::BASE_VERSION_FILTER;
    is_inited_ = true;
  } else if (OB_FAIL(mds_info_mgr_.init(truncate_info_allocator_, tablet, read_version_range, true/*for_access*/))) {
  } else if (mds_info_mgr_.empty()) {
    filter_type_ = has_truncate_flag ? ObTruncateFilterType::BASE_VERSION_FILTER : ObTruncateFilterType::EMPTY_FILTER;
    is_inited_ = true;
  } else if (OB_FAIL(init(schema_rowkey_cnt_, cols_desc, cols_param, mds_info_mgr_))) {
  }
  if (OB_SUCC(ret)) {
    base_version_ = read_version_range.base_version_;
  }
  LOG_INFO("[TRUNCATE INFO]", K(ret), K(tablet), K(cols_desc), K(read_version_range), KPC(this));
  return ret;
}

int ObTruncatePartitionFilter::init(
    const int64_t schema_rowkey_cnt,
    const ObIArray<ObColDesc> &cols_desc,
    const ObIArray<ObColumnParam *> *cols_param,
    const ObMdsInfoDistinctMgr &mds_info_mgr)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret));
  } else if (OB_UNLIKELY(mds_info_mgr.empty())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid empty mds info", K(ret));
  } else if (OB_FAIL(truncate_info_array_.init_for_first_creation(truncate_info_allocator_))) {
  } else if (OB_FAIL(mds_info_mgr.get_distinct_truncate_info_array(truncate_info_array_))) {
  } else if (OB_FAIL(init_truncate_filter(schema_rowkey_cnt, cols_desc, cols_param, truncate_info_array_))) {
  } else {
    filter_type_ = ObTruncateFilterType::NORMAL_FILTER;
  }
  LOG_INFO("[TRUNCATE INFO]", K(ret), K(schema_rowkey_cnt), K(cols_desc), K(mds_info_mgr), KPC(this));
  return ret;
}

int ObTruncatePartitionFilter::switch_info(
    ObTablet &tablet,
    const ObIArray<ObColDesc> &cols_desc,
    const ObIArray<ObColumnParam *> *cols_param,
    const ObVersionRange &read_version_range,
    const bool has_truncate_flag,
    const bool has_truncate_info)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (has_truncate_flag && !has_truncate_info) {
    filter_type_ = ObTruncateFilterType::BASE_VERSION_FILTER;
  } else if (OB_FAIL(mds_info_mgr_.init(truncate_info_allocator_, tablet, read_version_range, true/*for_access*/))) {
  } else if (mds_info_mgr_.empty()) {
    filter_type_ = has_truncate_flag ? ObTruncateFilterType::BASE_VERSION_FILTER : ObTruncateFilterType::EMPTY_FILTER;
  } else if (OB_FAIL(truncate_info_array_.init_for_first_creation(truncate_info_allocator_))) {
  } else if (OB_FAIL(mds_info_mgr_.get_distinct_truncate_info_array(truncate_info_array_))) {
  } else if (OB_UNLIKELY(schema_rowkey_cnt_ != tablet.get_rowkey_read_info().get_schema_rowkey_count())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected not equal schema rowkey cnt", K(ret), K_(schema_rowkey_cnt), K(tablet.get_rowkey_read_info().get_schema_rowkey_count()));
  } else if (OB_ISNULL(evaluator_)) {
    if (OB_FAIL(init_truncate_filter(schema_rowkey_cnt_, cols_desc, cols_param, truncate_info_array_))) {
    } else {
      filter_type_ = ObTruncateFilterType::NORMAL_FILTER;
    }
  } else if (OB_FAIL(evaluator_->switch_info(schema_rowkey_cnt_, cols_desc, truncate_info_array_))) {
  } else {
    filter_type_ = ObTruncateFilterType::NORMAL_FILTER;
  }
  if (OB_SUCC(ret)) {
    base_version_ = read_version_range.base_version_;
  }
  LOG_INFO("[TRUNCATE INFO]", K(ret), K(tablet), K(cols_desc), K(read_version_range), KPC(this));
  return ret;
}

void ObTruncatePartitionFilter::reuse()
{
  filter_type_ = ObTruncateFilterType::FILTER_TYPE_MAX;
  base_version_ = -1;
  truncate_info_array_.reset();
  mds_info_mgr_.reset();
  truncate_info_allocator_.reuse();
  if (OB_NOT_NULL(evaluator_)) {
    evaluator_->reuse();
  }
  if (OB_NOT_NULL(pushdown_runtime_)) {
    sql::detach_external_pushdown_filter(*pushdown_runtime_);
  }
  has_combined_to_pd_filter_ = false;
}

int ObTruncatePartitionFilter::filter(
    const ObDatumRow &row,
    bool &filtered,
    const bool check_filter,
    const bool check_version)
{
  int ret = OB_SUCCESS;
  filtered = false;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret));
  } else if (ObTruncateFilterType::NORMAL_FILTER == filter_type_) {
    if (check_filter && OB_FAIL(do_normal_filter(row, filtered))) {
      LOG_WARN("failed to do normal filter", K(ret), K(row));
    } else if (!filtered && check_version && OB_FAIL(do_base_version_filter(row, filtered))) {
      LOG_WARN("failed to do base version filter", K(ret), K(row));
    }
  } else if (ObTruncateFilterType::BASE_VERSION_FILTER == filter_type_) {
    if (check_version && OB_FAIL(do_base_version_filter(row, filtered))) {
      LOG_WARN("failed to do base version filter", K(ret), K(row));
    }
  } else if (ObTruncateFilterType::EMPTY_FILTER == filter_type_) {
  } else {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected state", K(ret), K_(filter_type), KPC(this));
  }
  return ret;
}

int ObTruncatePartitionFilter::do_normal_filter(const blocksstable::ObDatumRow &row, bool &filtered)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(row.row_flag_.is_delete() || row.row_flag_.is_lock())) {
    filtered = false;
  } else if (OB_ISNULL(evaluator_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("truncate evaluator is null", K(ret));
  } else if (OB_FAIL(evaluator_->filter(row, filtered))) {
  }
  return ret;
}

int ObTruncatePartitionFilter::do_base_version_filter(const blocksstable::ObDatumRow &row, bool &filtered)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(base_version_ < 0 || !row.is_valid() || schema_rowkey_cnt_ >= row.count_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected state", K(ret), K_(base_version), K(row), K_(schema_rowkey_cnt), KPC(this));
  } else {
    filtered = abs(row.storage_datums_[schema_rowkey_cnt_].get_int()) <= base_version_;
  }
  return ret;
}

int ObTruncatePartitionFilter::check_filter_row_complete(const blocksstable::ObDatumRow &row, bool &complete)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret));
  } else if (OB_UNLIKELY(!is_normal_filter())) {
    complete = true;
  } else if (OB_UNLIKELY(ref_column_idxs_.empty())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ref column idx is unexpected null", KR(ret), K_(ref_column_idxs));
  } else {
    complete = true;
    for (int64_t idx = 0; OB_SUCC(ret) && idx < ref_column_idxs_.count() && complete; ++idx) {
      const uint64_t ref_idx = ref_column_idxs_[idx];
      if (OB_UNLIKELY(ref_idx < 0 || ref_idx >= row.get_column_count()) ) {
        ret = OB_INVALID_DATA;
        LOG_WARN("invalid data", KR(ret), K(idx), K(ref_idx), K(row));
      } else if (row.storage_datums_[ref_idx].is_nop()) {
        complete = false;
      }
    }
  }
  return ret;
}

int ObTruncatePartitionFilter::combine_to_filter_tree(sql::ObPushdownFilterExecutor *&root_filter)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret));
  } else if (OB_ISNULL(pushdown_runtime_) || OB_ISNULL(evaluator_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("truncate pushdown runtime is not initialized", K(ret), KPC(this));
  } else if (OB_FAIL(sql::attach_external_pushdown_filter(*pushdown_runtime_, root_filter))) {
  }
  if (OB_SUCC(ret)) {
    has_combined_to_pd_filter_ = true;
  }
  LOG_INFO("[TRUNCATE INFO]", K(ret), KP(root_filter), KP_(evaluator), KPC(this));
  return ret;
}

void ObTruncatePartitionFilter::uncombined_from_pd_filter()
{
  if (nullptr != pushdown_runtime_) {
    sql::detach_external_pushdown_filter(*pushdown_runtime_);
  }
  has_combined_to_pd_filter_ = false;
}

int ObTruncatePartitionFilter::init_truncate_filter(
    const int64_t schema_rowkey_cnt,
    const ObIArray<ObColDesc> &cols_desc,
    const ObIArray<ObColumnParam *> *cols_param,
    const ObTruncateInfoArray &array)
{
  int ret = OB_SUCCESS;
  UNUSED(cols_param);
  if (nullptr == evaluator_) {
    void *buf = filter_allocator_.alloc(sizeof(ObTruncateFilterEvaluator));
    if (OB_ISNULL(buf)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to allocate truncate evaluator", K(ret));
    } else {
      evaluator_ = new (buf) ObTruncateFilterEvaluator();
    }
  }
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(evaluator_->init(schema_rowkey_cnt, cols_desc, array))) {
  } else if (nullptr == pushdown_runtime_ &&
             OB_FAIL(sql::create_external_pushdown_filter_runtime(
                 filter_allocator_, *evaluator_, pushdown_runtime_))) {
    LOG_WARN("failed to create truncate pushdown runtime", K(ret));
  } else if (OB_FAIL(init_column_idxs(array))) {
  } else {
    is_inited_ = true;
  }
  return ret;
}

int ObTruncatePartitionFilter::init_column_idxs(const ObTruncateInfoArray &array)
{
  int ret = OB_SUCCESS;
  for (int64_t idx = 0; OB_SUCC(ret) && idx < array.count(); ++idx) {
    if (OB_ISNULL(array.at(idx))) {
      ret = OB_INVALID_DATA;
      LOG_WARN("invalid data", KR(ret), K(idx), KPC(array.at(idx)));
    } else if (0 == idx && OB_FAIL(init_column_idxs(array.at(idx)->truncate_part_.part_key_idxs_))) {
      LOG_WARN("failed to init column idx", KR(ret), K(idx), KPC(array.at(idx)));
    } else if (array.at(idx)->is_sub_part_) {
      if (OB_FAIL(init_column_idxs(array.at(idx)->truncate_subpart_.part_key_idxs_))) {
      } else {
        break;
      }
    }
  }
  return ret;
}

int ObTruncatePartitionFilter::init_column_idxs(const ObPartKeyIdxArray &key_idx_array)
{
  int ret = OB_SUCCESS;
  for (int64_t idx = 0; OB_SUCC(ret) && idx < key_idx_array.count(); ++idx) {
    if (OB_FAIL(ref_column_idxs_.push_back(key_idx_array.at(idx)))) {
    }
  }
  return ret;
}

int ObTruncatePartitionFilterFactory::build_truncate_partition_filter(
    ObTablet &tablet,
    const ObIArray<ObColDesc> &cols_desc,
    const ObIArray<ObColumnParam *> *cols_param,
    const ObVersionRange &read_version_range,
    ObIAllocator *outer_allocator,
    ObTruncatePartitionFilter *&truncate_part_filter)
{
  int ret = OB_SUCCESS;
  bool has_truncate_flag = false;
  bool has_truncate_info = false;
  if (OB_UNLIKELY(nullptr == outer_allocator || !read_version_range.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Invalid argument", K(ret), K(tablet), KP(outer_allocator), K(read_version_range));
  } else if (FALSE_IT(tablet.check_truncate_info_state(read_version_range, has_truncate_flag, has_truncate_info))) {
  } else if (!has_truncate_flag && !has_truncate_info) {
    if (OB_UNLIKELY(nullptr != truncate_part_filter)) {
      truncate_part_filter->reuse();
      truncate_part_filter->set_empty();
    }
  } else if (nullptr == truncate_part_filter) {
    if (OB_ISNULL(truncate_part_filter = OB_NEWx(ObTruncatePartitionFilter, outer_allocator))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      STORAGE_LOG(WARN, "failed to alloc memory", K(ret));
    } else if (OB_FAIL(truncate_part_filter->init(tablet, cols_desc, cols_param, read_version_range,
                                                  has_truncate_flag, has_truncate_info, *outer_allocator))) {
    }
    if (OB_FAIL(ret) && nullptr != truncate_part_filter) {
      truncate_part_filter->~ObTruncatePartitionFilter();
      outer_allocator->free(truncate_part_filter);
      truncate_part_filter = nullptr;
    }
  } else if (FALSE_IT(truncate_part_filter->reuse())) {
  } else if (OB_FAIL(truncate_part_filter->switch_info(tablet, cols_desc, cols_param, read_version_range,
                                                       has_truncate_flag, has_truncate_info))) {
  }
  return ret;
}

void ObTruncatePartitionFilterFactory::destroy_truncate_partition_filter(ObTruncatePartitionFilter *&truncate_part_filter)
{
  if (nullptr != truncate_part_filter) {
    ObIAllocator *outer_allocator = truncate_part_filter->get_outer_allocator();
    truncate_part_filter->~ObTruncatePartitionFilter();
    if (nullptr != outer_allocator) {
      outer_allocator->free(truncate_part_filter);
    }
    truncate_part_filter = nullptr;
  }  
}

}
}
