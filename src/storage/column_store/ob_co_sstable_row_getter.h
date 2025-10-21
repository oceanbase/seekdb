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
#ifndef OB_STORAGE_COLUMN_STORE_COSSTABLE_ROW_GETTER_H_
#define OB_STORAGE_COLUMN_STORE_COSSTABLE_ROW_GETTER_H_
#include "ob_cg_sstable_row_getter.h"

namespace oceanbase
{
namespace storage
{
// row cache not supported in ObCOSSTableRowGetter because it is not efficient
class ObCOSSTableRowGetter : public ObCGSSTableRowGetter
{
public:
  ObCOSSTableRowGetter() :
      ObCGSSTableRowGetter(),
      has_fetched_(false),
      nop_pos_(nullptr),
      read_handle_(),
      prefetcher_()
  {
    type_ = ObStoreRowIterator::IteratorCOSingleGet;
  }
  virtual ~ObCOSSTableRowGetter()
  {}
  virtual void reset() override final;
  virtual void reuse() override final;
  void set_nop_pos(const ObNopPos *nop_pos)
  { nop_pos_ = nop_pos; }
  INHERIT_TO_STRING_KV("ObCOSSTableRowGetter", ObCGSSTableRowGetter, K_(prefetcher));
protected:
  virtual int inner_open(
      const ObTableIterParam &iter_param,
      ObTableAccessContext &access_ctx,
      ObITable *table,
      const void *query_range) final;
  virtual int inner_get_next_row(const blocksstable::ObDatumRow *&store_row) final;

private:
  bool has_fetched_;
  // nop_pos is used for opt in single merge, only get columns without update
  const ObNopPos *nop_pos_;
  ObSSTableReadHandle read_handle_;
  ObIndexTreePrefetcher prefetcher_;
};

}
}
#endif
