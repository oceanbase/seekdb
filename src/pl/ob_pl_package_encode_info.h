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

#ifndef SRC_PL_OB_PL_PACKAGE_ENCODE_INFO_H_
#define SRC_PL_OB_PL_PACKAGE_ENCODE_INFO_H_

#include <cstdint>
#include "lib/container/ob_iarray.h"
#include "share/ob_define.h"
#include "lib/string/ob_string.h"
#include "lib/utility/ob_unify_serialize.h"
#include "common/object/ob_object.h"

namespace oceanbase
{
namespace pl
{

enum PackageValueType
{
  INVALID_VALUE_TYPE = -1,
  NULL_TYPE,
  BOOL_TYPE,
  HEX_STRING_TYPE
};

struct ObPackageVarEncodeInfo
{
  int64_t var_idx_;
  PackageValueType value_type_;
  int64_t value_len_;
  ObObj encode_value_;

  ObPackageVarEncodeInfo()
    : var_idx_(common::OB_INVALID_INDEX),
      value_type_(PackageValueType::INVALID_VALUE_TYPE),
      value_len_(0),
      encode_value_() {}

  int construct();

  int get_serialize_size(int64_t &size);
  int encode(char *dst, const int64_t dst_len, int64_t &dst_pos);
  int decode(const char *src, const int64_t src_len, int64_t &src_pos);

  TO_STRING_KV(K(var_idx_), K(value_type_), K(value_len_), K(encode_value_));
};

} //end namespace pl
} //end namespace oceanbase
#endif /* SRC_PL_OB_PL_PACKAGE_STATE_H_ */
