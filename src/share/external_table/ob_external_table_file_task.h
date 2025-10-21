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

#ifndef OBDEV_SRC_EXTERNAL_TABLE_FILE_TASK_H_
#define OBDEV_SRC_EXTERNAL_TABLE_FILE_TASK_H_
#include "rpc/obrpc/ob_rpc_result_code.h"
#include "deps/oblib/src/lib/lock/ob_thread_cond.h"
#include "sql/engine/expr/ob_expr_regexp_context.h"
namespace oceanbase
{
namespace share
{

class ObFlushExternalTableFileCacheReq
{
  OB_UNIS_VERSION(1);
public:
  ObFlushExternalTableFileCacheReq() :
    tenant_id_(common::OB_INVALID_ID), table_id_(common::OB_INVALID_ID), partition_id_(common::OB_INVALID_ID) {}
public:
  uint64_t tenant_id_;
  int64_t table_id_;
  int64_t partition_id_;
  TO_STRING_KV(K_(tenant_id), K_(table_id), K_(partition_id));
};

class ObFlushExternalTableFileCacheRes
{
  OB_UNIS_VERSION(1);
public:
  ObFlushExternalTableFileCacheRes() : rcode_() {}
  TO_STRING_KV(K_(rcode));
public:
  obrpc::ObRpcResultCode rcode_;
};

class ObLoadExternalFileListReq
{
  OB_UNIS_VERSION(1);
public:
  ObLoadExternalFileListReq() :
   location_(), pattern_() {}
public:
  ObString location_;
  ObString pattern_;
  sql::ObExprRegexpSessionVariables regexp_vars_;
public:
  TO_STRING_KV(K_(location), K_(pattern), K_(regexp_vars));
};

class ObLoadExternalFileListRes
{
  OB_UNIS_VERSION(1);
public:
  ObLoadExternalFileListRes() : rcode_(), file_urls_(), file_sizes_(), allocator_() {}

  ObIAllocator &get_alloc() { return allocator_; }
  TO_STRING_KV(K_(rcode));
public:
  obrpc::ObRpcResultCode rcode_; // the returned error message
  ObSEArray<ObString, 8> file_urls_;
  ObSEArray<int64_t, 8> file_sizes_;

private:
  ObArenaAllocator allocator_;
};




}  // namespace share
}  // namespace oceanbase
#endif /* OBDEV_SRC_EXTERNAL_TABLE_FILE_TASK_H_ */
