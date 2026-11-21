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

#ifndef OCEANBASE_ENCODING_OB_NEW_COLUMN_DECODER_H_
#define OCEANBASE_ENCODING_OB_NEW_COLUMN_DECODER_H_

#include "storage/blocksstable/ob_block_sstable_struct.h"
#include "storage/blocksstable/ob_datum_row.h"

namespace oceanbase
{
namespace blocksstable
{

class ObNewColumnCommonDecoder final
{
public:
  int decode(const ObColumnParam *col_param, common::ObIAllocator *allocator, common::ObDatum &datum) const;

  int batch_decode(
      const ObColumnParam *col_param,
      common::ObIAllocator *allocator, 
      const int64_t row_cap,
      common::ObDatum *datums) const;


  int get_null_count(
      const ObColumnParam *col_param,
      const int64_t row_cap,
      int64_t &null_count) const;

};

} // end namespace blocksstable
} // end namespace oceanbase

#endif // OCEANBASE_ENCODING_OB_NEW_COLUMN_DECODER_H_
