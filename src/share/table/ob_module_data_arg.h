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

#ifndef OCEANBASE_SHARE_TABLE_OB_MODULE_DATA_ARG_H_
#define OCEANBASE_SHARE_TABLE_OB_MODULE_DATA_ARG_H_
#include "lib/ob_define.h"
#include "lib/string/ob_string.h"
#include "lib/utility/ob_print_utils.h"
namespace oceanbase
{
namespace table
{
struct ObModuleDataArg
{
public:
  enum ObInfoOpType {
    INVALID_OP = -1,
    LOAD_INFO,
    CHECK_INFO,
    MAX_OP
  };
  enum ObExecModule {
    INVALID_MOD = -1,
    REDIS,
    TIMEZONE,
    GIS,
    MAX_MOD
  };
  ObModuleDataArg() : 
    op_(ObInfoOpType::INVALID_OP),
    module_(ObExecModule::INVALID_MOD),
    file_path_()
  {}
  virtual ~ObModuleDataArg() {}
  bool is_valid() const;
  TO_STRING_KV(K_(op), K_(module), K_(file_path));

  ObInfoOpType op_; // enum ObInfoOpType
  ObExecModule module_; // ObExecModule
  ObString file_path_;
};
}  // namespace table
}  // namespace oceanbase
#endif
