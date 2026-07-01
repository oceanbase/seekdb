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

#ifndef SRC_SHARE_ERRSIM_OB_TENANT_ERRSIM_MODULE_MGR_H_
#define SRC_SHARE_ERRSIM_OB_TENANT_ERRSIM_MODULE_MGR_H_

#include "lib/ob_define.h"
#include "lib/utility/ob_print_utils.h"
#include "common/errsim_module/ob_errsim_module_type.h"
#include "lib/hash/ob_hashset.h"
#include "lib/hash/ob_hashmap.h"
#include "lib/lock/ob_bucket_lock.h"
#include "lib/utility/ob_backtrace.h"
#include "common/errsim_module/ob_tenant_errsim_event.h"

namespace oceanbase
{
namespace share
{

class ObTenantErrsimEventMgr
{
public:
  ObTenantErrsimEventMgr();
  virtual ~ObTenantErrsimEventMgr();
  static int mtl_init(ObTenantErrsimEventMgr *&errsim_module_mgr);
  int init();
  int add_tenant_event(
      const ObTenantErrsimEvent &event);
  void destroy();
private:
  bool is_inited_;
  common::SpinRWLock lock_;
  ObArray<ObTenantErrsimEvent> event_array_;
  DISALLOW_COPY_AND_ASSIGN(ObTenantErrsimEventMgr);
};

} // namespace share
} // namespace oceanbase
#endif
