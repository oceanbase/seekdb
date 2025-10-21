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

#ifndef OB_SUPER_BLOCK_BUFFER_HOLDER_H_
#define OB_SUPER_BLOCK_BUFFER_HOLDER_H_

#include "storage/ob_super_block_struct.h"

namespace oceanbase
{
namespace blocksstable
{
class ObSuperBlockBufferHolder
{
public:
  ObSuperBlockBufferHolder();
  virtual ~ObSuperBlockBufferHolder();

  int init(const int64_t buf_size);
  void reset();

  int serialize_super_block(const storage::ObServerSuperBlock &super_block);
  template<typename SuperBlockClass>
  int deserialize_super_block(SuperBlockClass &super_block);

  OB_INLINE char *get_buffer() { return buf_; }
  OB_INLINE int64_t get_len() { return len_; }

  TO_STRING_KV(KP_(buf), K_(len));

private:
  bool is_inited_;
  char *buf_;
  int64_t len_;
  common::ObArenaAllocator allocator_;
};

template<typename SuperBlockClass>
int ObSuperBlockBufferHolder::deserialize_super_block(SuperBlockClass &super_block)
{
  int ret = common::OB_SUCCESS;
  int64_t pos = 0;

  if (IS_NOT_INIT) {
    ret = common::OB_NOT_INIT;
    STORAGE_LOG(WARN, "not inited", K(ret));
  } else if (OB_FAIL(super_block.deserialize(buf_, len_, pos))) {
    STORAGE_LOG(WARN, "fail to deserialize super block", K(ret));
  } else {
    STORAGE_LOG(INFO, "load superblock ok.", K(super_block));
  }
  return ret;
}

} // namespace blocksstable
} // namespace oceanbase

#endif /* OB_SUPER_BLOCK_BUFFER_HOLDER_H_ */
