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

#ifndef OBDEV_SRC_SQL_DAS_ITER_OB_DAS_SCAN_ITER_H_
#define OBDEV_SRC_SQL_DAS_ITER_OB_DAS_SCAN_ITER_H_

#include "sql/das/iter/ob_das_iter.h"
#include "sql/das/ob_das_scan_op.h"
namespace oceanbase
{
namespace common {
class ObITabletScan;
}

namespace storage {
class ObTableScanParam;
}

namespace sql
{

class ObDASScanCtDef;

// DASScanIter is a wrapper class for storage iter, it doesn't require eval_ctx or exprs like other iters.
struct ObDASScanIterParam : public ObDASIterParam
{
public:
  ObDASScanIterParam()
    : ObDASIterParam(ObDASIterType::DAS_ITER_SCAN),
      scan_ctdef_(nullptr)
  {}
  const ObDASScanCtDef *scan_ctdef_;
  virtual bool is_valid() const override
  {
    return nullptr != scan_ctdef_ && ObDASIterParam::is_valid();
  }
};

class ObDASScanIter : public ObDASIter
{
public:
  ObDASScanIter()
    : ObDASIter(ObDASIterType::DAS_ITER_SCAN),
      tsc_service_(nullptr),
      result_(nullptr),
      scan_param_(nullptr)
  {}
  virtual ~ObDASScanIter() {}
  common::ObNewRowIterator *&get_output_result_iter() { return result_; }

  void set_scan_param(storage::ObTableScanParam &scan_param) { scan_param_ = &scan_param; }
  storage::ObTableScanParam &get_scan_param() { return *scan_param_; }

  virtual int do_table_scan() override;
  virtual int rescan() override;
  virtual void clear_evaluated_flag() override;

  virtual int get_diagnosis_info(ObDiagnosisManager* diagnosis_manager) override {
    return result_->get_diagnosis_info(diagnosis_manager);
  };
  virtual int set_scan_rowkey(ObEvalCtx *eval_ctx,
                              const ObIArray<ObExpr *> &rowkey_exprs,
                              const ObDASScanCtDef *lookup_ctdef,
                              ObIAllocator *alloc,
                              int64_t group_id) override;
protected:
  virtual int inner_init(ObDASIterParam &param) override;
  virtual int inner_reuse() override;
  virtual int inner_release() override;
  virtual int inner_get_next_row() override;
  virtual int inner_get_next_rows(int64_t &count, int64_t capacity) override;

private:
  common::ObITabletScan *tsc_service_;
  common::ObNewRowIterator *result_;
  // must ensure the lifecycle of scan param is longer than scan iter.
  storage::ObTableScanParam *scan_param_;
};


}  // namespace sql
}  // namespace oceanbase



#endif /* OBDEV_SRC_SQL_DAS_ITER_OB_DAS_SCAN_ITER_H_ */
