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

#ifndef OCEANBASE_SQL_OB_RECYCLEBIN_RESTORE_RESOLVER_
#define OCEANBASE_SQL_OB_RECYCLEBIN_RESTORE_RESOLVER_

#include "sql/resolver/ddl/ob_recyclebin_restore_stmt.h"
#include "sql/resolver/ddl/ob_ddl_resolver.h"

namespace oceanbase
{
namespace sql
{

class ObRecyclebinRestoreTableResolver : public ObDDLResolver
{
  static const int ORIGIN_TABLE_NODE = 0;
  static const int NEW_TABLE_NODE = 1;
public:
  explicit ObRecyclebinRestoreTableResolver(ObResolverParams &params)
    : ObDDLResolver(params){}
  virtual ~ObRecyclebinRestoreTableResolver() {}
  virtual int resolve(const ParseNode &parse_tree);

private:
  DISALLOW_COPY_AND_ASSIGN(ObRecyclebinRestoreTableResolver);
};

class ObRecyclebinRestoreDatabaseResolver : public ObDDLResolver
{
  static const int ORIGIN_DB_NODE = 0;
  static const int NEW_DB_NODE = 1;
public:
  explicit ObRecyclebinRestoreDatabaseResolver(ObResolverParams &params)
   : ObDDLResolver(params){}
  virtual ~ObRecyclebinRestoreDatabaseResolver() {}
  virtual int resolve(const ParseNode &parse_tree);

private:
  DISALLOW_COPY_AND_ASSIGN(ObRecyclebinRestoreDatabaseResolver);
};

}
}
#endif // OCEANBASE_SQL_OB_RECYCLEBIN_RESTORE_RESOLVER_
