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
#include "ob_sstable_merge_info_mgr.h"
#include "lib/alloc/alloc_func.h"

namespace oceanbase
{
using namespace common;
using namespace compaction;
namespace storage
{
/**
 * ------------------------------------------------------------------ObSSTableMergeInfoMgr---------------------------------------------------------------
 */
ObSSTableMergeInfoMgr::ObSSTableMergeInfoMgr()
  : is_inited_(false),
    major_info_pool_(),
    minor_info_pool_()
{
}


ObSSTableMergeInfoMgr::~ObSSTableMergeInfoMgr()
{
  destroy();
}

int ObSSTableMergeInfoMgr::server_module_init(ObSSTableMergeInfoMgr *&sstable_merge_info)
{
  return sstable_merge_info->init();
}

int64_t ObSSTableMergeInfoMgr::cal_max()
{
  int64_t max_size = std::min(lib::get_memory_budget() / 100 * MEMORY_PERCENTAGE,
                           static_cast<int64_t>(POOL_MAX_SIZE));
  return max_size;
}

int ObSSTableMergeInfoMgr::get_next_info(compaction::ObIDiagnoseInfoMgr::Iterator &major_iter,
      compaction::ObIDiagnoseInfoMgr::Iterator &minor_iter,
      ObSSTableMergeHistory &merge_history, char *buf, const int64_t buf_len)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(major_iter.get_next(&merge_history, buf, buf_len))) {
    if (OB_ITER_END == ret) {
      if (OB_FAIL(minor_iter.get_next(&merge_history, buf, buf_len))) {
        if (OB_ITER_END != ret) {
          STORAGE_LOG(WARN, "failed to get next minor sstable merge info", K(ret));
        }
      }
    } else {
      STORAGE_LOG(WARN, "failed to get next major sstable merge info", K(ret));
    }
  }
  return ret;
}

int ObSSTableMergeInfoMgr::init(const int64_t page_size)
{
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    STORAGE_LOG(WARN, "ObSSTableMergeInfoMgr has already been initiated", K(ret));
  } else {
    int64_t max_size = cal_max();
    if (OB_FAIL(major_info_pool_.init(false,
                                      "MajorMerge",
                                      page_size,
                                      max_size * (100 - MINOR_MEMORY_PERCENTAGE) / 100))) {
    } else if (OB_FAIL(minor_info_pool_.init(false,
                                      "MinorMerge",
                                      page_size,
                                      max_size * MINOR_MEMORY_PERCENTAGE / 100))) {
    } else {
      is_inited_ = true;
    }
  }

  if (!is_inited_) {
    reset();
  }
  return ret;
}

void ObSSTableMergeInfoMgr::destroy()
{
  if (IS_INIT) {
    reset();
  }
}

void ObSSTableMergeInfoMgr::reset()
{
  major_info_pool_.destroy();
  minor_info_pool_.destroy();
  is_inited_ = false;
  STORAGE_LOG(INFO, "ObSSTableMergeInfoMgr destroy finish");
}

int ObSSTableMergeInfoMgr::open_iter(compaction::ObIDiagnoseInfoMgr::Iterator &major_iter,
      compaction::ObIDiagnoseInfoMgr::Iterator &minor_iter)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    STORAGE_LOG(WARN, "ObSSTableMergeInfoMgr is not initialized", K(ret));
  } else if (OB_FAIL(major_info_pool_.open_iter(major_iter))) {
  } else if (OB_FAIL(minor_info_pool_.open_iter(minor_iter))) {
  }
  return ret;
}

int ObSSTableMergeInfoMgr::set_max(int64_t max_size)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    STORAGE_LOG(WARN, "ObSSTableMergeInfoMgr is not init", K(ret));
  } else if (OB_FAIL(major_info_pool_.set_max(max_size * (100 - MINOR_MEMORY_PERCENTAGE) / 100))) {
  } else if (OB_FAIL(minor_info_pool_.set_max(max_size * MINOR_MEMORY_PERCENTAGE / 100))) {
  }
  return ret;
}

int ObSSTableMergeInfoMgr::gc_info()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    STORAGE_LOG(WARN, "ObSSTableMergeInfoMgr is not init", K(ret));
  } else if (OB_FAIL(major_info_pool_.gc_info())) {
  } else if (OB_FAIL(minor_info_pool_.gc_info())) {
  }
  return ret;
}

int ObSSTableMergeInfoMgr::size()
{
  int size = 0;
  if (IS_INIT) {
    size = minor_info_pool_.size() + major_info_pool_.size();
  }
  return size;
}

int ObSSTableMergeInfoMgr::add_sstable_merge_info(ObSSTableMergeHistory &merge_history)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    STORAGE_LOG(WARN, "ObSSTableMergeInfoMgr is not initialized", K(ret));
  } else if (OB_UNLIKELY(!merge_history.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "invalid argument", K(ret), K(merge_history));
  } else {
    compaction::ObIDiagnoseInfoMgr *info_pool = &minor_info_pool_;
    if (merge_history.is_major_merge_type()) {
      info_pool = &major_info_pool_;
    }
    if (OB_FAIL(info_pool->alloc_and_add(0, &merge_history))) {
    }
  }
  return ret;
}
}//storage
}//oceanbase
