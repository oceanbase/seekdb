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

#pragma once

#include "observer/table_load/ob_table_load_merge_op.h"
#include "observer/table_load/ob_table_load_struct.h"
#include "storage/direct_load/ob_direct_load_trans_param.h"

namespace oceanbase
{
namespace observer
{
class ObTableLoadMergePhaseCtx
{
public:
  ObTableLoadMergePhaseCtx();
  TO_STRING_KV(K_(phase), K_(trans_param));

public:
  ObTableLoadMergerPhaseType phase_;
  storage::ObDirectLoadTransParam trans_param_;
};

class ObTableLoadMergePhaseBaseOp : public ObTableLoadMergeOp
{
public:
  ObTableLoadMergePhaseBaseOp(ObTableLoadMergeOp *parent);
  ObTableLoadMergePhaseBaseOp(ObTableLoadMergePhaseBaseOp *parent);
  virtual ~ObTableLoadMergePhaseBaseOp() = default;

public:
  ObTableLoadMergePhaseCtx *merge_phase_ctx_;
};

class ObTableLoadMergePhaseOp : public ObTableLoadMergePhaseBaseOp
{
public:
  ObTableLoadMergePhaseOp(ObTableLoadMergeOp *parent);
  virtual ~ObTableLoadMergePhaseOp() = default;

protected:
  virtual int inner_init() = 0;
  virtual int inner_close() = 0;
  int acquire_child_op(ObTableLoadMergeOpType::Type child_op_type, ObIAllocator &allocator,
                       ObTableLoadMergeOp *&child) override;

protected:
  enum Status
  {
    NONE = 0,
    DATA_MERGE,
    INDEX_MERGE,
    CONFLICT_MERGE,
    COMPLETED
  };

protected:
  ObTableLoadMergePhaseCtx inner_phase_ctx_;
  Status status_;
};

class ObTableLoadMergeInsertPhaseOp : public ObTableLoadMergePhaseOp
{
public:
  ObTableLoadMergeInsertPhaseOp(ObTableLoadMergeOp *parent);
  virtual ~ObTableLoadMergeInsertPhaseOp();

protected:
  int inner_init() override;
  int inner_close() override;
  int switch_next_op(bool is_parent_called) override;
};

class ObTableLoadMergeDeletePhaseOp : public ObTableLoadMergePhaseOp
{
public:
  ObTableLoadMergeDeletePhaseOp(ObTableLoadMergeOp *parent);
  virtual ~ObTableLoadMergeDeletePhaseOp();

protected:
  int inner_init() override;
  int inner_close() override;
  int switch_next_op(bool is_parent_called) override;
};

class ObTableLoadMergeAckPhaseOp : public ObTableLoadMergePhaseOp
{
public:
  ObTableLoadMergeAckPhaseOp(ObTableLoadMergeOp *parent);
  virtual ~ObTableLoadMergeAckPhaseOp();

protected:
  int inner_init() override;
  int inner_close() override;
  int switch_next_op(bool is_parent_called) override;
};

} // namespace observer
} // namespace oceanbase
