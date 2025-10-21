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

#include "observer/table_load/ob_table_load_merge_table_op.h"

namespace oceanbase
{
namespace observer
{
class ObTableLoadMergeDataTableOp final : public ObTableLoadMergeTableOp
{
public:
  ObTableLoadMergeDataTableOp(ObTableLoadMergePhaseBaseOp *parent);
  virtual ~ObTableLoadMergeDataTableOp() = default;

protected:
  int inner_init() override;
  int inner_close() override;
};

class ObTableLoadMergeDeletePhaseDataTableOp final : public ObTableLoadMergeTableOp
{
public:
  ObTableLoadMergeDeletePhaseDataTableOp(ObTableLoadMergePhaseBaseOp *parent);
  virtual ~ObTableLoadMergeDeletePhaseDataTableOp() = default;

protected:
  int inner_init() override;
  int inner_close() override;
};

class ObTableLoadMergeAckPhaseDataTableOp final : public ObTableLoadMergeTableOp
{
public:
  ObTableLoadMergeAckPhaseDataTableOp(ObTableLoadMergePhaseBaseOp *parent);
  virtual ~ObTableLoadMergeAckPhaseDataTableOp() = default;

protected:
  int inner_init() override;
  int inner_close() override;
};

} // namespace observer
} // namespace oceanbase
