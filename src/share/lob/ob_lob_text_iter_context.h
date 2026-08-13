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

#ifndef OCEANBASE_SHARE_LOB_OB_LOB_TEXT_ITER_CONTEXT_H_
#define OCEANBASE_SHARE_LOB_OB_LOB_TEXT_ITER_CONTEXT_H_

#include "common/object/ob_object.h"

namespace oceanbase
{
namespace common
{
class ObILobAccessContext;
class ObILobReadCursor;
class ObILobReadService;

// Internal protocol state shared only by the Share runtime and the Storage
// Adapter. It deliberately contains no SQL or Storage vocabulary.
struct ObLobTextIterCtx
{
  static const uint32_t OB_LOB_ITER_DEFAULT_BUFFER_LEN = 2 * 1024 * 1024;

  ObLobTextIterCtx(ObLobLocatorV2 &locator,
                   ObILobReadService &read_service,
                   int64_t timeout_ts,
                   ObIAllocator *allocator = nullptr,
                   uint32_t buffer_len = OB_LOB_ITER_DEFAULT_BUFFER_LEN)
    : read_service_(read_service), alloc_(allocator), timeout_ts_(timeout_ts),
      buff_(nullptr), buff_byte_len_(buffer_len),
      start_offset_(0), total_access_len_(0), total_byte_len_(0), content_byte_len_(0),
      content_len_(0), reserved_byte_len_(0), reserved_len_(0), accessed_byte_len_(0),
      accessed_len_(0), last_accessed_byte_len_(0), last_accessed_len_(0), iter_count_(0),
      is_cloned_temporary_(false), is_backward_(false), locator_(locator), read_cursor_(nullptr),
      access_context_(nullptr)
  {}

  TO_STRING_KV(KP_(alloc), K_(timeout_ts), KP_(buff), K_(buff_byte_len), K_(start_offset),
               K_(total_access_len), K_(content_byte_len), K_(content_len), K_(reserved_byte_len),
               K_(reserved_len), K_(accessed_byte_len), K_(accessed_len),
               K_(last_accessed_byte_len), K_(last_accessed_len), K_(iter_count),
               K_(is_cloned_temporary), K_(is_backward), K_(locator), KP_(read_cursor));

  void init(bool is_clone = false);
  void reuse();
  OB_INLINE void unset_clone() { is_cloned_temporary_ = false; }

  ObILobReadService &read_service_;
  ObIAllocator *alloc_;
  int64_t timeout_ts_;
  char *buff_;
  uint32_t buff_byte_len_;
  uint64_t start_offset_;
  int64_t total_access_len_;
  int64_t total_byte_len_;
  uint32_t content_byte_len_;
  uint32_t content_len_;
  uint32_t reserved_byte_len_;
  uint32_t reserved_len_;
  uint32_t accessed_byte_len_;
  uint32_t accessed_len_;
  uint32_t last_accessed_byte_len_;
  uint32_t last_accessed_len_;
  uint32_t iter_count_;
  bool is_cloned_temporary_;
  bool is_backward_;
  ObLobLocatorV2 locator_;
  ObILobReadCursor *read_cursor_;
  ObILobAccessContext *access_context_;
};

} // namespace common
} // namespace oceanbase

#endif // OCEANBASE_SHARE_LOB_OB_LOB_TEXT_ITER_CONTEXT_H_
