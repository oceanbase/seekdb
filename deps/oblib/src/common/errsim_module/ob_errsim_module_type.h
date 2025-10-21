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

#ifndef SRC_SHARE_ERRSIM_MODULE_OB_ERRSIM_MODULE_TYPE_H_
#define SRC_SHARE_ERRSIM_MODULE_OB_ERRSIM_MODULE_TYPE_H_

#include "lib/ob_define.h"
#include "lib/utility/ob_print_utils.h"

namespace oceanbase
{
namespace common
{

struct ObErrsimModuleType final
{
  OB_UNIS_VERSION(1);
public:
  enum TYPE
  {
    ERRSIM_MODULE_NONE = 0,
    ERRSIM_MODULE_ALL = 1,
    ERRSIM_MODULE_MIGRATION = 2,
    ERRSIM_MODULE_TRANSFER = 3,
    ERRSIM_MODULE_MAX
  };
  ObErrsimModuleType() : type_(ERRSIM_MODULE_NONE) {}
  explicit ObErrsimModuleType(const ObErrsimModuleType::TYPE &type) : type_(type) {}
  ~ObErrsimModuleType() = default;
  void reset();
  bool is_valid() const;
  const char *get_str();
  bool operator == (const ObErrsimModuleType &other) const;
  int hash(uint64_t &hash_val) const;
  int64_t hash() const;
  TO_STRING_KV(K_(type));
  TYPE type_;
};

struct ObErrsimModuleTypeHelper final
{
  static const char *get_str(const ObErrsimModuleType::TYPE &type);
  static ObErrsimModuleType::TYPE get_type(const char *type_str);
  static bool is_valid(const ObErrsimModuleType::TYPE &type);
  static const int64_t MAX_TYPE_NAME_LENGTH = 16;
};


} // namespace common
} // namespace oceanbase
#endif
