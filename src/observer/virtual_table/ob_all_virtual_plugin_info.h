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

#pragma once

#include "share/ob_scanner.h"
#include "share/ob_virtual_table_scanner_iterator.h"
#include "common/row/ob_row.h"
#include "lib/container/ob_se_array.h"

namespace oceanbase {
namespace plugin {
class ObPluginEntryHandle;
}
namespace observer {

class ObAllVirtualPluginInfo final : public common::ObVirtualTableScannerIterator
{
public:
  ObAllVirtualPluginInfo();
  virtual ~ObAllVirtualPluginInfo();

public:
  virtual int inner_get_next_row(common::ObNewRow *&row) override;
  virtual void reset() override;
  virtual int inner_open() override;
  virtual int inner_close() override;
  inline void set_addr(common::ObAddr &addr)
  {
    addr_ = addr;
  }

private:
  common::ObAddr addr_;
  char ip_buf_[common::OB_IP_STR_BUFF];

  ObArray<plugin::ObPluginEntryHandle *> plugin_entries_;

  int64_t iter_index_ = -1;

  TO_STRING_KV(K(addr_));

private:
  DISALLOW_COPY_AND_ASSIGN(ObAllVirtualPluginInfo);
};

} // namespace observer
} // namespace oceanbase
