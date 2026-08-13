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

#ifndef OCEANBASE_SHARE_SCHEMA_OB_PART_MGR_UTIL_
#define OCEANBASE_SHARE_SCHEMA_OB_PART_MGR_UTIL_

#include <stdint.h>
#include "lib/oblog/ob_log.h"
#include "share/ob_define.h"
#include "lib/string/ob_string.h"
#include "share/schema/ob_schema_struct.h"
#include "share/schema/ob_partition_schema_iter.h"

namespace oceanbase
{
namespace common
{
template<class T>
class ObIArray;
}
namespace share
{
namespace schema {

class ObTableSchema;
class ObSimpleTableSchemaV2;

class ObPartGetter
{
public:
  ObPartGetter(const ObTableSchema &table)
      : table_(table)
  {}
  int get_part_ids(const common::ObString &part_name, common::ObIArray<ObObjectID> &part_ids);
  int get_subpart_ids(const common::ObString &part_name, common::ObIArray<ObObjectID> &part_ids);
private:
  ObPartGetter();
  int get_subpart_ids_in_partition(const common::ObString &part_name,
                                   const ObPartition &partition,
                                   common::ObIArray<ObObjectID> &part_ids,
                                   bool &find);
private:
  const ObTableSchema &table_;
};


}
}
}
#endif
