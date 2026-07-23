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

#include "ob_empty_read_bucket.h"

namespace oceanbase
{
namespace storage
{
ObEmptyReadBucket::ObEmptyReadBucket()
  : allocator_(ObModIds::OB_BLOOM_FILTER, OB_MALLOC_NORMAL_BLOCK_SIZE),
    buckets_(NULL),
    bucket_size_(0)
{
}

ObEmptyReadBucket::~ObEmptyReadBucket()
{
}

int ObEmptyReadBucket::init()
{
  int ret = OB_SUCCESS;
  char *buf = NULL;
  STORAGE_LOG(DEBUG, "bucket number", K(BUCKET_SIZE));
  if (OB_ISNULL(buf = static_cast<char*>(allocator_.alloc(sizeof(ObEmptyReadCell) * BUCKET_SIZE)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    STORAGE_LOG(WARN, "Fail to allocate memory, ", K(ret));
  } else {
    buckets_ = new (buf) ObEmptyReadCell[BUCKET_SIZE];
    bucket_size_ = BUCKET_SIZE;
  }
  return ret;
}

int ObEmptyReadBucket::mtl_init(ObEmptyReadBucket *&bucket)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(bucket->init())) {
    STORAGE_LOG(WARN, "failed to init EmptyReadBucket, ", K(ret));
  }
  return ret;
}

void ObEmptyReadBucket::destroy()
{
  if (NULL != buckets_) {
    for (int64_t i = 0; i < bucket_size_; ++i) {
      buckets_[i].~ObEmptyReadCell();
    }
    allocator_.free(buckets_);
    allocator_.reset();
    buckets_ = NULL;
    bucket_size_ = 0;
  }
}

void ObEmptyReadBucket::mtl_destroy(ObEmptyReadBucket *&bucket)
{
  if (OB_NOT_NULL(bucket)) {
    bucket->destroy();
    common::ob_delete(bucket);
  }
}

int ObEmptyReadBucket::get_cell(const uint64_t hashcode, ObEmptyReadCell *&cell)
{
  int ret = OB_SUCCESS;
  uint64_t idx = hashcode & (bucket_size_ - 1);
  cell = NULL;
  if (OB_UNLIKELY(!is_valid())) {
    ret = OB_NOT_INIT;
    STORAGE_LOG(WARN, "ObBloomFilterCache bucket not init ", K(ret));
  } else {
    cell = &buckets_[idx];
  }
  return ret;
}

void ObEmptyReadBucket::reset()
{
  for (int64_t i = 0; i < bucket_size_; ++i) {
    buckets_[i].reset();
  }
}

} // namespace storage
} // namespace oceanbase
