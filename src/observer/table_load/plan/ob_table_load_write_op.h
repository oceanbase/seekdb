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

#include "observer/table_load/plan/ob_table_load_table_op.h"

namespace oceanbase
{
namespace observer
{
class ObTableLoadDagDirectWriteChannel;
class ObTableLoadDagStoreWriteChannel;
class ObTableLoadDagPreSortWriteChannel;

class ObTableLoadWriteOp : public ObTableLoadTableBaseOp
{
protected:
  ObTableLoadWriteOp(ObTableLoadTableBaseOp *parent) : ObTableLoadTableBaseOp(parent) {}

public:
  static int build(ObTableLoadTableOp *table_op, const ObTableLoadWriteType::Type write_type,
                   ObTableLoadWriteOp *&write_op);
};

// direct_write
class ObTableLoadDirectWriteOp final : public ObTableLoadWriteOp
{
public:
  ObTableLoadDirectWriteOp(ObTableLoadTableBaseOp *parent);
  virtual ~ObTableLoadDirectWriteOp();

public:
  ObTableLoadDagDirectWriteChannel *write_channel_;
};

// store_write
class ObTableLoadStoreWriteOp final : public ObTableLoadWriteOp
{
public:
  ObTableLoadStoreWriteOp(ObTableLoadTableBaseOp *parent);
  virtual ~ObTableLoadStoreWriteOp();

public:
  ObTableLoadDagStoreWriteChannel *write_channel_;
};

// pre_sort_write
class ObTableLoadPreSortWriteOp final : public ObTableLoadWriteOp
{
public:
  ObTableLoadPreSortWriteOp(ObTableLoadTableBaseOp *parent);
  virtual ~ObTableLoadPreSortWriteOp();

public:
  ObTableLoadDagPreSortWriteChannel *write_channel_;
};

} // namespace observer
} // namespace oceanbase
