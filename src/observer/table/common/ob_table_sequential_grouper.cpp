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

#include "ob_table_sequential_grouper.h"
#include "share/table/ob_table.h"

using namespace oceanbase::table;

// template <typename OpType, typename IndexType>
// template <typename Func>
// int GenericSequentialGrouper<OpType, IndexType>::process_groups(Func &&processor)
// {
//   int ret = OB_SUCCESS;
//   for (int i = 0; OB_SUCC(ret) && i < groups_.count(); ++i) {
//     if (OB_FAIL(processor(*groups_.at(i)))) {
//       LOG_WARN("failed to process group", K(ret), KP(groups_.at(i)));
//     }
//   }
//   return ret;
// }
