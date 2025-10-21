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

#ifndef OCEANBASE_SQL_OB_CATALOG_RESOLVER_H_
#define OCEANBASE_SQL_OB_CATALOG_RESOLVER_H_
#include "sql/resolver/ddl/ob_ddl_resolver.h"

namespace oceanbase
{
namespace sql
{
class ObCatalogResolver: public ObDDLResolver
{
  static const int64_t IF_NOT_EXIST = 0;
  static const int64_t CATALOG_NAME = 1;
  static const int64_t PROPERTIES = 2;
  static const int64_t CREATE_NUM_CHILD = 3;
  static const int64_t IF_EXIST = 0;
  static const int64_t DROP_NUM_CHILD = 2;
  static const int64_t SET_NUM_CHILD = 1;
public:
  explicit ObCatalogResolver(ObResolverParams &params);
  virtual ~ObCatalogResolver();
  virtual int resolve(const ParseNode &parse_tree);
private:
  int resolve_create_catalog(const ParseNode &parse_tree, obrpc::ObCatalogDDLArg &arg);
  int resolve_drop_catalog(const ParseNode &parse_tree, obrpc::ObCatalogDDLArg &arg);
  int resolve_set_catalog(const ParseNode &parse_tree, obrpc::ObCatalogDDLArg &arg);
  int resolve_catalog_name(const ParseNode &name_node, obrpc::ObCatalogDDLArg &arg);
  int check_internal_catalog_ddl(const ObString &name, const stmt::StmtType &stmt_type);
  int resolve_catalog_properties(const ParseNode &properties_node, obrpc::ObCatalogDDLArg &arg);
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObCatalogResolver);
};

} // end namespace sql
} // end namespace oceanbase

#endif /* OCEANBASE_SQL_OB_CATALOG_RESOLVER_H_*/
