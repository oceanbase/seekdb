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

#ifndef OCEANBASE_OBSERVER_OB_TABLE_GROUP_SERVICE_H_
#define OCEANBASE_OBSERVER_OB_TABLE_GROUP_SERVICE_H_

#include <unordered_map>
#include "ob_table_tenant_group.h"
#include "ob_table_group_execute.h"
#include "observer/table/ob_table_batch_service.h"

namespace oceanbase
{

namespace table
{

class ObTableGroupService final
{
public:
  static int process(ObTableGroupCtx &ctx, ObITableOp *op, bool is_direct_execute = false);
  static int process_trigger();
  static int process_one_by_one(ObTableGroup &group);
private:
  static int add_and_try_to_get_batch(ObITableOp *op, ObITableGroupValue *group, ObIArray<ObITableOp *> &ops);
  static int execute_batch(ObTableGroupCtx &ctx, 
                           ObIArray<ObITableOp *> &ops, 
                           bool is_direct_execute,
                          bool add_fail_group);
  static int process_failed_group();
  static int process_other_group();
  static int process_expired_group();
  static int check_legality(const ObTableGroupCtx &ctx, const ObITableGroupKey *key, const ObITableOp *op);
  static int start_trans(ObTableBatchCtx &batch_ctx);
  static int end_trans(const ObTableBatchCtx &batch_ctx,
                       ObTableGroupCommitEndTransCb *cb,
                       bool is_rollback);
  static int init_table_ctx(ObTableGroup &group, ObTableCtx &tb_ctx);
  static int init_batch_ctx(ObTableGroup &group, ObTableBatchCtx &batch_ctx);
};

} // end namespace table
} // end namespace oceanbase
#endif /* OCEANBASE_OBSERVER_OB_TABLE_GROUP_SERVICE_H_ */
