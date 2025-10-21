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

#ifndef OB_TEXT_DAAT_ITER_H_
#define OB_TEXT_DAAT_ITER_H_

#include "ob_sparse_bmw_iter.h"
#include "ob_sparse_daat_iter.h"
#include "ob_text_retrieval_token_iter.h"

namespace oceanbase
{
namespace sql
{
  class ObDASScanIter;
}
namespace storage
{

struct ObTextDaaTParam
{
  ObTextDaaTParam()
    : dim_iters_(nullptr), base_param_(nullptr), allocator_(nullptr), relevance_collector_(nullptr),
      total_doc_cnt_scan_param_(nullptr), estimated_total_doc_cnt_(0),
      total_doc_cnt_iter_(nullptr), total_doc_cnt_expr_(nullptr),
      mode_flag_(ObMatchAgainstMode::NATURAL_LANGUAGE_MODE), function_lookup_mode_(false) {}
  ~ObTextDaaTParam() {}
  TO_STRING_KV(K_(base_param), KP_(dim_iters), KP_(total_doc_cnt_scan_param), K_(estimated_total_doc_cnt),
      KPC_(total_doc_cnt_iter), KPC_(total_doc_cnt_expr), K_(mode_flag), K_(function_lookup_mode));
  ObIArray<ObISRDaaTDimIter *> *dim_iters_;
  ObSparseRetrievalMergeParam *base_param_;
  common::ObArenaAllocator *allocator_;
  ObSRDaaTRelevanceCollector *relevance_collector_;
  ObTableScanParam *total_doc_cnt_scan_param_;
  int64_t estimated_total_doc_cnt_;
  sql::ObDASScanIter *total_doc_cnt_iter_;
  sql::ObExpr *total_doc_cnt_expr_;
  ObMatchAgainstMode mode_flag_;
  bool function_lookup_mode_;
};

class ObTextDaaTIter final : public ObSRDaaTIterImpl
{
public:
  ObTextDaaTIter() : ObSRDaaTIterImpl(),
      estimated_total_doc_cnt_(0), total_doc_cnt_scan_param_(nullptr),
      total_doc_cnt_iter_(nullptr), total_doc_cnt_expr_(nullptr),
      mode_flag_(ObMatchAgainstMode::NATURAL_LANGUAGE_MODE),
      total_doc_cnt_calculated_(false), function_lookup_mode_(false) {}
  virtual ~ObTextDaaTIter() { reset(); }

  int init(const ObTextDaaTParam &param);
  virtual void reuse() override;
  virtual void reset() override;
protected:
  virtual int pre_process() override;
  int do_total_doc_cnt();
protected:
  int64_t estimated_total_doc_cnt_;
  ObTableScanParam *total_doc_cnt_scan_param_;
  sql::ObDASScanIter *total_doc_cnt_iter_;
  sql::ObExpr *total_doc_cnt_expr_;
  ObMatchAgainstMode mode_flag_;
  bool total_doc_cnt_calculated_;
  bool function_lookup_mode_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObTextDaaTIter);
};


class ObTextBMWIter final : public ObSRBMWIterImpl
{
public:
  ObTextBMWIter();
  virtual ~ObTextBMWIter() { reset(); }
  virtual void reuse() override;
  virtual void reset() override;
  int init(const ObTextDaaTParam &param);
protected:
  // TODO: eliminate repeated logic with naive daat implementation maybe
  virtual int get_next_rows(const int64_t capacity, int64_t &count) override;
  virtual int init_before_wand_process() override;
  virtual int do_total_doc_cnt();
protected:
  int64_t estimated_total_doc_cnt_;
  ObTableScanParam *total_doc_cnt_scan_param_;
  sql::ObDASScanIter *whole_doc_cnt_iter_;
  sql::ObExpr *total_doc_cnt_expr_;
  bool total_doc_cnt_calculated_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObTextBMWIter);
};

} // namespace storage
} // namespace oceanbase

#endif
