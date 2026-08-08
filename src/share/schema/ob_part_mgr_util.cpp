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

#define USING_LOG_PREFIX SHARE_SCHEMA
#include "share/schema/ob_part_mgr_util.h"



namespace oceanbase
{
using namespace common;

namespace share
{
namespace schema
{

int ObPartGetter::get_part_ids(const common::ObString &part_name,
                               ObIArray<ObObjectID> &part_ids)
{
  int ret = OB_SUCCESS;
  const ObPartitionLevel part_level = table_.get_part_level();
  const ObPartition *part = NULL;
  ObString cmp_part_name;
  bool find = false;
  if (part_name.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invlaid part_name", K(ret), K(part_name));
  } else if (PARTITION_LEVEL_MAX == part_level) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected part level", K(ret), K(part_level));
  } else {
    const ObCheckPartitionMode mode = CHECK_PARTITION_MODE_NORMAL;
    ObPartIterator iter(table_, mode);
    while (OB_SUCC(ret) && !find && OB_SUCC(iter.next(part))) {
      if (OB_ISNULL(part)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("get null partition", K(ret));
      } else {
        cmp_part_name = part->get_part_name();
        LOG_DEBUG("cmp part name", K(cmp_part_name));
        if (ObCharset::case_insensitive_equal(part_name, cmp_part_name)) {
          // match level one part
          find = true;
          if (PARTITION_LEVEL_TWO == part_level) {
            ObSubPartIterator sub_iter(table_, *part, mode);
            const ObSubPartition *subpart = NULL;
            while (OB_SUCC(ret) && OB_SUCC(sub_iter.next(subpart))) {
              if (OB_ISNULL(subpart)) {
                ret = OB_ERR_UNEXPECTED;
                LOG_WARN("get null subpartition", K(ret));
              } else if (OB_FAIL(part_ids.push_back(subpart->get_sub_part_id()))) {
                LOG_WARN("failed to push back subpart id", K(ret));
              }
            }
            if (OB_LIKELY(OB_ITER_END == ret)) {
              ret = OB_SUCCESS;
            }
          } else if (OB_FAIL(part_ids.push_back(PARTITION_LEVEL_ZERO == table_.get_part_level() ?
                                                table_.get_object_id() : part->get_part_id()))) {
            LOG_WARN("failed to push back part id", K(ret));
          }
        } else if (PARTITION_LEVEL_TWO == part_level &&
                   OB_FAIL(get_subpart_ids_in_partition(part_name, *part, part_ids, find))) {
          LOG_WARN("failed to get subpart ids in partition", K(ret));
        }
      }
    }
    if (!find && OB_ITER_END == ret) {
      ret = OB_UNKNOWN_PARTITION;
    }
  }
  return ret;
}

int ObPartGetter::get_subpart_ids(const common::ObString &part_name,
                                  ObIArray<ObObjectID> &part_ids)
{
  int ret = OB_SUCCESS;
  const ObPartitionLevel part_level = table_.get_part_level();
  const ObPartition *part = NULL;
  bool find = false;
  if (part_name.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invlaid part_name", K(ret), K(part_name));
  } else if (PARTITION_LEVEL_ZERO == part_level) {
    ret = OB_ERR_NOT_PARTITIONED;
    LOG_WARN("table is not partitioned", K(ret));
  } else if (PARTITION_LEVEL_ONE == part_level) {
    // Use subpartition() on the primary partition table to report "specified subpartition does not exist".
    ret = OB_UNKNOWN_SUBPARTITION;
    LOG_WARN("subpartition no exists", K(ret));
  } else if (PARTITION_LEVEL_TWO == part_level) {
    const ObCheckPartitionMode mode = CHECK_PARTITION_MODE_NORMAL;
    ObPartIterator iter(table_, mode);
    while (OB_SUCC(ret) && !find && OB_SUCC(iter.next(part))) {
      if (OB_ISNULL(part)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("get null partition", K(ret));
      } else if (OB_FAIL(get_subpart_ids_in_partition(part_name, *part, part_ids, find))) {
        LOG_WARN("failed to get subpart ids in partition", K(ret));
      }
    }
    if (!find && OB_ITER_END == ret) {
      ret = OB_UNKNOWN_SUBPARTITION;
    }
  } else {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected part level", K(ret), K(part_level));
  }
  return ret;
}

int ObPartGetter::get_subpart_ids_in_partition(const common::ObString &part_name,
                                               const ObPartition &partition,
                                               ObIArray<ObObjectID> &part_ids,
                                               bool &find)
{
  int ret = OB_SUCCESS;
  find = false;
  const ObCheckPartitionMode mode = CHECK_PARTITION_MODE_NORMAL;
  ObSubPartIterator sub_iter(table_, partition, mode);
  const ObSubPartition *subpart = NULL;
  ObString cmp_part_name;
  while (OB_SUCC(ret) && !find && OB_SUCC(sub_iter.next(subpart))) {
    if (OB_ISNULL(subpart)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("get null subpartition", K(ret));
    } else {
      cmp_part_name = subpart->get_part_name();
      LOG_DEBUG("cmp part name", K(cmp_part_name));
      if (ObCharset::case_insensitive_equal(part_name, cmp_part_name)) {
        if (OB_FAIL(part_ids.push_back(subpart->get_sub_part_id()))) {
          LOG_WARN("failed to push back subpart id", K(ret));
        } else {
          find = true;
        }
      }
    }
  }
  if (!find && OB_ITER_END == ret) {
    ret = OB_SUCCESS;
  }
  return ret;
}


}
}
}
