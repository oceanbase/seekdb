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

#include "ob_all_virtual_memory_context_stat.h"

namespace oceanbase
{
using namespace storage;
using namespace lib;
namespace observer
{
ObAllVirtualMemoryContextStat::ObAllVirtualMemoryContextStat()
{
}

ObAllVirtualMemoryContextStat::~ObAllVirtualMemoryContextStat()
{
  reset();
}

int ObAllVirtualMemoryContextStat::inner_get_next_row(common::ObNewRow *&row)
{
  UNUSEDx(row);
  return OB_ITER_END;
}

void ObAllVirtualMemoryContextStat::reset()
{
  is_inited_ = false;
  ObVirtualTableScannerIterator::reset();
}

} /* namespace observer */
} /* namespace oceanbase */
