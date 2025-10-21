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

#ifndef _OB_MEM_LEAK_CHECKER_INFO_H_
#define _OB_MEM_LEAK_CHECKER_INFO_H_

#include "share/ob_define.h"
#include "lib/net/ob_addr.h"
#include "lib/allocator/ob_mem_leak_checker.h"

#include "share/ob_virtual_table_iterator.h"
#include "share/ob_scanner.h"
#include "common/row/ob_row.h"

namespace oceanbase
{
namespace allocator
{
}

namespace observer
{
class ObMemLeakChecker;
class ObMemLeakCheckerInfo : public common::ObVirtualTableIterator
{
public:
  ObMemLeakCheckerInfo();
  virtual ~ObMemLeakCheckerInfo();

  inline void set_addr(common::ObAddr &addr) {addr_ = &addr;}
  inline void set_tenant_id(uint64_t tenant_id) {tenant_id_ = tenant_id;}
  virtual int inner_get_next_row(common::ObNewRow *&row);
  virtual void reset();
private:
  int sanity_check();
  int fill_row(common::ObNewRow *&row);
private:
  bool opened_;
  common::ObMemLeakChecker *leak_checker_;
  common::ObMemLeakChecker::mod_info_map_t::hashmap::const_iterator it_;
  common::ObMemLeakChecker::mod_info_map_t info_map_;
  common::ObAddr *addr_;
  uint64_t tenant_id_;
  const char *label_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObMemLeakCheckerInfo);
};
}
}

#endif /* _OB_MEM_LEAK_CHECKER_INFO_H_ */
