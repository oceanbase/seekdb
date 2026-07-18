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
#pragma once

#include "storage/blocksstable/ob_storage_datum.h"

namespace oceanbase
{
namespace storage
{
class ObDirectLoadDatumSerialization
{
public:
  static int serialize(char *buf, const int64_t buf_len, int64_t &pos,
                       const blocksstable::ObStorageDatum &datum);
  static int deserialize(const char *buf, const int64_t data_len, int64_t &pos,
                         blocksstable::ObStorageDatum &datum);
  static int64_t get_serialize_size(const blocksstable::ObStorageDatum &datum);
};

struct ObDirectLoadDatumArray
{
  OB_UNIS_VERSION(1);
public:
  ObDirectLoadDatumArray();
  ObDirectLoadDatumArray(const ObDirectLoadDatumArray &other) = delete;
  ~ObDirectLoadDatumArray();
  void reset();
  void reuse();
  int assign(blocksstable::ObStorageDatum *datums, int32_t count);
  ObDirectLoadDatumArray &operator=(const ObDirectLoadDatumArray &other);
  int64_t get_deep_copy_size() const;
  int deep_copy(const ObDirectLoadDatumArray &src, char *buf, const int64_t len, int64_t &pos);
  bool is_valid() const { return 0 == count_ || nullptr != datums_; }
  DECLARE_TO_STRING;
public:
  common::ObArenaAllocator allocator_;
  int64_t capacity_;
  int64_t count_;
  blocksstable::ObStorageDatum *datums_;
};

struct ObDirectLoadConstDatumArray
{
public:
  ObDirectLoadConstDatumArray();
  ObDirectLoadConstDatumArray(const ObDirectLoadConstDatumArray &other) = delete;
  ~ObDirectLoadConstDatumArray();
  void reset();
  ObDirectLoadConstDatumArray &operator=(const ObDirectLoadConstDatumArray &other);
  ObDirectLoadConstDatumArray &operator=(const ObDirectLoadDatumArray &other);
  int64_t get_deep_copy_size() const;
  int deep_copy(const ObDirectLoadConstDatumArray &src, char *buf, const int64_t len, int64_t &pos);
  OB_INLINE bool is_valid() const { return 0 == count_ || nullptr != datums_; }
  DECLARE_TO_STRING;
public:
  int64_t count_;
  blocksstable::ObStorageDatum *datums_;
};

} // namespace storage
} // namespace oceanbase
