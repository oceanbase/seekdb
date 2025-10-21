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

#ifndef OCEANBASE_STORAGE_OB_MULTIPLE_GET_MERGE_
#define OCEANBASE_STORAGE_OB_MULTIPLE_GET_MERGE_

#include "ob_multiple_merge.h"
#include "ob_fuse_row_cache_fetcher.h"
#include "storage/blocksstable/ob_fuse_row_cache.h"
#include "storage/ob_storage_struct.h"

namespace oceanbase
{
namespace storage
{

enum class ObMultiGetRowState
{
  INVALID = 0,
  IN_FUSE_ROW_CACHE,
  IN_FUSE_ROW_CACHE_AND_SSTABLE,
  IN_MEMTABLE,
  IN_SSTABLE
};

struct ObQueryRowInfo final
{
public:
  ObQueryRowInfo()
    : row_(), nop_pos_(), final_result_(false), state_(ObMultiGetRowState::INVALID),
    end_iter_idx_(0), sstable_end_log_ts_(0)
  {}
  ~ObQueryRowInfo() = default;
  TO_STRING_KV(K_(row), K_(final_result), K_(final_result), K_(end_iter_idx), K_(sstable_end_log_ts));
  blocksstable::ObDatumRow row_;
  ObNopPos nop_pos_;
  bool final_result_;
  ObMultiGetRowState state_;
  int64_t end_iter_idx_;
  int64_t sstable_end_log_ts_;
};

class ObMultipleGetMerge : public ObMultipleMerge
{
public:
  ObMultipleGetMerge();
  virtual ~ObMultipleGetMerge();
  int open(const common::ObIArray<blocksstable::ObDatumRowkey> &rowkeys);
  virtual void reset() override;
  virtual void reuse() override;
  virtual void reclaim() override;
  virtual int is_range_valid() const override;
  virtual int pause(bool& do_pause) override final { do_pause = false; return OB_SUCCESS; }
protected:
  virtual int prepare() override;
  virtual int calc_scan_range() override;
  virtual int construct_iters() override;
  virtual int inner_get_next_row(blocksstable::ObDatumRow &row);
  virtual int get_range_count() const override
  { return rowkeys_->count(); }
private:
  const common::ObIArray<blocksstable::ObDatumRowkey> *rowkeys_;
  common::ObSEArray<blocksstable::ObDatumRowkey, common::OB_DEFAULT_MULTI_GET_ROWKEY_NUM> cow_rowkeys_;
  int64_t get_row_range_idx_;

  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObMultipleGetMerge);
};

}
}

#endif // OCEANBASE_STORAGE_OB_MULTIPLE_GET_MERGE_
