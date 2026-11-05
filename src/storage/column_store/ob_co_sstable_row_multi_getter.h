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
#ifndef OB_STORAGE_COLUMN_STORE_COSSTABLE_ROW_MULTI_GETTER_H_
#define OB_STORAGE_COLUMN_STORE_COSSTABLE_ROW_MULTI_GETTER_H_
#include "ob_cg_sstable_row_getter.h"

namespace oceanbase
{
namespace storage
{
class ObCOSSTableRowMultiGetter : public ObCGSSTableRowGetter
{
public:
  ObCOSSTableRowMultiGetter() :
      ObCGSSTableRowGetter(),
      prefetcher_()
  {
    type_ = ObStoreRowIterator::IteratorCOMultiGet;
  }
  virtual ~ObCOSSTableRowMultiGetter()
  {}
  void reset() override final;
  void reuse() override final;
  INHERIT_TO_STRING_KV("ObCOSSTableRowMultiGetter", ObCGSSTableRowGetter, K_(prefetcher));
protected:
  virtual int inner_open(
      const ObTableIterParam &iter_param,
      ObTableAccessContext &access_ctx,
      ObITable *table,
      const void *query_range) final;
  virtual int inner_get_next_row(const blocksstable::ObDatumRow *&store_row) final;

protected:
  ObIndexTreeMultiPrefetcher prefetcher_;
};

}
}
#endif
