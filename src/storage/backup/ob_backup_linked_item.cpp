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

#include "storage/backup/ob_backup_linked_item.h"

namespace oceanbase
{
namespace backup
{

ObBackupLinkedBlockHeader::ObBackupLinkedBlockHeader()
  : version_(),
    magic_(),
    item_count_(0),
    tablet_id_(),
    table_key_(),
    prev_block_addr_(),
    has_prev_(false)
{
}

ObBackupLinkedBlockHeader::~ObBackupLinkedBlockHeader()
{
}

OB_SERIALIZE_MEMBER(ObBackupLinkedBlockHeader,
                    version_,
                    magic_,
                    item_count_,
                    tablet_id_,
                    table_key_,
                    prev_block_addr_,
                    has_prev_);

ObBackupLinkedItem::ObBackupLinkedItem()
  : macro_id_(),
    backup_id_()
{
}

ObBackupLinkedItem::~ObBackupLinkedItem()
{
}

void ObBackupLinkedItem::reset()
{
  macro_id_.reset();
  backup_id_.reset();
}

bool ObBackupLinkedItem::is_valid() const
{
  return macro_id_.is_valid()
      && backup_id_.is_valid();
}

bool ObBackupLinkedItem::operator==(const ObBackupLinkedItem &other) const
{
  return macro_id_ == other.macro_id_
      && backup_id_ == other.backup_id_;
}

bool ObBackupLinkedItem::operator!=(const ObBackupLinkedItem &other) const
{
  return !(other == *this);
}

OB_SERIALIZE_MEMBER(ObBackupLinkedItem,
                    macro_id_,
                    backup_id_);

}
}
