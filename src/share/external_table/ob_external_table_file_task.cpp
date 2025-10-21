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
#define USING_LOG_PREFIX SQL
#include "ob_external_table_file_task.h"
#include "share/external_table/ob_external_table_file_rpc_processor.h"

namespace oceanbase
{
namespace share
{


OB_SERIALIZE_MEMBER(ObFlushExternalTableFileCacheReq, tenant_id_, table_id_, partition_id_);

OB_SERIALIZE_MEMBER(ObFlushExternalTableFileCacheRes, rcode_);

OB_SERIALIZE_MEMBER(ObLoadExternalFileListReq, location_, pattern_, regexp_vars_);

OB_DEF_SERIALIZE(ObLoadExternalFileListRes)
{
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_ENCODE, rcode_, file_urls_, file_sizes_);
  return ret;
}

OB_DEF_SERIALIZE_SIZE(ObLoadExternalFileListRes)
{
  int64_t len = 0;
  LST_DO_CODE(OB_UNIS_ADD_LEN, rcode_, file_urls_, file_sizes_);
  return len;
}

OB_DEF_DESERIALIZE(ObLoadExternalFileListRes)
{
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_DECODE, rcode_, file_urls_, file_sizes_);
  for (int64_t i = 0; OB_SUCC(ret) && i < file_urls_.count(); i++) {
    ObString file_url;
    OZ (ob_write_string(allocator_, file_urls_.at(i), file_url));
    file_urls_.at(i).assign_ptr(file_url.ptr(), file_url.length());

  }
  return ret;
}



}  // namespace share
}  // namespace oceanbase
