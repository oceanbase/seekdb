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

#ifndef OB_STORAGE_OB_SSTABLE_ROW_GETTER_H_
#define OB_STORAGE_OB_SSTABLE_ROW_GETTER_H_

#include "storage/blocksstable/ob_micro_block_row_getter.h"
#include "ob_index_tree_prefetcher.h"

namespace oceanbase {
namespace storage {
class ObSSTableRowGetter : public ObStoreRowIterator
{
public:
  ObSSTableRowGetter() :
      ObStoreRowIterator(),
      is_opened_(false),
      has_fetched_(false),
      sstable_(nullptr),
      micro_getter_(nullptr),
      iter_param_(nullptr),
      access_ctx_(nullptr),
      prefetcher_(),
      macro_block_reader_(nullptr),
      read_handle_()
  {
    type_ = ObStoreRowIterator::IteratorSingleGet;
  }
  virtual ~ObSSTableRowGetter();
  virtual void reset() override;
  virtual void reuse() override;
  virtual void reclaim() override;
  TO_STRING_KV(K_(is_opened), K_(has_fetched), K_(prefetcher), KP_(macro_block_reader));
protected:
  int inner_open(
      const ObTableIterParam &iter_param,
      ObTableAccessContext &access_ctx,
      ObITable *table,
      const void *query_range);
  virtual int inner_get_next_row(const blocksstable::ObDatumRow *&store_row) override;
  int fetch_row(ObSSTableReadHandle &read_handle, const blocksstable::ObDatumRow *&store_row);

protected:
  bool is_opened_;
  bool has_fetched_;
  ObSSTable *sstable_;
  blocksstable::ObMicroBlockRowGetter *micro_getter_;
  const ObTableIterParam *iter_param_;
  ObTableAccessContext *access_ctx_;
  ObIndexTreePrefetcher prefetcher_;
  ObMacroBlockReader *macro_block_reader_;
private:
  ObSSTableReadHandle read_handle_;
};

}
}
#endif //OB_STORAGE_OB_SSTABLE_ROW_GETTER_V2_H_
