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

#ifndef OBDEV_SRC_SQL_DAS_ITER_OB_DAS_CACHE_LOOKUP_ITER_H_
#define OBDEV_SRC_SQL_DAS_ITER_OB_DAS_CACHE_LOOKUP_ITER_H_

#include "sql/das/iter/ob_das_local_lookup_iter.h"
#include "src/sql/engine/basic/ob_chunk_datum_store.h"

namespace oceanbase
{
using namespace common;
namespace sql
{

struct ObDASCacheLookupIterParam : public ObDASLocalLookupIterParam
{
public:
  ObDASCacheLookupIterParam()
    : ObDASLocalLookupIterParam(),
      index_scan_proj_exprs_()
  {}
  virtual bool is_valid() const override
  {
    return true;
  }

  common::ObArray<ObExpr*> index_scan_proj_exprs_;
};

class ObDASScanCtDef;
class ObDASScanRtDef;
class ObDASFuncLookupIter;
class ObDASCacheLookupIter : public ObDASLocalLookupIter
{
public:

  struct IndexProjRowStore
  {
  public:
    IndexProjRowStore()
      : eval_ctx_(nullptr),
        max_size_(1),
        saved_size_(0),
        cur_idx_(OB_INVALID_INDEX),
        store_rows_(nullptr),
        index_scan_proj_exprs_(),
        iter_end_(false)
    {}

    int init(common::ObIAllocator &allocator,
             const common::ObIArray<ObExpr*> &exprs,
             ObEvalCtx *eval_ctx,
             int64_t max_size);
    void reuse();
    void reset();
    int save(bool is_vectorized, int64_t size);
    int to_expr(int64_t size);
    inline bool have_data() const { return cur_idx_ != OB_INVALID_INDEX && cur_idx_ < saved_size_; }

    TO_STRING_KV(K_(saved_size),
                 K_(cur_idx),
                 K_(iter_end),
                 K_(index_scan_proj_exprs));

  public:
    ObEvalCtx *eval_ctx_;
    int64_t max_size_;
    int64_t saved_size_;
    int64_t cur_idx_;
    ObChunkDatumStore::LastStoredRow *store_rows_;
    ExprFixedArray index_scan_proj_exprs_;
    bool iter_end_;
  };

public:
  ObDASCacheLookupIter(const ObDASIterType type = ObDASIterType::DAS_ITER_LOCAL_LOOKUP)
    : ObDASLocalLookupIter(type),
      store_allocator_("SqlCacheLookup", OB_MALLOC_NORMAL_BLOCK_SIZE, MTL_ID(), ObCtxIds::DEFAULT_CTX_ID),
      index_proj_rows_()
  {}
  virtual ~ObDASCacheLookupIter() {}

protected:

  virtual int inner_init(ObDASIterParam &param) override;
  virtual int inner_reuse() override;
  virtual int inner_release() override;
  virtual int inner_get_next_row() override;
  virtual int inner_get_next_rows(int64_t &count, int64_t capacity) override;
  virtual void reset_lookup_state() override;

private:
  // for store rows
  common::ObArenaAllocator store_allocator_;
  IndexProjRowStore index_proj_rows_;
};

}  // namespace sql
}  // namespace oceanbase


#endif /* OBDEV_SRC_SQL_DAS_ITER_OB_DAS_LOOKUP_ITER_H_ */
