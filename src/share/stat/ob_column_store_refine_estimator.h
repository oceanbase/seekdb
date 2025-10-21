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

#ifndef OB_COLUMN_STORE_REFINE_ESTIMATOR_H
#define OB_COLUMN_STORE_REFINE_ESTIMATOR_H

#include "share/stat/ob_basic_stats_estimator.h"
namespace oceanbase
{
namespace common
{

class ObColumnStoreRefineEstimator : public ObBasicStatsEstimator
{
public:
  explicit ObColumnStoreRefineEstimator(ObExecContext &ctx, ObIAllocator &allocator);

  int estimate(const ObOptStatGatherParam &param,
               ObOptStat &opt_stat);

};

} // end of namespace common
} // end of namespace oceanbase

#endif /*#endif OB_COLUMN_STORE_REFINE_ESTIMATOR_H */
