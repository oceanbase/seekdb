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

#ifndef _OB_SHARE_RESOURCE_OB_RESOURCE_MANAGER_H_
#define _OB_SHARE_RESOURCE_OB_RESOURCE_MANAGER_H_

#include "share/ob_define.h"
#include "share/resource_manager/ob_resource_plan_manager.h"

namespace oceanbase
{
namespace common
{
class ObString;
}
namespace share
{

class ObResourceManager
{
public:
  static ObResourceManager &get_instance();
  int init();
  ObResourcePlanManager &get_plan_mgr() { return res_plan_mgr_; }
private:
  ObResourceManager() = default;
  virtual ~ObResourceManager() = default;
private:
  /* variables */
  ObResourcePlanManager res_plan_mgr_;
  DISALLOW_COPY_AND_ASSIGN(ObResourceManager);
};

#define G_RES_MGR (::oceanbase::share::ObResourceManager::get_instance())

}
}
#endif /* _OB_SHARE_RESOURCE_OB_RESOURCE_MANAGER_H_ */
//// end of header file

