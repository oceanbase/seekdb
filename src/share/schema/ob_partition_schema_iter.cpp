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
#include "share/schema/ob_partition_schema_iter.h"

namespace oceanbase
{
using namespace common;

namespace share
{
namespace schema
{

int ObPartIterator::next(const ObPartition *&part)
{
  int ret = OB_SUCCESS;
  part = NULL;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
  } else {
    // The partition_array of the system table is empty and needs to be processed by part_num
    int64_t part_num = check_normal_partition(check_partition_mode_) ?
                       partition_schema_->get_first_part_num() : 0;
    int64_t hidden_part_num = check_hidden_partition(check_partition_mode_) ?
                              partition_schema_->get_hidden_partition_num() : 0;
    int64_t total_part_num = part_num + hidden_part_num;
    part_.reset();
    if (idx_++ >= total_part_num - 1) {
      ret = OB_ITER_END;
    } else if (0 <= idx_ && part_num > idx_) {
       // deal with normal partition
      int64_t idx = idx_;
      ObPartition **part_array = partition_schema_->get_part_array();
      const ObPartitionLevel part_level = partition_schema_->get_part_level();
      if (PARTITION_LEVEL_ZERO == part_level) {
        ObString part_name(
                           ObPartitionSchema::MYSQL_NON_PARTITIONED_TABLE_PART_NAME);
        if (OB_FAIL(part_.set_part_name(part_name))) {
        } else {
          part_.set_part_id(0);
          part = &part_;
        }
      } else if (PARTITION_LEVEL_ONE == part_level ||
                 PARTITION_LEVEL_TWO == part_level) {
        if (OB_ISNULL(part_array)) {
          ret = OB_ERR_UNEXPECTED;
        } else {
          part = part_array[idx];
        }
      } else {
        ret = OB_ERR_UNEXPECTED;
      }
    } else if (part_num <= idx_ && total_part_num > idx_) {
      // deal with hidden partition
      int64_t idx = idx_ - part_num;
      ObPartition **hidden_part_array = partition_schema_->get_hidden_part_array();
      if (OB_ISNULL(hidden_part_array)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (idx < 0 || idx >= partition_schema_->get_hidden_partition_num()) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        part = hidden_part_array[idx];
      }
    } else {
      ret = OB_ERR_UNEXPECTED;
    }
  }
  return ret;
}

int ObSubPartIterator::next(const ObSubPartition *&subpart)
{
  int ret = OB_SUCCESS;
  subpart = NULL;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
  } else {
    int64_t sub_part_num = check_normal_partition(check_partition_mode_) ?
                           part_->get_subpartition_num() : 0;
    int64_t hidden_sub_part_num = check_hidden_partition(check_partition_mode_) ?
                                   part_->get_hidden_subpartition_num() : 0;
    int64_t total_sub_part_num = sub_part_num + hidden_sub_part_num;
    if (idx_++ >= total_sub_part_num - 1) {
      ret = OB_ITER_END;
    } else {
      int64_t idx = OB_INVALID_INDEX;
      ObSubPartition **subpart_array = NULL;
      if (0 <= idx_ && sub_part_num > idx_) {
        idx = idx_;
        subpart_array = part_->get_subpart_array();
      } else if (sub_part_num <= idx_ && total_sub_part_num > idx_) {
        idx = idx_ - sub_part_num;
        subpart_array = part_->get_hidden_subpart_array();
      } else {
        ret = OB_ERR_UNEXPECTED;
      }
      if (OB_FAIL(ret)) {
      } else if (OB_ISNULL(subpart_array)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_ISNULL(subpart_array[idx])) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        subpart = subpart_array[idx];
      }
    }
  }
  return ret;
}

ObPartitionSchemaIter::ObPartitionSchemaIter(
  const ObPartitionSchema &partition_schema,
  const ObCheckPartitionMode check_partition_mode)
  : partition_schema_(partition_schema),
    check_partition_mode_(check_partition_mode),
    part_iter_(),
    subpart_iter_(),
    part_(NULL),
    part_idx_(common::OB_INVALID_INDEX),
    subpart_idx_(common::OB_INVALID_INDEX)
{
}

int ObPartitionSchemaIter::next_tablet_id(
    ObTabletID &tablet_id)
{
  int ret = OB_SUCCESS;
  ObPartitionSchemaIter::Info info;
  if (OB_FAIL(next_partition_info(info))) {
    if (OB_ITER_END != ret) {
    }
  } else {
    tablet_id = info.tablet_id_;
  }
  return ret;
}


int ObPartitionSchemaIter::next_partition_info(
    ObPartitionSchemaIter::Info &info)
{
  int ret = OB_SUCCESS;

  const ObPartitionLevel part_level = partition_schema_.get_part_level();
  const uint64_t schema_id = partition_schema_.get_table_id();
  if (is_virtual_table(schema_id)) {
    ret = OB_NOT_SUPPORTED;
  } else if (OB_ISNULL(part_)) {
    if (PARTITION_LEVEL_TWO == part_level) {
      // Normal partition of nontemplate secondary partitioned table may contains
      // hidden subpartitions, so we should always iterate normal partition
      // and ObSubPartIterator will filter subpartitions by check_partition_mode_.
      const ObCheckPartitionMode new_mode = static_cast<ObCheckPartitionMode>(check_partition_mode_ | CHECK_PARTITION_NORMAL_FLAG);
      part_iter_.init(partition_schema_, new_mode);
    } else {
      part_iter_.init(partition_schema_, check_partition_mode_);
    }
  }

  if (OB_FAIL(ret)) {
  } else if (OB_ISNULL(part_) || PARTITION_LEVEL_TWO != part_level) {
    if (OB_FAIL(part_iter_.next(part_))) {
      if (ret != OB_ITER_END) {
      }
    } else if (OB_ISNULL(part_)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (PARTITION_LEVEL_TWO == part_level) {
      subpart_iter_.init(partition_schema_, *part_, check_partition_mode_);
    }
    part_idx_++;
    subpart_idx_ = OB_INVALID_INDEX;
  }

  if (OB_SUCC(ret)) {
    info.object_id_ = PARTITION_LEVEL_ZERO == part_level ?
                      partition_schema_.get_object_id() :
                      part_->get_part_id();
    info.tablet_id_ = PARTITION_LEVEL_ZERO == part_level ?
                      partition_schema_.get_tablet_id() :
                      part_->get_tablet_id();
    info.part_idx_ = part_idx_;
    info.part_ = PARTITION_LEVEL_ZERO == part_level ?  NULL : part_;
    info.partition_ = PARTITION_LEVEL_ZERO == part_level ?  NULL : part_;
    if (PARTITION_LEVEL_TWO == part_level) {
      const ObSubPartition *subpart = NULL;
      if (OB_FAIL(subpart_iter_.next(subpart))) {
        if (ret != OB_ITER_END) {
        } else if (OB_FAIL(part_iter_.next(part_))) {
          if (ret != OB_ITER_END) {
          }
        } else if (OB_ISNULL(part_)) {
          ret = OB_ERR_UNEXPECTED;
        } else {
          part_idx_++;
          subpart_idx_ = OB_INVALID_INDEX;
          if (FALSE_IT(subpart_iter_.init(partition_schema_,
                                          *part_,
                                          check_partition_mode_))) {
            // will never be here
          } else if (OB_FAIL(subpart_iter_.next(subpart))) {
            if (ret != OB_ITER_END) {
            }
          }
        }
      }
      if (OB_SUCC(ret)) {
        subpart_idx_++;
        if (OB_ISNULL(subpart)) {
          ret = OB_ERR_UNEXPECTED;
        } else {
          info.tablet_id_ = subpart->get_tablet_id();
          info.object_id_ = subpart->get_sub_part_id();
          info.part_idx_ = part_idx_;
          info.subpart_idx_ = subpart_idx_;
          info.part_ = part_;
          info.partition_ = subpart;
        }
      }
    }
  }

  return ret;
}

} // namespace schema
} // namespace share
} // namespace oceanbase
