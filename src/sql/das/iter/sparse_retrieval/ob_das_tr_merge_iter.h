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

#ifndef OB_DAS_TR_MERGE_ITER_H_
#define OB_DAS_TR_MERGE_ITER_H_

#include "sql/das/iter/ob_das_iter.h"
#include "sql/das/ob_das_ir_define.h"

namespace oceanbase
{
namespace sql
{
struct ObDASIRScanCtDef;
struct ObDASIRScanRtDef;
class ObDASTextRetrievalIterator;
class ObFtsEvalNode;
class ObDASTextRetrievalIter;
class ObDocIdExt;
// Query-owned facade; the concrete retrieval engine is installed by Observer.
class ObIDASTextRetrievalEngine;

struct ObDASTRMergeIterParam : public ObDASIterParam
{
  ObDASTRMergeIterParam()
    : ObDASIterParam(DAS_ITER_TEXT_RETRIEVAL_MERGE),
      ir_ctdef_(nullptr),
      ir_rtdef_(nullptr),
      tx_desc_(nullptr),
      snapshot_(nullptr),
      query_tokens_(),
      dim_weights_(),
      max_batch_size_(0),
      boolean_compute_node_(nullptr),
      flags_(0)
  {}

  virtual bool is_valid() const override
  {
    return nullptr != ir_ctdef_ && nullptr != ir_rtdef_;
  }

  const ObDASIRScanCtDef *ir_ctdef_;
  ObDASIRScanRtDef *ir_rtdef_;
  transaction::ObTxDesc *tx_desc_;
  transaction::ObTxReadSnapshot *snapshot_;
  ObArray<ObString> query_tokens_;
  ObArray<double> dim_weights_;
  int64_t max_batch_size_;
  ObFtsEvalNode *boolean_compute_node_;
  union {
    struct {
      uint32_t function_lookup_mode_  : 1;
      uint32_t topk_mode_             : 1;
      uint32_t daat_mode_             : 1;
      uint32_t taat_mode_             : 1;
      uint32_t reserve                : 28;
    };
    uint32_t flags_;
  };
};

class ObDASTRMergeIter : public ObDASIter
{
public:
  ObDASTRMergeIter();
  virtual ~ObDASTRMergeIter() {}
  virtual int do_table_scan() override;
  virtual int rescan() override;

  INHERIT_TO_STRING_KV(
      "ObDASIter", ObDASIter, KP_(engine), K_(is_inited));
protected:
  virtual int inner_init(ObDASIterParam &param) override;
  virtual int inner_reuse() override;
  virtual int inner_release() override;
  virtual int inner_get_next_row() override;
  virtual int inner_get_next_rows(int64_t &count, int64_t capacity) override;
public:
  int set_related_tablet_ids(
      const ObDASFTSTabletID &related_tablet_ids);
  static int build_query_tokens(
      const ObDASIRScanCtDef *ir_ctdef,
      ObDASIRScanRtDef *ir_rtdef,
      common::ObIAllocator &alloc,
      ObArray<ObString> &query_tokens,
      ObArray<double> &boost_values,
      ObFtsEvalNode *&root_node,
      bool &has_duplicate_boolean_tokens);
  int set_children_iter_rangekey(const common::ObIArray<std::pair<ObDocIdExt, int>> &virtual_rangekeys, const int64_t batch_size);
  bool is_taat_mode();
  int get_query_max_score(double &score);

private:
  common::ObArenaAllocator engine_allocator_;
  ObIDASTextRetrievalEngine *engine_;
  bool is_inited_;
  DISALLOW_COPY_AND_ASSIGN(ObDASTRMergeIter);
};

} // namespace sql
} // namespace oceanbase

#endif // OB_DAS_TR_MERGE_ITER_H_
