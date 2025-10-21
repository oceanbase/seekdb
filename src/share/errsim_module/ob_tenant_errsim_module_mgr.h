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

#ifndef SRC_SHARE_ERRSIM_MODULE_OB_TENANT_ERRSIM_MODULE_MGR_H_
#define SRC_SHARE_ERRSIM_MODULE_OB_TENANT_ERRSIM_MODULE_MGR_H_

#include "lib/ob_define.h"
#include "lib/utility/ob_print_utils.h"
#include "common/errsim_module/ob_errsim_module_type.h"
#include "lib/hash/ob_hashset.h"
#include "lib/hash/ob_hashmap.h"
#include "lib/lock/ob_bucket_lock.h"

namespace oceanbase
{
namespace share
{

class ObTenantErrsimModuleMgr
{
public:
  typedef ObFixedLengthString<ObErrsimModuleTypeHelper::MAX_TYPE_NAME_LENGTH> ErrsimModuleString;
  typedef ObArray<ObFixedLengthString<ObErrsimModuleTypeHelper::MAX_TYPE_NAME_LENGTH>> ModuleArray;
public:
  ObTenantErrsimModuleMgr();
  virtual ~ObTenantErrsimModuleMgr();
  static int mtl_init(ObTenantErrsimModuleMgr *&errsim_module_mgr);
  int init(const uint64_t tenant_id);
  int build_tenant_moulde(
      const uint64_t tenant_id,
      const int64_t config_version,
      const ModuleArray &module_array,
      const int64_t percentage);
  bool is_errsim_module(
      const ObErrsimModuleType::TYPE &type);
  void destroy();

private:
  typedef hash::ObHashSet<ObErrsimModuleType> ErrsimModuleSet;
  static const int64_t MAX_BUCKET_NUM = 128;
  bool is_inited_;
  uint64_t tenant_id_;
  common::SpinRWLock lock_;
  int64_t config_version_;
  bool is_whole_module_;
  ErrsimModuleSet module_set_;
  int64_t percentage_;
  DISALLOW_COPY_AND_ASSIGN(ObTenantErrsimModuleMgr);
};

} // namespace share
} // namespace oceanbase
#endif
