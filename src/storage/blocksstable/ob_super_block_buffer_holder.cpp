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
#include "ob_super_block_buffer_holder.h"

namespace oceanbase
{
using namespace common;
namespace blocksstable
{
ObSuperBlockBufferHolder::ObSuperBlockBufferHolder()
  : is_inited_(false),
    buf_(NULL),
    len_(0),
    allocator_(ObModIds::OB_SUPER_BLOCK_BUFFER)
{
}

ObSuperBlockBufferHolder::~ObSuperBlockBufferHolder()
{
}

int ObSuperBlockBufferHolder::init(const int64_t buf_size)
{
  int ret = OB_SUCCESS;
  char *tmp_buf = NULL;

  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    STORAGE_LOG(WARN, "inited twice", K(ret));
  } else if (buf_size <= 0) {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "invalid argument", K(ret), K(buf_size));
  } else if (NULL == (tmp_buf = (char*) allocator_.alloc(buf_size + DIO_READ_ALIGN_SIZE))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    STORAGE_LOG(ERROR, "allocate buffer for super block fail", K(ret), K(buf_size), KP(tmp_buf));
  } else {
    // ObArenaAllocator::alloc_aligned has bug, manually align address
    buf_ = (char *) upper_align((int64_t) tmp_buf, DIO_READ_ALIGN_SIZE);
    len_ = buf_size;
    is_inited_ = true;
  }

  if (IS_NOT_INIT) {
    reset();
  }
  return ret;
}

void ObSuperBlockBufferHolder::reset()
{
  buf_ = NULL;
  len_ = 0;
  allocator_.reset();
  is_inited_ = false;
}

int ObSuperBlockBufferHolder::serialize_super_block(const storage::ObServerSuperBlock &super_block)
{
  int ret = OB_SUCCESS;
  int64_t pos = 0;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    STORAGE_LOG(WARN, "not inited", K(ret));
  } else if (OB_FAIL(super_block.serialize(buf_, len_, pos))) {
    STORAGE_LOG(ERROR, "fail to write super block buf", K(ret), KP_(buf), K_(len),
        K(pos), K(super_block));
  }
  return ret;
}



} // namespace blocksstable
} // namespace oceanbase
