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

#ifndef OCEANBASE_SQL_OB_Purge_RESOLVER_
#define OCEANBASE_SQL_OB_Purge_RESOLVER_

#include "sql/resolver/ddl/ob_purge_stmt.h"
#include "sql/resolver/ddl/ob_ddl_resolver.h"
#include "sql/session/ob_sql_session_info.h"

namespace oceanbase
{
namespace sql
{

class ObPurgeTableResolver : public ObDDLResolver
{
  static const int TABLE_NODE = 0;
public:
  explicit ObPurgeTableResolver(ObResolverParams &params)
    : ObDDLResolver(params){}
  virtual ~ObPurgeTableResolver() {}
  virtual int resolve(const ParseNode &parse_tree);

private:
  static const int OLD_NAME_NODE = 0;
  static const int NEW_NAME_NODE = 1;
  DISALLOW_COPY_AND_ASSIGN(ObPurgeTableResolver);
};

class ObPurgeIndexResolver : public ObDDLResolver
{
  static const int TABLE_NODE = 0;
public:
  explicit ObPurgeIndexResolver(ObResolverParams &params)
    : ObDDLResolver(params){}
  virtual ~ObPurgeIndexResolver() {}
  virtual int resolve(const ParseNode &parse_tree);

private:
  static const int OLD_NAME_NODE = 0;
  static const int NEW_NAME_NODE = 1;
  DISALLOW_COPY_AND_ASSIGN(ObPurgeIndexResolver);
};

class ObPurgeDatabaseResolver : public ObDDLResolver
{
  static const int DATABASE_NODE = 0;
public:
  explicit ObPurgeDatabaseResolver(ObResolverParams &params)
   : ObDDLResolver(params){}
  virtual ~ObPurgeDatabaseResolver() {}
  virtual int resolve(const ParseNode &parse_tree);

private:
  DISALLOW_COPY_AND_ASSIGN(ObPurgeDatabaseResolver);
};

class ObPurgeTenantResolver : public ObDDLResolver
{
  static const int TENANT_NODE = 0;
public:
  explicit ObPurgeTenantResolver(ObResolverParams &params)
    : ObDDLResolver(params){}
  virtual ~ObPurgeTenantResolver() {}
  virtual int resolve(const ParseNode &parse_tree);

private:
  DISALLOW_COPY_AND_ASSIGN(ObPurgeTenantResolver);
};

class ObPurgeRecycleBinResolver : public ObDDLResolver
{
public:
  explicit ObPurgeRecycleBinResolver(ObResolverParams &params)
    : ObDDLResolver(params){}
  virtual ~ObPurgeRecycleBinResolver() {}
  virtual int resolve(const ParseNode &parse_tree);

private:
  DISALLOW_COPY_AND_ASSIGN(ObPurgeRecycleBinResolver);
};

} //namespace sql
} //namespace oceanbase
#endif //OCEANBASE_SQL_OB_PURGE_RESOLVER_



