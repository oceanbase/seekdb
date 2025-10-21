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

#ifndef OCEANBASE_OBSERVER_VIRTUAL_TABLE_OB_ALL_LATCH_H_
#define OCEANBASE_OBSERVER_VIRTUAL_TABLE_OB_ALL_LATCH_H_

#include "lib/net/ob_addr.h"
#include "share/ob_virtual_table_iterator.h"

namespace oceanbase
{
namespace common
{
class ObDiagnoseTenantInfo;
}

namespace observer
{

class ObAllLatch : public common::ObVirtualTableIterator
{
public:
  ObAllLatch();
  virtual ~ObAllLatch();
  virtual int inner_open();
  virtual void reset();
  virtual int inner_get_next_row(common::ObNewRow *&row);
  virtual int get_all_diag_info();
  inline void set_addr(common::ObAddr &addr) {addr_ = &addr;}
private:
  int get_the_diag_info(const uint64_t tenant_id);
private:
  enum SYS_COLUMN
  {
    TENANT_ID = common::OB_APP_MIN_COLUMN_ID,
    SVR_IP,
    SVR_PORT,
    LATCH_ID,
    NAME,
    ADDR,
    LEVEL,
    HASH,
    GETS,
    MISSES,
    SLEEPS,
    IMMEDIATE_GETS,
    IMMEDIATE_MISSES,
    SPIN_GETS,
    WAIT_TIME
  };
  common::ObAddr *addr_;
  int32_t iter_;
  int64_t latch_iter_;
  common::ObArray<std::pair<uint64_t, common::ObDiagnoseTenantInfo*> > tenant_dis_;
  DISALLOW_COPY_AND_ASSIGN(ObAllLatch);
}; // end of class ObAllLatch

} // end of namespace observer
} // end of namespace oceanbase


#endif // OCEANBASE_OBSERVER_VIRTUAL_TABLE_OB_ALL_LATCH_H_
