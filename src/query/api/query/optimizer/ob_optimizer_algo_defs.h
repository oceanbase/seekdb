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

#ifndef OCEANBASE_QUERY_OPTIMIZER_ALGO_DEFS_H_
#define OCEANBASE_QUERY_OPTIMIZER_ALGO_DEFS_H_

namespace oceanbase
{
namespace sql
{

enum JoinAlgo
{
  INVALID_JOIN_ALGO = 0,
  NESTED_LOOP_JOIN = (1UL),
  MERGE_JOIN = (1UL << 1),
  HASH_JOIN = (1UL << 2)
};

enum AggregateAlgo
{
  AGGREGATE_UNINITIALIZED = 0,
  MERGE_AGGREGATE,
  HASH_AGGREGATE,
  SCALAR_AGGREGATE
};

enum SetAlgo
{
  INVALID_SET_ALGO = 0,
  MERGE_SET,
  HASH_SET
};

enum DistAlgo
{
  DIST_INVALID_METHOD = 0,
  DIST_BASIC_METHOD = (1UL),
  DIST_PULL_TO_LOCAL = (1UL << 1),
  DIST_HASH_NONE = (1UL << 2),
  DIST_NONE_HASH = (1UL << 3),
  DIST_HASH_HASH = (1UL << 4),
  DIST_BROADCAST_NONE = (1UL << 5),
  DIST_NONE_BROADCAST = (1UL << 6),
  DIST_BC2HOST_NONE = (1UL << 7),
  DIST_PARTITION_NONE = (1UL << 8),
  DIST_NONE_PARTITION = (1UL << 9),
  DIST_NONE_ALL = (1UL << 10),
  DIST_ALL_NONE = (1UL << 11),
  DIST_RANDOM_ALL = (1UL << 12),
  DIST_HASH_ALL = (1UL << 13),
  DIST_PARTITION_WISE = (1UL << 14),
  DIST_EXT_PARTITION_WISE = (1UL << 15),
  DIST_HASH_HASH_LOCAL = (1UL << 16),
  DIST_PARTITION_HASH_LOCAL = (1UL << 17),
  DIST_HASH_LOCAL_PARTITION = (1UL << 18),
  DIST_BROADCAST_HASH_LOCAL = (1UL << 19),
  DIST_HASH_LOCAL_BROADCAST = (1UL << 20),
  DIST_MAX_JOIN_METHOD = (1UL << 21),
  DIST_SET_RANDOM = (1UL << 22),
  DIST_SET_PARTITION_WISE = (1UL << 23)
};

enum WinDistAlgo
{
  WIN_DIST_INVALID = 0,
  WIN_DIST_NONE = (1UL),
  WIN_DIST_HASH = (1UL << 1),
  WIN_DIST_RANGE = (1UL << 2),
  WIN_DIST_LIST = (1UL << 3),
  WIN_DIST_HASH_LOCAL = (1UL << 4)
};

} // namespace sql
} // namespace oceanbase

#endif // OCEANBASE_QUERY_OPTIMIZER_ALGO_DEFS_H_
