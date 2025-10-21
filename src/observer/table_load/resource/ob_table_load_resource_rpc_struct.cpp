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

#define USING_LOG_PREFIX SERVER

#include "ob_table_load_resource_rpc_struct.h"

namespace oceanbase
{
namespace observer
{

OB_SERIALIZE_MEMBER(ObDirectLoadResourceOpRequest, 
                    command_type_, 
                    arg_content_);

OB_UNIS_DEF_SERIALIZE(ObDirectLoadResourceOpResult, 
                      command_type_, 
                      res_content_);

OB_UNIS_DEF_SERIALIZE_SIZE(ObDirectLoadResourceOpResult, 
                           command_type_, 
                           res_content_);

OB_DEF_DESERIALIZE(ObDirectLoadResourceOpResult)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(allocator_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected null allocator in deserialize", K(ret));
  } else {
    ObString tmp_res_content;
    LST_DO_CODE(OB_UNIS_DECODE,
                command_type_,
                tmp_res_content);
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(ob_write_string(*allocator_, tmp_res_content, res_content_))) {
      LOG_WARN("fail to copy string", K(ret));
    }
  }

  return ret;
}

OB_SERIALIZE_MEMBER_SIMPLE(ObDirectLoadResourceUnit,
                           addr_, 
                           thread_count_, 
                           memory_size_);

OB_SERIALIZE_MEMBER_SIMPLE(ObDirectLoadResourceApplyArg, 
                           tenant_id_, 
                           task_key_, 
                           apply_array_);

OB_SERIALIZE_MEMBER_SIMPLE(ObDirectLoadResourceReleaseArg, 
                           tenant_id_, 
                           task_key_);

OB_SERIALIZE_MEMBER_SIMPLE(ObDirectLoadResourceUpdateArg, 
                           tenant_id_, 
                           thread_count_,
                           memory_size_,
                           addrs_);

OB_SERIALIZE_MEMBER_SIMPLE(ObDirectLoadResourceCheckArg, 
                           tenant_id_,
                           avail_memory_,
                           first_check_);

OB_SERIALIZE_MEMBER_SIMPLE(ObDirectLoadResourceOpRes, 
                           error_code_, 
                           avail_memory_,
                           assigned_array_);

} // namespace observer
} // namespace oceanbase
