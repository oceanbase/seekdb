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

#ifndef SRC_SQL_ENGINE_BASIC_OB_MONITORING_DUMP_OP_H_
#define SRC_SQL_ENGINE_BASIC_OB_MONITORING_DUMP_OP_H_

#include "sql/engine/ob_operator.h"
#include "share/datum/ob_datum.h"

namespace oceanbase
{
namespace sql
{

class ObMonitoringDumpSpec : public ObOpSpec
{
  OB_UNIS_VERSION_V(1);
public:
  ObMonitoringDumpSpec(common::ObIAllocator &alloc, const ObPhyOperatorType type);

  INHERIT_TO_STRING_KV("op_spec", ObOpSpec, K_(flags), K_(dst_op_id));

  uint64_t flags_;
  uint64_t dst_op_id_;

};

class ObMonitoringDumpOp : public ObOperator
{
public:
  ObMonitoringDumpOp(ObExecContext &exec_ctx, const ObOpSpec &spec, ObOpInput *input);

  virtual int inner_open() override;
  virtual int inner_close() override;
  virtual int inner_rescan() override;
  virtual int inner_get_next_row() override;
  virtual int inner_get_next_batch(const int64_t max_row_cnt) override;
  virtual void destroy() override { ObOperator::destroy(); }

  int calc_hash_value();
  int64_t get_monitored_row_count() const
  {
    return rows_;
  }

private:
  common::ObDatum op_name_;
  common::ObDatum tracefile_identifier_;
  uint64_t open_time_;
  uint64_t rows_;
  uint64_t first_row_time_;
  uint64_t last_row_time_;
  bool first_row_fetched_;
  common::ObFixedArray<uint64_t, common::ObIAllocator> output_hash_;
};

}
}

#endif /* SRC_SQL_ENGINE_BASIC_OB_MONITORING_DUMP_OP_H_ */

