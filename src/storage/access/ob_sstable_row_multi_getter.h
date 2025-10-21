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

#ifndef OB_STORAGE_OB_SSTABLE_ROW_MULTI_GETTER_H_
#define OB_STORAGE_OB_SSTABLE_ROW_MULTI_GETTER_H_

#include "storage/blocksstable/ob_micro_block_row_getter.h"
#include "ob_index_tree_prefetcher.h"

namespace oceanbase {
namespace storage {
class ObSSTableRowMultiGetter : public ObStoreRowIterator
{
public:
  ObSSTableRowMultiGetter() :
      ObStoreRowIterator(),
      sstable_(nullptr),
      iter_param_(nullptr),
      access_ctx_(nullptr),
      prefetcher_(),
      macro_block_reader_(nullptr),
      is_opened_(false),
      micro_getter_(nullptr)
  {
    type_ = ObStoreRowIterator::IteratorMultiGet;
  }

  virtual ~ObSSTableRowMultiGetter();
  virtual void reset() override;
  virtual void reuse() override;
  virtual void reclaim() override;
  TO_STRING_KV(K_(is_opened), K_(prefetcher), KP_(macro_block_reader));
protected:
  int inner_open(
      const ObTableIterParam &access_param,
      ObTableAccessContext &access_ctx,
      ObITable *table,
      const void *query_range) final;
  virtual int inner_get_next_row(const blocksstable::ObDatumRow *&store_row);
  virtual int fetch_row(ObSSTableReadHandle &read_handle, const blocksstable::ObDatumRow *&store_row);

protected:
  ObSSTable *sstable_;
  const ObTableIterParam *iter_param_;
  ObTableAccessContext *access_ctx_;
  ObIndexTreeMultiPrefetcher prefetcher_;
  ObMacroBlockReader *macro_block_reader_;
private:
  bool is_opened_;
  blocksstable::ObMicroBlockRowGetter *micro_getter_;
};

}
}
#endif //OB_STORAGE_OB_SSTABLE_ROW_MULTI_GETTER_H_
