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

#ifndef OCEANBASE_ROOTSERVER_OB_CORE_META_TABLE_H_
#define OCEANBASE_ROOTSERVER_OB_CORE_META_TABLE_H_

#include "share/ob_virtual_table_projector.h"

namespace oceanbase
{
namespace share
{
class ObLSReplica;
class ObLSTableOperator;
namespace schema
{
class ObTableSchema;
class ObColumnSchemaV2;
}
}
namespace rootserver
{
class ObCoreMetaTable : public common::ObVirtualTableProjector
{
public:
  ObCoreMetaTable();
  virtual ~ObCoreMetaTable();

  int init(share::ObLSTableOperator &lst_operator,
           share::schema::ObSchemaGetterGuard *schema_guard);

  virtual int inner_get_next_row(common::ObNewRow *&row);
private:
  int get_full_row(const share::schema::ObTableSchema *table,
                   const share::ObLSReplica &replica,
                   common::ObIArray<Column> &columns);
  bool inited_;
  share::ObLSTableOperator *lst_operator_;
  share::schema::ObSchemaGetterGuard *schema_guard_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObCoreMetaTable);
};

}//end namespace rootserver
}//end namespace oceanbase

#endif //OCEANBASE_ROOTSERVER_OB_CORE_META_TABLE_H_
