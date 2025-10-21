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

#include "storage/blocksstable/ob_batch_datum_rows.h"
#include "storage/direct_load/ob_direct_load_batch_rows.h"

namespace oceanbase
{
namespace storage
{
class ObDirectLoadBatchDatumRows
{
public:
  ObDirectLoadBatchDatumRows() : allocator_("TLD_BDatumRows")
  {
    allocator_.set_tenant_id(MTL_ID());
  }
  ~ObDirectLoadBatchDatumRows() {}
public:
  ObArenaAllocator allocator_;
  ObDirectLoadBatchRows batch_rows_;
  blocksstable::ObBatchDatumRows datum_rows_;
};

} // namespace storage
} // namespace oceanbase
