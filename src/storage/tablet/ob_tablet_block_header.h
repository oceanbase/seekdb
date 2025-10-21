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

#ifndef OCEANBASE_STORAGE_OB_TABLET_BLOCK_HEADER_H
#define OCEANBASE_STORAGE_OB_TABLET_BLOCK_HEADER_H

#include <stdint.h>
#include "lib/utility/ob_print_utils.h"
#include "lib/allocator/page_arena.h"
#include "lib/container/ob_array_wrap.h"
#include "lib/container/ob_array_serialization.h"

namespace oceanbase
{
namespace storage
{
enum class ObSecondaryMetaType : uint8_t
{
  TABLET_MACRO_INFO = 0,
  TABLE_STORE = 1,
  STORAGE_SCHEMA = 2,
  MDS_DATA = 3,
  MAX = 4
};

struct ObInlineSecondaryMetaDesc final
{
public:
  ObInlineSecondaryMetaDesc()
    : type_(ObSecondaryMetaType::MAX), length_(0)
  {
  }
  ObInlineSecondaryMetaDesc(const ObSecondaryMetaType type, const int32_t length)
    : type_(type), length_(length)
  {
  }

  ObSecondaryMetaType type_;
  int32_t length_;

  TO_STRING_KV(K_(type), K_(length));

} __attribute__((packed));

struct ObTabletBlockHeader final
{
public:
  static const int32_t TABLET_VERSION_V1 = 1;
  static const int32_t TABLET_VERSION_V2 = 2;
  static const int32_t TABLET_VERSION_V3 = 3;

  ObTabletBlockHeader()
    : is_inited_(false), pushed_inline_meta_cnt_(0),
      version_(TABLET_VERSION_V3), length_(0),
      checksum_(0), inline_meta_count_(0)
  {
  }

  int init(const int32_t secondary_meta_count);
  bool is_valid() const
  {
    return is_inited_ && version_ == TABLET_VERSION_V3  && length_ > 0 && checksum_ > 0 && inline_meta_count_ >= 0;
  }

  NEED_SERIALIZE_AND_DESERIALIZE;
  int push_inline_meta(const ObInlineSecondaryMetaDesc &desc);

  TO_STRING_KV(K_(version), K_(length), K_(checksum), "desc_array",
      common::ObArrayWrap<ObInlineSecondaryMetaDesc>(desc_array_, inline_meta_count_));

  int32_t get_version() const { return version_; }
  int32_t get_length() const { return length_; }
public:
  static const int32_t MAX_INLINE_META_COUNT = 8;
  bool is_inited_;
  int32_t pushed_inline_meta_cnt_;

  // below need serialize
  int32_t version_;
  int32_t length_; // tablet first-level meta size
  int32_t checksum_; // checksum for tablet first-level meta
  int32_t inline_meta_count_; // inline meta refers the secondary meta which is stored consecutively with tablet first-level meta
  ObInlineSecondaryMetaDesc desc_array_[MAX_INLINE_META_COUNT];
};

struct ObSecondaryMetaHeader final
{
public:
  static const int32_t SECONDARY_META_HEADER_VERSION = 1;
public:
  ObSecondaryMetaHeader()
    : version_(SECONDARY_META_HEADER_VERSION),
      size_(sizeof(ObSecondaryMetaHeader)), checksum_(0), payload_size_(0)
  {
  }
  ~ObSecondaryMetaHeader() { destroy(); }
  void destroy();
  TO_STRING_KV(K_(version), K_(checksum), K_(size), K_(payload_size));
  NEED_SERIALIZE_AND_DESERIALIZE;
public:
  int32_t version_;
  int32_t size_;
  int32_t checksum_;
  int32_t payload_size_;
};

struct ObInlineSecondaryMeta final
{
public:
  ObInlineSecondaryMeta()
  {
  }
  ObInlineSecondaryMeta(const void *obj, const ObSecondaryMetaType meta_type)
    : obj_(obj), meta_type_(meta_type)
  {
  }
  TO_STRING_KV(KP_(obj), K_(meta_type));
  const void *obj_;
  ObSecondaryMetaType meta_type_;
};

} // namespace storage
} // namespace oceanbase

#endif // OCEANBASE_STORAGE_OB_TABLET_BLOCK_HEADER_H
