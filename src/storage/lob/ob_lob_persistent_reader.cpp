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

#include "ob_lob_persistent_reader.h"
#include "storage/lob/ob_lob_persistent_iterator.h"

namespace oceanbase
{
namespace storage
{

ObPersistLobReaderCache::~ObPersistLobReaderCache()
{
  int ret = OB_SUCCESS;
  DLIST_FOREACH(curr, list_) {
    curr->reader_->reset();
    curr->reader_->~ObLobMetaIterator();
    curr->reader_ = nullptr;
  }
  list_.clear();
}

int ObPersistLobReaderCache::get(ObPersistLobReaderCacheKey key, ObLobMetaIterator *&reader)
{
  int ret = OB_SUCCESS;
  DLIST_FOREACH_X(curr, list_, OB_SUCC(ret) && nullptr == reader) {
    if (OB_ISNULL(curr)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("curr is null", K(ret));
    } else if (! (curr->key_ == key)) { // next
    } else if (false  == list_.move_to_last(curr)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("move_to_last fail", K(ret), K(key));
    } else if (OB_ISNULL(reader = curr->reader_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("reader is null", K(ret), K(key));
    }
  }
  return ret;
}

int ObPersistLobReaderCache::put(ObPersistLobReaderCacheKey key, ObLobMetaIterator *reader)
{
  int ret = OB_SUCCESS;
  ObPersistLobReaderCacheNode *node = nullptr;
  if (OB_ISNULL(reader)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("reader is null", K(ret), K(key));
  } else if (OB_ISNULL(node = OB_NEWx(ObPersistLobReaderCacheNode, &allocator_))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("alloc fail", K(ret));
  } else {
    node->key_ = key;
    node->reader_ = reader;
    if (list_.get_size() >= cap_ && OB_FAIL(remove_first())) {
      LOG_WARN("remove_first fail", K(ret), K(list_), K(cap_));
    } else if (false == list_.add_last(node)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("add_last fail", K(ret), K(key));
    }
  }

  if (OB_FAIL(ret)) {
    if (OB_NOT_NULL(node)) {
      allocator_.free(node);
    }
  }
  return ret;
}

int ObPersistLobReaderCache::remove_first()
{
  int ret = OB_SUCCESS;
  ObPersistLobReaderCacheNode *node = list_.remove_first();
  if (OB_ISNULL(node)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("node is null", K(ret), K(list_));
  } else {
    node->reader_->~ObLobMetaIterator();
    allocator_.free(node->reader_);
    node->reader_ = nullptr;
    allocator_.free(node);
  }
  return ret;
}

ObLobMetaIterator* ObPersistLobReaderCache::alloc_reader(const ObLobAccessCtx *access_ctx)
{
  return OB_NEWx(ObLobMetaIterator, &allocator_, access_ctx);
}

} // storage
} // oceanbase
