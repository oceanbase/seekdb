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

#ifndef __OCEANBASE_SQL_ENGINE_PX_OB_PX_BASIC_INFO_H__
#define __OCEANBASE_SQL_ENGINE_PX_OB_PX_BASIC_INFO_H__


namespace oceanbase
{
namespace sql
{

#define IS_PX_COORD(type) \
     ((type) == PHY_PX_FIFO_COORD || \
      (type) == PHY_PX_MERGE_SORT_COORD ||\
      (type) == PHY_PX_ORDERED_COORD ||\
      (type) == PHY_VEC_PX_MERGE_SORT_COORD)
// NOTE: There may be multiple receive op types, all of which need to be judged. Currently, there is only one.
#define IS_PX_RECEIVE(type) \
     ((type) == PHY_PX_FIFO_RECEIVE || \
      (type) == PHY_PX_MERGE_SORT_RECEIVE || \
      (type) == PHY_PX_MERGE_SORT_COORD || \
      (type) == PHY_VEC_PX_MERGE_SORT_COORD || \
      (type) == PHY_VEC_PX_MERGE_SORT_RECEIVE || \
      (type) == PHY_PX_FIFO_COORD || \
      (type) == PHY_PX_ORDERED_COORD)

#define IS_PX_TRANSMIT(type) \
     ((type) == PHY_PX_DIST_TRANSMIT || \
      (type) == PHY_PX_REPART_TRANSMIT || \
      (type) == PHY_PX_REDUCE_TRANSMIT)

#define IS_PX_GI(type) \
  ((type) == PHY_GRANULE_ITERATOR)

#define IS_PX_JOIN_FILTER(type) \
  ((type) == PHY_JOIN_FILTER)


#define IS_PX_MODIFY(type) \
  ((type) == PHY_PX_MULTI_PART_UPDATE || \
   (type) == PHY_PX_MULTI_PART_DELETE || \
   (type) == PHY_PX_MULTI_PART_INSERT || \
   (type) == PHY_TABLE_DIRECT_INSERT)
// Determine if an operator is a subclass of Transmit, used for splitting Job
#define IS_TRANSMIT(type) \
    ((type) == PHY_DIRECT_TRANSMIT || \
     (type) == PHY_DISTRIBUTED_TRANSMIT || \
     (type) == PHY_PX_DIST_TRANSMIT || \
     (type) == PHY_PX_REPART_TRANSMIT || \
     (type) == PHY_PX_REDUCE_TRANSMIT || \
     (type) == PHY_DETERMINATE_TASK_TRANSMIT)

#define IS_DIST_TRANSMIT(type) \
     ((type) == PHY_DISTRIBUTED_TRANSMIT || \
      (type) == PHY_DETERMINATE_TASK_TRANSMIT || \
      (type) == PHY_PX_DIST_TRANSMIT || \
      (type) == PHY_PX_REPART_TRANSMIT || \
      (type) == PHY_PX_REDUCE_TRANSMIT)

#define IS_PX_TRANSMIT(type) \
     ((type) == PHY_PX_DIST_TRANSMIT || \
      (type) == PHY_PX_REPART_TRANSMIT || \
      (type) == PHY_PX_REDUCE_TRANSMIT)

#define IS_DML(type) \
  ((type) == PHY_INSERT || \
   (type) == PHY_INSERT_ON_DUP || \
   (type) == PHY_INSERT_RETURNING || \
   (type) == PHY_INSERT_ON_DUP_RETURNING || \
   (type) == PHY_DELETE || \
   (type) == PHY_DELETE_RETURNING || \
   (type) == PHY_UPDATE || \
   (type) == PHY_UPDATE_RETURNING || \
   (type) == PHY_REPLACE || \
   (type) == PHY_REPLACE_RETURNING || \
   (type) == PHY_PX_MULTI_PART_DELETE || \
   (type) == PHY_PX_MULTI_PART_INSERT || \
   (type) == PHY_PX_MULTI_PART_UPDATE || \
   (type) == PHY_PX_MULTI_PART_SSTABLE_INSERT || \
   (type) == PHY_VEC_PX_MULTI_PART_SSTABLE_INSERT || \
   (type) == PHY_LOCK || \
   (type) == PHY_TABLE_DIRECT_INSERT)

#define IS_SAMPLE_SCAN(type) \
      ((type) == PHY_BLOCK_SAMPLE_SCAN || \
      (type) == PHY_ROW_SAMPLE_SCAN || \
      (type) == PHY_DDL_BLOCK_SAMPLE_SCAN)

enum JoinFilterMode
{
  NOT_INIT,
  CREATE,
  USE,
};
enum JoinFilterSharedType
{
  INVALID_TYPE,
  SHARED_JOIN_FILTER,  // broadcast/hash
  NONSHARED_JOIN_FILTER, // partition wise join/pkey
  SHARED_PARTITION_JOIN_FILTER, // bc2host
  NONSHARED_PARTITION_JOIN_FILTER, // except bc2host
};
enum ObStatCollectorType
{
  NOT_INIT_TYPE,
  SAMPLE_SORT,
};
enum ObPxSampleType
{
  NOT_INIT_SAMPLE_TYPE,
  HEADER_INPUT_SAMPLE, // sample the first N input rows
  FULL_INPUT_SAMPLE, // sample the full input rows
  OBJECT_SAMPLE
};



}
}
#endif /* __OCEANBASE_SQL_ENGINE_PX_OB_PX_BASIC_INFO_H__ */
