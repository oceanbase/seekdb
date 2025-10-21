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

#include "share/ob_errno.h"
#include "lib/oblog/ob_log_module.h"
#include "storage/lob/ob_lob_util.h"
#include "storage/blocksstable/ob_datum_row.h"
#include "storage/blocksstable/ob_macro_block_bare_iterator.h"

namespace oceanbase
{
namespace storage
{
class ObDDLRedoLogRowIterator
{
public:
  ObDDLRedoLogRowIterator(common::ObIAllocator &allocator, const uint64_t tenant_id);
  ~ObDDLRedoLogRowIterator();
  void reset();
private:
  common::ObIAllocator &allocator_;
  blocksstable::ObMacroBlockRowBareIterator iter_;
  common::ObStoreRowkey rowkey_;
  common::ObObj *rowkey_obobj_;
  int64_t schema_rowkey_column_count_;
  bool is_inited_;
};

} // namespace storage
} // namespace oceanbase
