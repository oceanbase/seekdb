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

#define USING_LOG_PREFIX SERVER

#include "observer/table_load/ob_table_load_assigned_memory_manager.h"
#include "storage/direct_load/ob_direct_load_mem_define.h"
#include "src/observer/table_load/ob_table_load_assigned_memory_manager.h"

namespace oceanbase
{
namespace observer
{
using namespace common;
using namespace common::hash;
using namespace share;
using namespace share::schema;
using namespace lib;
using namespace table;
using namespace omt;

/**
 * ObTableLoadAssignedMemoryManager
 */

ObTableLoadAssignedMemoryManager::ObTableLoadAssignedMemoryManager() 
  : avail_sort_memory_(0),
    avail_memory_(0),
    chunk_count_(0),
    is_inited_(false)
{
}

ObTableLoadAssignedMemoryManager::~ObTableLoadAssignedMemoryManager() 
{
}

int ObTableLoadAssignedMemoryManager::init() 
{
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ObTableLoadAssignedMemoryManager init twice", KR(ret), KP(this));
  } else {
    is_inited_ = true;
  }

  return ret;
}

int ObTableLoadAssignedMemoryManager::assign_memory(bool is_sort, int64_t assign_memory)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTableLoadAssignedMemoryManager not init", KR(ret), KP(this));
  } else {
    ObMutexGuard guard(mutex_);
    chunk_count_ += (is_sort ? assign_memory / ObDirectLoadExternalMultiPartitionRowChunk::MIN_MEMORY_LIMIT : 0);
    avail_sort_memory_ -= (is_sort ? 0 : assign_memory);
    LOG_INFO("ObTableLoadAssignedMemoryManager::assign_memory", 
        K(MTL_ID()), K(is_sort), K(chunk_count_), K(assign_memory), K(avail_sort_memory_), K(avail_memory_));
  }
  
  return ret;
}

int ObTableLoadAssignedMemoryManager::recycle_memory(bool is_sort, int64_t assign_memory)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTableLoadAssignedMemoryManager not init", KR(ret), KP(this));
  } else {
    ObMutexGuard guard(mutex_);
    chunk_count_ -= (is_sort ? assign_memory / ObDirectLoadExternalMultiPartitionRowChunk::MIN_MEMORY_LIMIT : 0);
    avail_sort_memory_ += (is_sort ? 0 : assign_memory);
    LOG_INFO("ObTableLoadAssignedMemoryManager::recycle_memory", 
        K(MTL_ID()), K(is_sort), K(chunk_count_), K(assign_memory), K(avail_sort_memory_), K(avail_memory_));
  }

  return ret;
}

int64_t ObTableLoadAssignedMemoryManager::get_avail_memory()
{ 
  int64_t avail_memory;
  {
    ObMutexGuard guard(mutex_);
    avail_memory = avail_memory_;
  }
  return avail_memory;
}

int ObTableLoadAssignedMemoryManager::refresh_avail_memory(int64_t avail_memory) 
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTableLoadAssignedMemoryManager not init", KR(ret), KP(this));
  } else {
    ObMutexGuard guard(mutex_);
    avail_sort_memory_ += avail_memory - avail_memory_;
    avail_memory_ = avail_memory;
  }

  return ret;
}

int ObTableLoadAssignedMemoryManager::get_sort_memory(int64_t &sort_memory) 
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTableLoadAssignedMemoryManager not init", KR(ret), KP(this));
  } else {
    ObMutexGuard guard(mutex_);
    if (OB_UNLIKELY(chunk_count_ == 0)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected chunk_count_ equal to zero", KR(ret));
    } else if (OB_UNLIKELY(avail_memory_ == 0)) {
      ret = OB_EAGAIN;
      LOG_WARN("avail_memory_ equal to zero, resource has been migrated", KR(ret));
    } else {
      sort_memory = avail_sort_memory_ / chunk_count_;
    }
  }
  
  return ret;
}

} // namespace observer
} // namespace oceanbase
