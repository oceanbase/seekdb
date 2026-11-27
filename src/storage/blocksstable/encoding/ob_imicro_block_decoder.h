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

#ifndef OB_STORAGE_BLOCKSSTABLE_ENCODING_OB_IMICRO_BLOCK_DECODER_H_
#define OB_STORAGE_BLOCKSSTABLE_ENCODING_OB_IMICRO_BLOCK_DECODER_H_

#include "lib/container/ob_bitmap.h"
#include "query/engine/basic/ob_pushdown_filter.h"
#include "storage/blocksstable/ob_imicro_block_reader.h"
#include "storage/blocksstable/ob_row_reader.h"
#include "storage/ob_i_store.h"

namespace oceanbase
{
namespace storage
{
class ObTruncateFilterEvaluator;
}
namespace blocksstable
{

class ObIMicroBlockDecoder : public ObIMicroBlockReader
{
public:
  ObIMicroBlockDecoder() : ObIMicroBlockReader() {}
  virtual ~ObIMicroBlockDecoder() {}
  virtual int compare_rowkey(
    const ObDatumRowkey &rowkey, const int64_t index, int32_t &compare_result) override = 0;
  virtual int filter_truncate_evaluator(
      storage::ObTruncateFilterEvaluator &evaluator,
      const int64_t start,
      const int64_t count,
      const common::ObBitmap *candidate_rows,
      common::ObBitmap &result_bitmap) = 0;
  virtual int get_rows(const common::ObIArray<int32_t> &cols,
    const common::ObIArray<const share::schema::ObColumnParam *> &col_params, const bool is_padding_mode,
    const int32_t *row_ids, const char **cell_datas, const int64_t row_cap,
    common::ObIArray<ObSqlDatumInfo> &datum_infos, const int64_t datum_offset = 0) = 0;

};

}  // namespace blocksstable
}  // namespace oceanbase

#endif
