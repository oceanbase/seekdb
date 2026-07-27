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

#ifndef SRC_STORAGE_BLOCKSSTABLE_TABLE_FLAG_H_
#define SRC_STORAGE_BLOCKSSTABLE_TABLE_FLAG_H_

#include "lib/utility/ob_macro_utils.h"
#include "lib/utility/ob_print_utils.h"
#include "lib/oblog/ob_log_module.h"

namespace oceanbase
{
namespace storage
{

class ObTableHasBackupFlag final
{
public:
  enum FLAG
  {
    NO_BACKUP = 0,
    HAS_BACKUP = 1,
    MAX,
  };
};

class ObTableHasLocalFlag final
{
public:
  enum FLAG
  {
    HAS_LOCAL = 0,
    NO_LOCAL = 1,
    MAX
  };
};

struct ObTableBackupFlag final
{
  OB_UNIS_VERSION(1);
public:
  ObTableBackupFlag();
  // TODO: yangyi.yyy to Refactor
  ObTableBackupFlag(int64_t flag);
  ~ObTableBackupFlag();
  void reset();
  bool is_valid() const;
  OB_INLINE bool operator==(const ObTableBackupFlag &other) const;
  OB_INLINE bool operator!=(const ObTableBackupFlag &other) const;
  void clear();
  TO_STRING_KV(K_(has_backup_flag), K_(has_local_flag));

public:
  bool has_backup() const { return ObTableHasBackupFlag::HAS_BACKUP == has_backup_flag_; }
  bool has_no_backup() const { return ObTableHasBackupFlag::NO_BACKUP == has_backup_flag_; }
  void set_has_backup() { has_backup_flag_ = ObTableHasBackupFlag::HAS_BACKUP; }
  void set_no_backup() { has_backup_flag_ = ObTableHasBackupFlag::NO_BACKUP; }
  bool has_local() const { return ObTableHasLocalFlag::HAS_LOCAL == has_local_flag_; }
  void set_has_local() { has_local_flag_ = ObTableHasLocalFlag::HAS_LOCAL; }
  void set_no_local() { has_local_flag_ = ObTableHasLocalFlag::NO_LOCAL; }
  bool is_backup_only() const { return has_backup() && !has_local(); }
  int32_t get_flag() const { return flag_; }

private:
  static const uint64_t SF_BIT_HAS_BACKUP = 1;
  static const uint64_t SF_BIT_HAS_LOCAL = 1;
  static const uint64_t SF_BIT_RESERVED = 30;

public:
  union {
    int32_t flag_;
    struct {
      // NOTE: use unsigned bit-fields here. MSVC treats enum / int bit-fields as
      // signed by default, so a 1-bit signed field can only hold 0 and -1, which
      // makes writing values like ObTableHasLocalFlag::NO_LOCAL (1) read back as
      // -1 and break is_valid() on Windows. Using uint32_t guarantees portable
      // unsigned semantics across GCC/MSVC.
      uint32_t has_backup_flag_ : SF_BIT_HAS_BACKUP;
      uint32_t has_local_flag_  : SF_BIT_HAS_LOCAL;
      uint32_t reserved_        : SF_BIT_RESERVED;
    };
  };
};

bool ObTableBackupFlag::operator==(const ObTableBackupFlag &other) const
{
  return flag_ == other.flag_;
}

bool ObTableBackupFlag::operator!=(const ObTableBackupFlag &other) const
{
  return !(this->operator==(other));
}

}
}

#endif
