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

#ifndef _OB_MODULE_DATA_RESOLVER_H
#define _OB_MODULE_DATA_RESOLVER_H 1

#include "sql/resolver/cmd/ob_alter_system_stmt.h"
#include "sql/resolver/cmd/ob_system_cmd_resolver.h"

namespace oceanbase
{
namespace sql
{
class ObModuleDataResolver : public ObSystemCmdResolver 
{ 
public: 
  ObModuleDataResolver(ObResolverParams &params) : ObSystemCmdResolver(params) {} 
  virtual ~ObModuleDataResolver() {} 
  virtual int resolve(const ParseNode &parse_tree); 

private:
  static const int32_t CHILD_NUM = 4;
  static const int32_t TYPE_IDX = 0;
  static const int32_t MODULE_IDX = 1;
  static const int32_t TENANT_IDX = 2;
  static const int32_t FILE_IDX = 3;
  int resolve_exec_type(const ParseNode *node, table::ObModuleDataArg::ObInfoOpType &type);
  int resolve_module(const ParseNode *node, table::ObModuleDataArg::ObExecModule &mod);
  int resolve_target_tenant_id(const ParseNode *node,
                               const table::ObModuleDataArg::ObExecModule mod,
                               uint64_t &target_tenant_id);
  int resolve_file_path(const ParseNode *node, table::ObModuleDataArg &arg);
};
}//namespace sql
}//namespace oceanbase
#endif // _OB_MODULE_DATA_RESOLVER_H
