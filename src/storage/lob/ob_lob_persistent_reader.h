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

#ifndef OCEABASE_STORAGE_OB_LOB_PERSISTENT_READER_
#define OCEABASE_STORAGE_OB_LOB_PERSISTENT_READER_

#include "storage/lob/ob_lob_access_param.h"

namespace oceanbase
{
namespace storage
{


class ObLobMetaIterator;

struct ObPersistLobReaderCacheKey
{
  ObPersistLobReaderCacheKey():
    ls_id_(),
    tablet_id_(),
    snapshot_(0),
    is_get_(false)
  {}

  share::ObLSID ls_id_;
  common::ObTabletID tablet_id_;
  int64_t snapshot_;
  bool is_get_;
  bool operator==(const ObPersistLobReaderCacheKey &other) const
  { 
    return snapshot_ == other.snapshot_ && tablet_id_ == other.tablet_id_ && ls_id_ == other.ls_id_ && is_get_ == other.is_get_;
  }

  TO_STRING_KV(K(ls_id_), K(tablet_id_), K(snapshot_));
};

struct ObPersistLobReaderCacheNode : public ObDLinkBase<ObPersistLobReaderCacheNode>
{
  ObPersistLobReaderCacheNode():
    key_(),
    reader_(nullptr)
  {}

  ObPersistLobReaderCacheKey key_;
  ObLobMetaIterator *reader_;
};

class ObPersistLobReaderCache
{
public:
  static const int DEFAULT_CAP = 10;

public:
  ObPersistLobReaderCache(int32_t cap = DEFAULT_CAP):
    allocator_(ObModIds::OB_LOB_READER, OB_MALLOC_NORMAL_BLOCK_SIZE/*8KB*/, MTL_ID()),
    cap_(cap) 
  {}
  ~ObPersistLobReaderCache();

  int get(ObPersistLobReaderCacheKey key, ObLobMetaIterator *&reader);
  int put(ObPersistLobReaderCacheKey key, ObLobMetaIterator *reader);

  ObLobMetaIterator* alloc_reader(const ObLobAccessCtx *access_ctx);
  ObIAllocator& get_allocator() { return allocator_; }

private:
  int remove_first();

private:
  ObArenaAllocator allocator_;
  const int32_t cap_;
  ObDList<ObPersistLobReaderCacheNode> list_;
};


struct ObLobAccessCtx
{
  ObLobAccessCtx():
    reader_cache_()
  {}
  ObPersistLobReaderCache reader_cache_;
};


} // storage
} // oceanbase

#endif
