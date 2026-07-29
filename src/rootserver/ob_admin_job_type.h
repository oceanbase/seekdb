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

#ifndef OCEANBASE_ROOTSERVER_OB_ADMIN_JOB_TYPE_H_
#define OCEANBASE_ROOTSERVER_OB_ADMIN_JOB_TYPE_H_

namespace oceanbase
{
namespace rootserver
{

enum ObAdminJobType
{
  JOB_TYPE_INVALID = 0,
  JOB_TYPE_RESTORE_TENANT,
  JOB_TYPE_CREATE_INNER_SCHEMA,
  JOB_TYPE_LOAD_MYSQL_SYS_PACKAGE,
  JOB_TYPE_MAX
};

}  // namespace rootserver
}  // namespace oceanbase

#endif /* OCEANBASE_ROOTSERVER_OB_ADMIN_JOB_TYPE_H_ */
