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

#ifndef OCEANBASE_RPC_FRAME_OB_RPC_CHECKSUM_H_
#define OCEANBASE_RPC_FRAME_OB_RPC_CHECKSUM_H_

#include "lib/string/ob_string.h"

namespace oceanbase
{
namespace rpc
{
namespace frame
{

#ifdef OPTIONAL
#undef OPTIONAL
#endif

enum class ObReqCheckSumCheckLevel
{
  INVALID,
  FORCE,
  OPTIONAL,
  DISABLE
};

extern ObReqCheckSumCheckLevel g_rpc_checksum_check_level;

inline void set_rpc_checksum_check_level(
    const ObReqCheckSumCheckLevel rpc_checksum_check_level)
{
  g_rpc_checksum_check_level = rpc_checksum_check_level;
}

inline ObReqCheckSumCheckLevel get_rpc_checksum_check_level()
{
  return g_rpc_checksum_check_level;
}

inline ObReqCheckSumCheckLevel get_rpc_checksum_check_level_from_string(
    const common::ObString &string)
{
  ObReqCheckSumCheckLevel ret_type = ObReqCheckSumCheckLevel::INVALID;
  if (0 == string.case_compare("Force")) {
    ret_type = ObReqCheckSumCheckLevel::FORCE;
  } else if (0 == string.case_compare("Optional")) {
    ret_type = ObReqCheckSumCheckLevel::OPTIONAL;
  } else if (0 == string.case_compare("Disable")) {
    ret_type = ObReqCheckSumCheckLevel::DISABLE;
  }
  return ret_type;
}

} // namespace frame
} // namespace rpc
} // namespace oceanbase

#endif // OCEANBASE_RPC_FRAME_OB_RPC_CHECKSUM_H_
