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
#include "rootserver/freeze/ob_fts_checksum_validate_util.h"
#include "share/rc/ob_tenant_base.h"

namespace oceanbase
{
namespace rootserver
{
ObFTSGroup::ObFTSGroup()
    : data_table_id_(0),
      rowkey_doc_index_id_(0),
      doc_rowkey_index_id_(0),
      index_info_()
{
  index_info_.set_attr(ObMemAttr(MTL_ID(), "FTS_GROUP"));
}

ObFTSGroupArray::ObFTSGroupArray()
  : fts_groups_()
{
  fts_groups_.set_attr(ObMemAttr(MTL_ID(), "FTS_INFO_ARR"));
}

bool ObFTSGroupArray::need_check_fts() const
{
  return count() > 0;
}

} // namespace rootserver
} // namespace oceanbase
