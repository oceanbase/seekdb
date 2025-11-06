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

#ifndef OCEANBASE_ROOTSERVER_STANDBY_SCHEMA_REFRESH_TRIGGER_H
#define OCEANBASE_ROOTSERVER_STANDBY_SCHEMA_REFRESH_TRIGGER_H

#include "lib/thread/ob_reentrant_thread.h"//ObRsReentrantThread
#include "lib/utility/ob_print_utils.h" //TO_STRING_KV
#include "share/ob_tenant_info_proxy.h"//ObAllTenantInfo
#include "rootserver/ob_primary_ls_service.h"//ObTenantThreadHelper

namespace oceanbase {
namespace common
{
class ObMySQLProxy;
}
namespace share
{
class SCN;
}
namespace rootserver
{

class ObStandbySchemaRefreshTrigger : public ObTenantThreadHelper
{
public:
  ObStandbySchemaRefreshTrigger() : is_inited_(false),
    tenant_id_(OB_INVALID_TENANT_ID), sql_proxy_(NULL) {}
  virtual ~ObStandbySchemaRefreshTrigger() {}
  int init();
  void destroy();
  virtual void do_work() override;

  DEFINE_MTL_FUNC(ObStandbySchemaRefreshTrigger)

private:
  int check_inner_stat_();
  int submit_tenant_refresh_schema_task_();
  const static int64_t DEFAULT_IDLE_TIME = 1000 * 1000;  // 1s

public:
 TO_STRING_KV(K_(is_inited), K_(tenant_id), KP_(sql_proxy));

private:
  bool is_inited_;
  uint64_t tenant_id_;
  common::ObMySQLProxy *sql_proxy_;
};

} // namespace rootserver
} // namespace oceanbase

#endif /* !OCEANBASE_ROOTSERVER_STANDBY_SCHEMA_REFRESH_TRIGGER_H */
