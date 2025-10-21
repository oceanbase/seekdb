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

#ifndef OCEANBASE_OBSERVER_OB_TABLE_EXECUTOR_FACTORY_H
#define OCEANBASE_OBSERVER_OB_TABLE_EXECUTOR_FACTORY_H
#include "ob_table_executor_reg.h"

namespace oceanbase
{
namespace table
{

class ObTableExecutorFactory
{
public:
  static int alloc_executor(common::ObIAllocator &alloc,
                            ObTableCtx &ctx,
                            const ObTableApiSpec &spec,
                            ObTableApiExecutor *&executor);

  static int generate_spec(common::ObIAllocator &alloc,
                           const ObTableExecutorType &type,
                           ObTableCtx &ctx,
                           ObTableApiSpec *&spec);

  static inline bool is_registered(const ObTableExecutorType exec_type)
  {
    return exec_type >= TABLE_API_EXEC_INVALID
           && exec_type < TABLE_API_EXEC_MAX
           && NULL != G_TABLE_API_ALLOC_FUNCS_[exec_type].exec_func_;
  }

  struct AllocFunc
  {
    // use typeof instead
    typeof(&ObTableExecutorFactory::alloc_executor) exec_func_;
    typeof(&ObTableExecutorFactory::generate_spec) gen_spec_func_;
  };

private:
  static AllocFunc *G_TABLE_API_ALLOC_FUNCS_;
};

} // end namespace table
} // end namespace oceanbase

#endif // OCEANBASE_OBSERVER_OB_TABLE_EXECUTOR_FACTORY_H
