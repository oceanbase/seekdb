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

#ifndef Oceanbase_SQL_OPTIMIZER_DEFAULT_STAT_H_
#define Oceanbase_SQL_OPTIMIZER_DEFAULT_STAT_H_

namespace oceanbase
{
namespace common
{
const double LN_2 = 0.69314718055994530941723212145818;
#define LOGN(x) (log(x))
#define LOG2(x) (LOGN(static_cast<double>(x)) / LN_2)

const double DEFAULT_CACHE_HIT_RATE = 0.8;

const int64_t DEFAULT_TABLE_ROW_COUNT = 1;

const int64_t DEFAULT_ROW_SIZE = 200;

const int64_t DEFAULT_COLUMN_SIZE = 10;

const int64_t DEFAULT_MICRO_BLOCK_SIZE = 16L * 1024;

const int64_t DEFAULT_MACRO_BLOCK_SIZE_MB = 2L;

const int64_t DEFAULT_MACRO_BLOCK_SIZE = DEFAULT_MACRO_BLOCK_SIZE_MB * 1024 * 1024;

const int64_t OB_EST_DEFAULT_VIRTUAL_TABLE_ROW_COUNT = 100000;

const double EST_DEF_COL_NULL_RATIO = 0.01;

const double EST_DEF_COL_NOT_NULL_RATIO = 1 - EST_DEF_COL_NULL_RATIO;

const double EST_DEF_VAR_EQ_SEL = EST_DEF_COL_NOT_NULL_RATIO;

const int64_t OB_EST_DEFAULT_DATA_SIZE = DEFAULT_ROW_SIZE;

const double OB_DEFAULT_HALF_OPEN_RANGE_SEL = 0.1;

const double OB_DEFAULT_CLOSED_RANGE_SEL = 0.05;

/**
 *@brief  Default selection rate for equivalent expressions like ("A = b")
 */
const double DEFAULT_EQ_SEL = 0.005;

/**
 *@brief Non-equality expression (e.g., "A < b") default selectivity
 */
const double DEFAULT_INEQ_SEL = 1.0 / 3.0;

/**
 *@brief　Default selection rate for spatial expressions: 1 / OB_GEO_S2REGION_OPTION_MAX_CELL
 */
const double DEFAULT_SPATIAL_SEL = 0.25;

/**
 *@brief　Can't guess default selection rate: half and half
 */
const double DEFAULT_SEL = 0.5;
// [agg(expr) <|>|btw const] the default selectivity
const double DEFAULT_AGG_RANGE = 0.05;
// [aggr(expr) = const] default selectivity
const double DEFAULT_AGG_EQ = 0.01;
// like's default selection rate
const double DEFAULT_LIKE_SEL = 0.05;
const double DEFAULT_ANTI_JOIN_SEL = 0.01;
// Range predicate out-of-bound selection rate, reference SQLserver
const double DEFAULT_OUT_OF_BOUNDS_SEL = 0.3;
const double DEFAULT_INEQ_JOIN_SEL = 0.05;

} // namespace common
} // namespace oceanabse






#endif /* Oceanbase_SQL_OPTIMIZER_DEFAULT_STAT_H_ */
