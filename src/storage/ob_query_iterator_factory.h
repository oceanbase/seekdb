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

#ifndef OCEANBASE_STORAGE_OB_QUERY_ITERATOR_FACTORY_
#define OCEANBASE_STORAGE_OB_QUERY_ITERATOR_FACTORY_

#include <stdint.h>
#include "common/row/ob_row_iterator.h"

namespace oceanbase
{
namespace blocksstable
{
class ObDatumRowIterator;
}
namespace storage
{
class ObMultipleScanMerge;
class ObMultipleGetMerge;
class ObTableScanIterator;
class ObQueryRowIterator;
class ObValueRowIterator;
class ObColMap;
class ObStoreRow;
class ObQueryIteratorFactory
{
public:
  static ObValueRowIterator *get_insert_dup_iter();

  static void free_insert_dup_iter(blocksstable::ObDatumRowIterator *iter);
  static void free_work_row(ObStoreRow *row);
private:
  static void print_count();
private:
  static int64_t single_row_merge_alloc_count_;
  static int64_t single_row_merge_release_count_;
  static int64_t multi_scan_merge_alloc_count_;
  static int64_t multi_scan_merge_release_count_;
  static int64_t multi_get_merge_alloc_count_;
  static int64_t multi_get_merge_release_count_;
  static int64_t table_scan_alloc_count_;
  static int64_t table_scan_release_count_;
  static int64_t insert_dup_alloc_count_;
  static int64_t insert_dup_release_count_;
  static int64_t col_map_alloc_count_;
  static int64_t col_map_release_count_;
  static int64_t work_row_alloc_count_;
  static int64_t work_row_release_count_;
};

} // namespace storage
} // namespace oceanbase
#endif // OCEANBASE_STORAGE_OB_QUERY_ITERATOR_FACTORY_
