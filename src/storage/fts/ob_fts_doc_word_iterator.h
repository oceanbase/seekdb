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

#ifndef OCEANBASE_STORAGE_FTS_DOC_WORD_ITERATOR_H
#define OCEANBASE_STORAGE_FTS_DOC_WORD_ITERATOR_H

#include "lib/allocator/page_arena.h"
#include "share/datum/ob_datum.h"
#include "share/ob_fts_index_builder_util.h"
#include "share/schema/ob_table_param.h"
#include "sql/das/ob_das_dml_ctx_define.h"
#include "storage/access/ob_dml_param.h"

namespace oceanbase
{
namespace storage
{

class ObFTDocWordScanIterator final
{
public:
  ObFTDocWordScanIterator();
  ~ObFTDocWordScanIterator();

  int init(
      const uint64_t table_id,
      const share::ObLSID &ls_id,
      const common::ObTabletID &tablet_id,
      const transaction::ObTxReadSnapshot *snapshot,
      const int64_t schema_version);
  int do_scan(const uint64_t table_id, const ObDatum &row_mapping_id);
  int get_next_row(blocksstable::ObDatumRow *&datum_row);

  void reset();

  TO_STRING_KV(KP(doc_word_iter_), K(scan_param_), K(table_param_));
private:
  int init_scan_param(
      const uint64_t table_id,
      const share::ObLSID &ls_id,
      const common::ObTabletID &tablet_id,
      const transaction::ObTxReadSnapshot *snapshot,
      const int64_t schema_version);
  int build_table_param(
      const uint64_t table_id,
      share::schema::ObTableParam &table_param,
      common::ObIArray<uint64_t> &column_ids);
  int build_key_range(const uint64_t table_id,
                      const ObDatum &row_mapping_id,
                      common::ObIArray<ObNewRange> &rowkey_range);
  int do_table_scan();
  int do_table_rescan();
  int reuse();

private:
  common::ObArenaAllocator allocator_;
  common::ObArenaAllocator scan_allocator_;
  share::schema::ObTableParam table_param_;
  storage::ObTableScanParam scan_param_;
  common::ObNewRowIterator *doc_word_iter_;
  ObDocIDType docid_type_;
  bool is_inited_;

  DISALLOW_COPY_AND_ASSIGN(ObFTDocWordScanIterator);
};
} // end namespace storage
} // end namespace oceanbase

#endif // OCEANBASE_STORAGE_FTS_DOC_WORD_ITERATOR_H
