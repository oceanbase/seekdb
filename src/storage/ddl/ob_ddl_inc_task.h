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

#include "share/scheduler/ob_tenant_dag_scheduler.h"

namespace oceanbase
{
namespace storage
{

class ObDDLIncStartTask final : public share::ObITask
{
public:
  ObDDLIncStartTask(const int64_t tablet_idx);
  int process() override;

private:
  int generate_next_task(ObITask *&next_task) override;
private:
  int64_t tablet_idx_;
};

class ObDDLIncCommitTask final : public share::ObITask
{
public:
  ObDDLIncCommitTask(const int64_t tablet_idx);
  ObDDLIncCommitTask(const ObTabletID &tablet_id);
  int process() override;

private:
  int generate_next_task(ObITask *&next_task) override;

private:
  int64_t tablet_idx_;
  ObTabletID tablet_id_;
};

} // namespace storage
} // namespace oceanbase
