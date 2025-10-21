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

#ifndef OCEANBASE_STORAGE_DICT_OB_DIC_LOCK_H_
#define OCEANBASE_STORAGE_DICT_OB_DIC_LOCK_H_
#include "storage/ddl/ob_ddl_lock.h"
#include "src/storage/fts/dict/ob_dic_loader.h"

namespace oceanbase
{
namespace storage
{
class ObDicLock : public ObDDLLock
{
public:
  static int lock_dic_tables_out_trans(
      const uint64_t tenant_id, 
      const ObTenantDicLoader &dic_loader, 
      const transaction::tablelock::ObTableLockMode lock_mode, 
      const transaction::tablelock::ObTableLockOwnerID &lock_owner);
  static int unlock_dic_tables(
      const uint64_t tenant_id, 
      const ObTenantDicLoader &dic_loader, 
      const transaction::tablelock::ObTableLockMode lock_mode, 
      const transaction::tablelock::ObTableLockOwnerID lock_owner,
      ObMySQLTransaction &trans);
  static int lock_dic_tables_in_trans(
      const int64_t tenant_id, 
      const ObTenantDicLoader &dic_loader,  
      const transaction::tablelock::ObTableLockMode lock_mode, 
      ObMySQLTransaction &trans);
private:
  static constexpr int64_t DEFAULT_TIMEOUT = 0;
};
} //end storage
} // end oceanbase



#endif //OCEANBASE_STORAGE_DICT_OB_DIC_LOCK_H_
