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

#ifndef OCEANBASE_ROOTSERVER_OB_RS_JOB_TYPE_H_
#define OCEANBASE_ROOTSERVER_OB_RS_JOB_TYPE_H_

namespace oceanbase
{
namespace rootserver
{

enum ObRsJobType
{
  JOB_TYPE_INVALID = 0,
  JOB_TYPE_ALTER_TENANT_LOCALITY,
  JOB_TYPE_ROLLBACK_ALTER_TENANT_LOCALITY, // deprecated in V4.2
  JOB_TYPE_MIGRATE_UNIT,
  JOB_TYPE_DELETE_SERVER,
  JOB_TYPE_SHRINK_RESOURCE_TENANT_UNIT_NUM, // deprecated in V4.2
  JOB_TYPE_RESTORE_TENANT,
  JOB_TYPE_UPGRADE_STORAGE_FORMAT_VERSION,
  JOB_TYPE_STOP_UPGRADE_STORAGE_FORMAT_VERSION,
  JOB_TYPE_CREATE_INNER_SCHEMA,
  JOB_TYPE_UPGRADE_POST_ACTION,
  JOB_TYPE_UPGRADE_SYSTEM_VARIABLE,
  JOB_TYPE_UPGRADE_SYSTEM_TABLE,
  JOB_TYPE_UPGRADE_BEGIN,
  JOB_TYPE_UPGRADE_VIRTUAL_SCHEMA,
  JOB_TYPE_UPGRADE_SYSTEM_PACKAGE,
  JOB_TYPE_UPGRADE_ALL_POST_ACTION,
  JOB_TYPE_UPGRADE_INSPECTION,
  JOB_TYPE_UPGRADE_END,
  JOB_TYPE_UPGRADE_ALL,
  JOB_TYPE_ALTER_RESOURCE_TENANT_UNIT_NUM,
  JOB_TYPE_ALTER_TENANT_PRIMARY_ZONE,
  JOB_TYPE_UPGRADE_FINISH,
  JOB_TYPE_LOAD_MYSQL_SYS_PACKAGE,
  JOB_TYPE_MAX
};

}  // namespace rootserver
}  // namespace oceanbase

#endif /* OCEANBASE_ROOTSERVER_OB_RS_JOB_TYPE_H_ */
