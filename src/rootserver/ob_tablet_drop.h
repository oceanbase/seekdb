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

#ifndef OB_TABLE_DROP_H
#define OB_TABLE_DROP_H

#include "lib/container/ob_array.h"
#include "lib/container/ob_iarray.h"
#include "lib/allocator/ob_malloc.h"
#include "common/mysqlclient/ob_mysql_transaction.h"
#include "share/ob_define.h"
#include "common/ob_tablet_id.h"

namespace oceanbase
{
namespace share
{
namespace schema
{
class ObTableSchema;
}

}

namespace rpc
{
class ObBatchRemoveTabletArg;
}
namespace rootserver
{

class ObTabletDrop
{
public:
  ObTabletDrop(
      ObMySQLTransaction &trans,
      int64_t schema_version)
                : trans_(trans),
                  allocator_("TbtDrop"),
                  tablet_ids_(NULL),
                  schema_version_(schema_version),
                  inited_(false) {}
  virtual ~ObTabletDrop();
  int init();
  int execute();
  // drop tablets in some table:
  // 1. one of which is data table, other are its local indexes,
  // 2. or all are local indexes of a table
  //
  // @param [in] table_schema, table schema for dropping tablets,
  // 1. the first is data table, others are its local indexes.
  // 2. or all are local indexes of a table
  int add_drop_tablets_of_table_arg(
      const common::ObIArray<const share::schema::ObTableSchema*> &schemas);
private:
  int drop_tablet_(
      const common::ObIArray<const share::schema::ObTableSchema *> &table_schema_ptr_array,
      const int64_t i, 
      const int64_t j,
      const bool is_hidden);
private:
  ObMySQLTransaction &trans_;
  ObArenaAllocator allocator_;
  common::ObIArray<ObTabletID> *tablet_ids_;
  int64_t schema_version_;
  bool inited_;
};
}
}



#endif /* !OB_TABLE_DROP_H */
