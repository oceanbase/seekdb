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

#define USING_LOG_PREFIX SERVER

#include "observer/table_load/dag/ob_table_load_empty_insert_dag.h"

namespace oceanbase
{
namespace observer
{
using namespace share;
using namespace storage;

int ObTableLoadEmptyInsertDag::init_by_param(const ObIDagInitParam *param)
{
  int ret = OB_SUCCESS;
  const ObTableLoadEmptyInsertDagInitParam *init_param =
    static_cast<const ObTableLoadEmptyInsertDagInitParam *>(param);
  if (OB_UNLIKELY(nullptr == init_param || !init_param->is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), KPC(init_param));
  } else if (OB_FAIL(ObDDLIndependentDag::init_by_param(init_param))) {
    LOG_WARN("init ddl independent dag failed", K(ret), KPC(init_param));
  } else {
    ObArray<ObITask *> write_macro_block_tasks;
    if (OB_FAIL(generate_write_macro_block_tasks(write_macro_block_tasks))) {
      LOG_WARN("fail to generate write macro block tasks", KR(ret));
    } else if (OB_FAIL(batch_add_task(write_macro_block_tasks))) {
      LOG_WARN("batch add task failed", K(ret), K(write_macro_block_tasks.count()));
    }
  }
  FLOG_INFO("empty insert dag init", KR(ret), KPC(this));
  return ret;
}

} // namespace observer
} // namespace oceanbase
