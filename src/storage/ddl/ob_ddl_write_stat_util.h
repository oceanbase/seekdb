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

#ifndef OB_STORAGE_DDL_OB_DDL_WRITE_STAT_UTIL_H_
#define OB_STORAGE_DDL_OB_DDL_WRITE_STAT_UTIL_H_

#include "storage/ob_i_table.h"

namespace oceanbase
{
namespace storage
{
class ObWriteMacroParam;
struct ObDDLWriteStat;

// split from share ObDDLUtil(only user of the nested ObITable::TableKey name, definition and callers are both in storage)
class ObDDLStorageWriteUtil
{
public:
  static int get_ddl_write_stat(
      const ObWriteMacroParam &param,
      const ObITable::TableKey &table_key,
      ObDDLWriteStat *&ddl_write_stat);
};

}  // namespace storage
}  // namespace oceanbase

#endif
