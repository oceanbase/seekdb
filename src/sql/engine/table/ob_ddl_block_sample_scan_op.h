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

#ifndef _OB_TABLE_DDL_BLOCK_SAMPLE_SCAN_OP_H
#define _OB_TABLE_DDL_BLOCK_SAMPLE_SCAN_OP_H

#include "sql/engine/table/ob_table_scan_op.h"

namespace oceanbase {
namespace sql {

class ObDDLBlockSampleScanOpInput : public ObTableScanOpInput
{
public:
  ObDDLBlockSampleScanOpInput(ObExecContext &ctx, const ObOpSpec &spec)
    : ObTableScanOpInput(ctx, spec)
  {}
};

class ObDDLBlockSampleScanSpec: public ObTableScanSpec
{
  OB_UNIS_VERSION_V(1);
public:
  explicit ObDDLBlockSampleScanSpec(common::ObIAllocator &alloc, const ObPhyOperatorType type)
    : ObTableScanSpec(alloc, type)
  {}
  virtual ~ObDDLBlockSampleScanSpec() {}

  inline void set_sample_info(const common::SampleInfo &sample_info) {
    sample_info_ = sample_info;
  }
  inline common::SampleInfo &get_sample_info() {
    return sample_info_;
  }
  inline const common::SampleInfo &get_sample_info() const {
    return sample_info_;
  }
private:
  common::SampleInfo sample_info_;
};

class ObDDLBlockSampleScanOp : public ObTableScanOp
{
public:
  ObDDLBlockSampleScanOp(ObExecContext &exec_ctx, const ObOpSpec &spec, ObOpInput *input)
    : ObTableScanOp(exec_ctx, spec, input), need_sample_(false) {}
  virtual int inner_open() override;
  virtual int inner_get_next_row() override;
  virtual int inner_get_next_batch(const int64_t max_row_cnt) override;
  virtual void set_need_sample(bool flag) override
  {
    need_sample_ = flag;
    tsc_rtdef_.scan_rtdef_.sample_info_ = need_sample_ ? &(MY_SPEC.get_sample_info()) : nullptr;
  }
private:
  bool need_sample_;
};

}
}

#endif /* _OB_TABLE_DDL_BLOCK_SAMPLE_SCAN_OP_H */
