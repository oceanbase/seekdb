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

#ifndef OCEANBASE_STORAGE_TABLET_TABLET_TABLE_STORE_FLAG_H_
#define OCEANBASE_STORAGE_TABLET_TABLET_TABLE_STORE_FLAG_H_

#include <stdint.h>
#include "lib/utility/ob_print_utils.h"
#include "share/ob_force_print_log.h"

namespace oceanbase
{
namespace storage
{

class ObTabletTableStoreWithMajorFlag final
{
public:
  enum FLAG 
  {
    WITHOUT_MAJOR_SSTABLE = 0,
    WITH_MAJOR_SSTABLE = 1,
  };
};

class ObTabletTableStoreFlag final
{
public:
  ObTabletTableStoreFlag();
  ~ObTabletTableStoreFlag();
  void reset();
  int serialize(char *buf, const int64_t len, int64_t &pos) const;
  int deserialize(const char *buf, const int64_t len, int64_t &pos);
  int64_t get_serialize_size() const;
  bool with_major_sstable() const { return ObTabletTableStoreWithMajorFlag::WITH_MAJOR_SSTABLE == with_major_sstable_; }
  void set_with_major_sstable() { with_major_sstable_ = ObTabletTableStoreWithMajorFlag::WITH_MAJOR_SSTABLE; }
  void set_without_major_sstable() { with_major_sstable_ = ObTabletTableStoreWithMajorFlag::WITHOUT_MAJOR_SSTABLE; }
  void set_is_user_data_table(const bool is_user_data_table) { is_user_data_table_ = is_user_data_table; }
  bool is_user_data_table() const { return is_user_data_table_; }
  TO_STRING_KV(K_(with_major_sstable), K_(is_user_data_table));

public:
  static const uint64_t SF_ONE_BIT = 1;
  static const uint64_t SF_BIT_RESERVED = 62;

private:
  union {
    int64_t status_;
    struct {
      ObTabletTableStoreWithMajorFlag::FLAG with_major_sstable_ : SF_ONE_BIT;
      int64_t is_user_data_table_ : SF_ONE_BIT;
      int64_t reserved_: SF_BIT_RESERVED;
    };
  };
};

} // end namespace storage
} // end namespace oceanbase

#endif
