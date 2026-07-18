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

#include "lib/task/ob_timer.h"

namespace oceanbase
{
namespace share
{
namespace schema
{

class ObStandbySchemaRefreshTrigger : public common::ObTimerTask
{
public:
  ObStandbySchemaRefreshTrigger() : is_inited_(false), is_scheduled_(false) {}
  virtual ~ObStandbySchemaRefreshTrigger() {}
  
  int init();
  int stop();
  int wait();
  void destroy();
  virtual void runTimerTask() override;

private:
  int schedule_();
  int check_inner_stat_();
  int submit_tenant_refresh_schema_task_();
  static const int64_t DEFAULT_IDLE_TIME = 1000 * 1000;  // 1s
  
  bool is_inited_;
  bool is_scheduled_;
};

} // namespace schema
} // namespace share
} // namespace oceanbase

#endif /* OCEANBASE_SHARE_SCHEMA_OB_STANDBY_SCHEMA_REFRESH_TRIGGER_H_ */
