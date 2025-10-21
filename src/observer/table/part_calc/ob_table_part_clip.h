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

#ifndef OCEANBASE_OBSERVER_OB_TABLE_PART_CLIP_H_
#define OCEANBASE_OBSERVER_OB_TABLE_PART_CLIP_H_
#include "observer/table/ob_table_cache.h"

namespace oceanbase
{

namespace table
{

enum class ObTablePartClipType
{
  NONE = 0,
  HOT_ONLY = 1
};

class ObTablePartClipper
{
public:
  ObTablePartClipper() {}
  ~ObTablePartClipper() {}
public:
  static int clip(const share::schema::ObSimpleTableSchemaV2 &simple_schema,
                  ObTablePartClipType clip_type,
                  const common::ObIArray<common::ObTabletID> &src_tablet_ids,
                  common::ObIArray<common::ObTabletID> &dst_tablet_id);
private:
  DISALLOW_COPY_AND_ASSIGN(ObTablePartClipper);
};


} // end namespace table
} // end namespace oceanbase
#endif /* OCEANBASE_OBSERVER_OB_TABLE_PART_CLIP_H_ */
