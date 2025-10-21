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
#include "storage/blocksstable/ob_table_flag.h"

namespace oceanbase
{
namespace storage
{

ObTableBackupFlag::ObTableBackupFlag()
  : has_backup_flag_(ObTableHasBackupFlag::NO_BACKUP),
    has_local_flag_(ObTableHasLocalFlag::HAS_LOCAL),
    reserved_(0)
{
}

ObTableBackupFlag::ObTableBackupFlag(int64_t flag)
  : flag_(flag)
{
}

ObTableBackupFlag::~ObTableBackupFlag()
{
}

void ObTableBackupFlag::reset()
{
  has_backup_flag_ = ObTableHasBackupFlag::NO_BACKUP;
  has_local_flag_ = ObTableHasLocalFlag::HAS_LOCAL;
  reserved_ = 0;
}

bool ObTableBackupFlag::is_valid() const
{
  return has_backup_flag_ >= ObTableHasBackupFlag::NO_BACKUP && has_backup_flag_ < ObTableHasBackupFlag::MAX
      && has_local_flag_ >= ObTableHasLocalFlag::HAS_LOCAL && has_local_flag_ < ObTableHasLocalFlag::MAX;
}


void ObTableBackupFlag::clear()
{
  has_backup_flag_ = ObTableHasBackupFlag::NO_BACKUP;
  has_local_flag_ = ObTableHasLocalFlag::NO_LOCAL;
  reserved_ = 0;
}

OB_SERIALIZE_MEMBER(ObTableBackupFlag, flag_);



ObTableSharedFlag::ObTableSharedFlag()
  : shared_flag_(PRIVATE),
    is_split_sstable_(0),
    reserved_(0)
{
}

ObTableSharedFlag::~ObTableSharedFlag()
{
}

void ObTableSharedFlag::reset()
{
  shared_flag_ = PRIVATE;
  reserved_ = 0;
}

bool ObTableSharedFlag::is_valid() const
{
  return shared_flag_ >= PRIVATE && shared_flag_ < MAX;
}


OB_SERIALIZE_MEMBER(ObTableSharedFlag, flag_);


}
}

