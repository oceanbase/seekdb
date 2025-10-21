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

#ifndef OCEANBASE_SHARE_OB_DDL_ARGS_H_
#define OCEANBASE_SHARE_OB_DDL_ARGS_H_

#include "lib/ob_define.h"
#include "share/schema/ob_schema_struct.h"

namespace oceanbase
{
namespace obrpc
{
struct ObDDLArg
{
  OB_UNIS_VERSION_V(1);
public:
  ObDDLArg() :
      ddl_stmt_str_(),
      exec_tenant_id_(common::OB_INVALID_TENANT_ID),
      ddl_id_str_(),
      sync_from_primary_(false),
      based_schema_object_infos_(),
      parallelism_(0),
      task_id_(0),
      consumer_group_id_(0),
      is_parallel_(false)
   { }
  virtual ~ObDDLArg() = default;
  bool is_need_check_based_schema_objects() const
  {
    return 0 < based_schema_object_infos_.count();
  }
  virtual bool is_allow_when_disable_ddl() const { return false; }
  virtual bool is_allow_when_upgrade() const { return false; }
  bool is_sync_from_primary() const
  {
    return sync_from_primary_;
  }
  //user tenant can not ddl in standby
  virtual bool is_allow_in_standby() const
  { return !is_user_tenant(exec_tenant_id_); }
  virtual int assign(const ObDDLArg &other);
  virtual bool contain_sensitive_data() const { return false; }
  void reset()
  {
    ddl_stmt_str_.reset();
    exec_tenant_id_ = common::OB_INVALID_TENANT_ID;
    ddl_id_str_.reset();
    sync_from_primary_ = false;
    based_schema_object_infos_.reset();
    parallelism_ = 0;
    task_id_ = 0;
    consumer_group_id_ = 0;
    is_parallel_ = false;
  }
  DECLARE_TO_STRING;

  common::ObString ddl_stmt_str_;
  uint64_t exec_tenant_id_;
  common::ObString ddl_id_str_;
  bool sync_from_primary_;
  common::ObSArray<share::schema::ObBasedSchemaObjectInfo> based_schema_object_infos_;
  int64_t parallelism_;
  int64_t task_id_;
  int64_t consumer_group_id_;
  //some parallel ddl is effect before 4220, this member is valid after 4220
  bool is_parallel_;
};

} // namespace obrpc
} // namespace oceanbase
#endif
