/**
 * Copyright (c) 2025 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
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