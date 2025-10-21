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

#include "storage/ddl/ob_ddl_independent_dag.h"

namespace oceanbase
{
namespace observer
{

using ObTableLoadEmptyInsertDagInitParam = storage::ObDDLIndependentDagInitParam;

class ObTableLoadEmptyInsertDag final : public storage::ObDDLIndependentDag
{
public:
  ObTableLoadEmptyInsertDag() = default;
  virtual ~ObTableLoadEmptyInsertDag() = default;

private:
  int init_by_param(const share::ObIDagInitParam *param) override;
  bool is_scan_finished() override { return true; }
};

} // namespace observer
} // namespace oceanbase
