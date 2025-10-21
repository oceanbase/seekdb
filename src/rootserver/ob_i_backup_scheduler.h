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

#ifndef OCEANBASE_ROOTSERVER_OB_I_BACKUP_SCHEDULER_H_
#define OCEANBASE_ROOTSERVER_OB_I_BACKUP_SCHEDULER_H_

#include "ob_rs_reentrant_thread.h"

namespace oceanbase
{
namespace rootserver
{

class ObIBackupScheduler : public ObRsReentrantThread
{
public:
  ObIBackupScheduler()
  {
  }
  virtual ~ObIBackupScheduler()
  {
  }
  virtual bool is_working() const = 0;

  // force cancel backup task, such as backup database, backupbackup, validate, archive etc.
  virtual int force_cancel(const uint64_t tenant_id) = 0;
};

} //rootserver
} //oceanbase


#endif /* OCEANBASE_ROOTSERVER_OB_I_BACKUP_SCHEDULER_H_ */
