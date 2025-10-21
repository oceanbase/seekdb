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

#ifndef OB_ALL_VIRTUAL_MEMORY_CONTEXT_STAT_H_
#define OB_ALL_VIRTUAL_MEMORY_CONTEXT_STAT_H_
#include "share/ob_virtual_table_scanner_iterator.h"
#include "lib/rc/context.h"

namespace oceanbase
{
namespace observer
{
class ObAllVirtualMemoryContextStat : public common::ObVirtualTableScannerIterator
{
public:
  ObAllVirtualMemoryContextStat();
  virtual ~ObAllVirtualMemoryContextStat();
  virtual int inner_get_next_row(common::ObNewRow *&row);
  virtual void reset();
private:
  char ip_buf_[common::OB_IP_STR_BUFF];
  char entity_buf_[32];
  char p_entity_buf_[32];
  char location_buf_[512];
  bool is_inited_;
  DISALLOW_COPY_AND_ASSIGN(ObAllVirtualMemoryContextStat);
};

} /* namespace observer */
} /* namespace oceanbase */
#endif /* OB_ALL_VIRTUAL_MEMORY_CONTEXT_STAT_H_ */
