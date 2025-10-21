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

#ifndef DEV_SRC_STORAGE_BLOCKSSTABLE_OB_DATUM_ROW_UTILS_H_
#define DEV_SRC_STORAGE_BLOCKSSTABLE_OB_DATUM_ROW_UTILS_H_
#include "share/ob_define.h"
#include "ob_datum_row.h"
namespace oceanbase
{
namespace blocksstable
{
class ObDatumRowUtils
{
  typedef common::ObIArray<share::schema::ObColDesc> ObColDescIArray;
public:
  static int ob_create_row(ObIAllocator &allocator, int64_t col_count, ObDatumRow *&datum_row);
  static int ob_create_rows(ObIAllocator &allocator, int64_t row_count, int64_t col_count, ObDatumRow *&datum_rows);
  static int ob_create_rows_shallow_copy(ObIAllocator &allocator,
                                         const ObDatumRow *src_rows,
                                         const ObIArray<int64_t> &dst_row_ids,
                                         ObDatumRow *&dst_rows);
  // TODO@xuanxi: rewrite it when store rowkey is no longer needed
  static int prepare_rowkey(
    const ObDatumRow &datum_row,
    const int key_datum_cnt, 
    const ObColDescIArray &col_descs,
    common::ObIAllocator &allocator,
    ObDatumRowkey &rowkey);
};

}  // namespace sql
}  // namespace oceanbase
#endif /* DEV_SRC_STORAGE_BLOCKSSTABLE_OB_DATUM_ROW_UTILS_H_ */
