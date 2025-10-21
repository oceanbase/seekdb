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

#include "lib/ob_define.h"
#include "common/sql_mode/ob_sql_mode.h"
#include "lib/worker.h"

#ifndef __OB_SHARE_GET_COMPAT_MODE_H__
#define __OB_SHARE_GET_COMPAT_MODE_H__

namespace oceanbase
{
namespace common
{
class ObMySQLProxy;
class ObTabletID;
}
namespace share
{

class ObCompatModeGetter
{
public:
  static ObCompatModeGetter &instance();
  //Provide global function interface to external users
  static int get_tenant_mode(const uint64_t tenant_id, lib::Worker::CompatMode& mode);
  static int get_table_compat_mode(const uint64_t tenant_id, const int64_t table_id, lib::Worker::CompatMode& mode);
  static int get_tablet_compat_mode(const uint64_t tenant_id, const common::ObTabletID &tablet_id, lib::Worker::CompatMode& mode);
  static int check_is_oracle_mode_with_tenant_id(const uint64_t tenant_id, bool &is_oracle_mode);
  static int check_is_oracle_mode_with_table_id(
             const uint64_t tenant_id,
             const int64_t table_id,
             bool &is_oracle_mode);
  //Initialize hash table
  int init(common::ObMySQLProxy *proxy);
  // Init for OBCDC
  //
  // Avoid relying on SQL when CDC consumes archive logs offline
  //Release hash table memory
  void destroy();
  //According to the tenant id, get the compatibility mode of the tenant system variables, the first time it will send an internal SQL, afterwards it will directly read from the cache
  int get_tenant_compat_mode(const uint64_t tenant_id, lib::Worker::CompatMode& mode);
  // only for unittest used

private:
  ObCompatModeGetter();
  ~ObCompatModeGetter();
  DISALLOW_COPY_AND_ASSIGN(ObCompatModeGetter);
};

}   //end share
}   //end oceanbase

#endif
