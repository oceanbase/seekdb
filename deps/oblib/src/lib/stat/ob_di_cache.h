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

#ifndef OB_DI_CACHE_H_
#define OB_DI_CACHE_H_
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <pthread.h>
#include <new>
#include <typeinfo>
#include "lib/ob_define.h"
#include "lib/stat/ob_diagnose_info.h"
#include "lib/stat/ob_di_list.h"
#include "lib/container/ob_array.h"

namespace oceanbase
{
namespace common
{
class ObDISessionCollect : public ObDINode<ObDISessionCollect>
{
public:
  ObDISessionCollect();
  virtual ~ObDISessionCollect();
  TO_STRING_KV(K_(session_id), K_(client_sid), "di_session_info", (uint64_t)&base_value_, K(lock_.get_lock()));
  uint64_t session_id_;
  uint32_t client_sid_;
  ObDiagnoseSessionInfo base_value_;
  DIRWLock lock_;
};

class ObDITenantCollect : public ObDINode<ObDITenantCollect>
{
public:
  ObDITenantCollect(ObIAllocator *allocator = NULL);
  virtual ~ObDITenantCollect();
  uint64_t tenant_id_;
  uint64_t last_access_time_;
  ObDiagnoseTenantInfo base_value_;
};

struct AddWaitEvent
{
  AddWaitEvent() {}
  void operator()(ObDiagnoseTenantInfo &base_info, const ObDiagnoseTenantInfo &info)
  {
    base_info.add_wait_event(info);
  }
};

struct AddStatEvent
{
  AddStatEvent() {}
  void operator()(ObDiagnoseTenantInfo &base_info, const ObDiagnoseTenantInfo &info)
  {
    base_info.add_stat_event(info);
  }
};

struct AddLatchStat
{
  AddLatchStat() {}
  void operator()(ObDiagnoseTenantInfo &base_info, const ObDiagnoseTenantInfo &info)
  {
    base_info.add_latch_stat(info);
  }
};

}// end namespace common
}
#endif /* OB_DI_CACHE_H_ */
