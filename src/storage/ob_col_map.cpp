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

#include "storage/ob_col_map.h"

namespace oceanbase
{
using namespace common;
namespace storage
{
void ObColMap::destroy()
{
  reset();
}

int ObColMap::init(const int64_t col_count)
{
  int ret = OB_SUCCESS;
  void *ptr = NULL;
  const uint64_t count = static_cast<uint64_t>(col_count);
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    STORAGE_LOG(WARN, "ObColMap init twice", K(ret));
  } else if (count <= FINAL_LEVEL_MAX_COL_NUM) {
    if (OB_UNLIKELY(count > FIRST_LEVEL_MAX_COL_NUM)) {
      if (NULL == (ptr = ob_malloc(sizeof(ColMapFinal), ObModIds::OB_COL_MAP))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        STORAGE_LOG(ERROR, "fail to allocate memory for column_ids map", K(ret));
      } else {
        col_map_final_ = new (ptr) ColMapFinal();
      }
    }
    if (OB_SUCC(ret)) {
      is_inited_ = true;
    }
  } else {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "invalid column count, not support", K(ret), K(col_count));
  }
  if (OB_SUCCESS != ret && !is_inited_) {
    destroy();
  }
  return ret;
}

int ObColMap::set_refactored(const uint64_t col_id, const int64_t col_idx)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    STORAGE_LOG(WARN, "ColMap is not initialized", K(ret));
  } else if (OB_UNLIKELY(OB_INVALID_ID == col_id) || OB_UNLIKELY(col_idx < 0)) {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "invalid argument", K(ret), K(col_id), K(col_idx));
  } else if (OB_UNLIKELY(NULL != col_map_final_)) {
    ret = col_map_final_->set_refactored(col_id, col_idx);
  } else {
    ret = col_map_first_.set_refactored(col_id, col_idx);
  }
  return ret;
}

int ObColMap::get_refactored(const uint64_t col_id, int64_t &col_idx) const
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    STORAGE_LOG(WARN, "ColMap is not initialized", K(ret));
  } else if (OB_UNLIKELY(OB_INVALID_ID == col_id)) {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "invalid argument", K(ret), K(col_id));
  } else if (OB_UNLIKELY(NULL != col_map_final_)) {
    ret = col_map_final_->get_refactored(col_id, col_idx);
  } else {
    ret = col_map_first_.get_refactored(col_id, col_idx);
  }
  return ret;
}


int64_t *ObColMap::get(const uint64_t col_id)
{
  int ret = OB_SUCCESS;
  int64_t *col_idx = NULL;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    STORAGE_LOG(WARN, "ColMap is not initialized", K(ret));
  } else if (OB_UNLIKELY(OB_INVALID_ID == col_id)) {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "invalid argument", K(ret), K(col_id));
  } else if (OB_UNLIKELY(NULL != col_map_final_)) {
    col_idx = col_map_final_->get(col_id);
  } else {
    col_idx = col_map_first_.get(col_id);
  }
  return col_idx;
}

void ObColMap::reset()
{
  col_map_first_.reset();
  if (OB_UNLIKELY(NULL != col_map_final_)) {
    col_map_final_->~ColMapFinal();
    ob_free(col_map_final_);
    col_map_final_ = NULL;
  }
  is_inited_ = false;
}


} // namespace storage
} // namespace oceanbase
