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

#ifndef OCEANBASE_SHARE_SCHEMA_OB_STANDBY_SCHEMA_REFRESH_TRIGGER_H_
#define OCEANBASE_SHARE_SCHEMA_OB_STANDBY_SCHEMA_REFRESH_TRIGGER_H_

#include "rootserver/ob_tenant_thread_helper.h"

namespace oceanbase
{
namespace share
{
namespace schema
{

class ObStandbySchemaRefreshTrigger : public rootserver::ObTenantThreadHelper
{
public:
  ObStandbySchemaRefreshTrigger() : is_inited_(false) {}
  virtual ~ObStandbySchemaRefreshTrigger() {}

  int init();
  void destroy();
  virtual void do_work() override;

  DEFINE_MTL_FUNC(ObStandbySchemaRefreshTrigger)

private:
  int check_inner_stat_();
  int submit_tenant_refresh_schema_task_();
  static const int64_t DEFAULT_IDLE_TIME = 1000 * 1000;  // 1s

  bool is_inited_;
};

} // namespace schema
} // namespace share
} // namespace oceanbase

#endif /* OCEANBASE_SHARE_SCHEMA_OB_STANDBY_SCHEMA_REFRESH_TRIGGER_H_ */
