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

#include "storage/ddl/ob_ddl_struct.h"

namespace oceanbase
{
namespace storage
{
class ObDirectLoadInsertTabletContext;
class ObLobMacroBlockWriter;

class ObDirectLoadDagLobBuilder
{
public:
  ObDirectLoadDagLobBuilder();
  ~ObDirectLoadDagLobBuilder();
  int init(ObDirectLoadInsertTabletContext *insert_tablet_ctx);
  int switch_slice(const int64_t slice_idx);
  int append_lob(blocksstable::ObDatumRow &datum_row);
  int append_lob(blocksstable::ObBatchDatumRows &datum_rows);
  int close();

private:
  ObDirectLoadInsertTabletContext *insert_tablet_ctx_;
  int64_t slice_idx_;
  ObWriteMacroParam write_param_;
  ObLobMacroBlockWriter *lob_writer_;
  ObArenaAllocator lob_allocator_;
  bool is_inited_;
};

} // namespace storage
} // namespace oceanbase
