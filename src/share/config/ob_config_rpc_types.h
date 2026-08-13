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

#ifndef OCEANBASE_SHARE_CONFIG_OB_CONFIG_RPC_TYPES_H_
#define OCEANBASE_SHARE_CONFIG_OB_CONFIG_RPC_TYPES_H_

#include "lib/string/ob_fixed_length_string.h"
#include "lib/utility/ob_print_utils.h"
#include "lib/utility/ob_unify_serialize.h"
#include "share/ob_define.h"

namespace oceanbase
{
namespace obcall
{

struct ObAdminSetConfigItem
{
  OB_UNIS_VERSION(1);
public:
  ObAdminSetConfigItem() : name_(), value_(), comment_() {}
  TO_STRING_KV(K_(name), K_(value), K_(comment));

  common::ObFixedLengthString<common::OB_MAX_CONFIG_NAME_LEN> name_;
  common::ObFixedLengthString<common::OB_MAX_CONFIG_VALUE_LEN> value_;
  common::ObFixedLengthString<common::OB_MAX_CONFIG_INFO_LEN> comment_;
};

} // namespace obcall
} // namespace oceanbase

#endif // OCEANBASE_SHARE_CONFIG_OB_CONFIG_RPC_TYPES_H_
