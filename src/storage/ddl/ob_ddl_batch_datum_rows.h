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
#include "storage/ddl/ob_ddl_batch_rows.h"

namespace oceanbase
{
namespace storage
{
class ObDDLBatchDatumRows
{
public:
  ObDDLBatchDatumRows() : allocator_("DDL_BDatumRows")
  {

  }
  ~ObDDLBatchDatumRows() {}
public:
  ObArenaAllocator allocator_;
  ObDDLBatchRows batch_rows_;
  blocksstable::ObBatchDatumRows datum_rows_;
};

} // namespace storage
} // namespace oceanbase
