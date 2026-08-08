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

#ifndef OCEANBASE_SHARE_SCHEMA_OB_PARTITION_SCHEMA_ITER_
#define OCEANBASE_SHARE_SCHEMA_OB_PARTITION_SCHEMA_ITER_

#include "share/schema/ob_schema_struct.h"

namespace oceanbase
{
namespace share
{
namespace schema
{

class ObPartIterator
{
public:
  ObPartIterator()
    : is_inited_(false),
      partition_schema_(NULL),
      idx_(common::OB_INVALID_INDEX), part_(),
      check_partition_mode_(CHECK_PARTITION_MODE_NORMAL)
   {}

  ObPartIterator(const ObPartitionSchema &partition_schema,
                 const ObCheckPartitionMode &mode)
  {
    (void) init(partition_schema, mode);
  }

  void init(const ObPartitionSchema &partition_schema,
            const ObCheckPartitionMode &mode)
  {
    partition_schema_ = &partition_schema;
    idx_ = common::OB_INVALID_INDEX;
    part_.reset();
    check_partition_mode_ = mode;
    is_inited_ = true;
  }
  int next(const ObPartition *&part);
private:
  bool is_inited_;
  const ObPartitionSchema *partition_schema_;
  int64_t idx_;
  // @note: For non-range partitions, the schema does not store partition objects,
  //  here is the mock out for external use
  share::schema::ObPartition part_;
  ObCheckPartitionMode check_partition_mode_;
};

class ObSubPartIterator
{
public:
  ObSubPartIterator()
    : is_inited_(false),
      partition_schema_(NULL),
      part_(NULL),
      idx_(common::OB_INVALID_INDEX), subpart_(),
      check_partition_mode_(CHECK_PARTITION_MODE_NORMAL)
  {
  }
  ObSubPartIterator(
    const ObPartitionSchema &partition_schema,
    const ObPartition &part,
    const ObCheckPartitionMode &mode)
  {
    (void) init(partition_schema, part, mode);
  }
  void init(
    const ObPartitionSchema &partition_schema,
    const ObPartition &part,
    const ObCheckPartitionMode &mode)
  {
    partition_schema_ = &partition_schema;
    part_ = &part;
    idx_ = common::OB_INVALID_INDEX;
    subpart_.reset();
    check_partition_mode_ = mode;
    is_inited_ = true;
  }
  int next(const ObSubPartition *&subpart);
private:
  bool is_inited_;
  const ObPartitionSchema *partition_schema_;
  const ObPartition *part_;
  int64_t idx_;
  // @note: For non-range partitions, the schema does not store partition objects,
  //  here is the mock out for external use
  ObSubPartition subpart_;
  ObCheckPartitionMode check_partition_mode_;
};

class ObPartitionSchemaIter
{
public:
  struct Info {
  public:
   Info()
     : object_id_(common::OB_INVALID_ID),
       tablet_id_(common::ObTabletID::INVALID_TABLET_ID),
       part_idx_(common::OB_INVALID_INDEX),
       subpart_idx_(common::OB_INVALID_INDEX),
       part_(NULL),
       partition_(NULL) {}
   ~Info() {}
   TO_STRING_KV(K_(object_id), K_(tablet_id),
                K_(part_idx), K_(subpart_idx),
                KP_(partition));

   ObObjectID object_id_;
   ObTabletID tablet_id_;
   int64_t part_idx_;    // partition offset in partition_array
   int64_t subpart_idx_; // subparititon offset in subpartition_array
   /* first partition in partitioned table, it's null when iter non-partitioned table */
   const share::schema::ObPartition *part_;
   /*
    * When part_level = 0, it's null;
    * When part_level = 1, it's ObPartition;
    * When part_level = 2, it's ObSubPartition.
    */
   const share::schema::ObBasePartition *partition_;
  };
public:
  ObPartitionSchemaIter() = delete;
  explicit ObPartitionSchemaIter(const ObPartitionSchema &partition_schema,
                                 const ObCheckPartitionMode check_partition_mode);
  int next_partition_info(ObPartitionSchemaIter::Info &info);
  int next_tablet_id(ObTabletID &tablet_id);
  TO_STRING_KV(K_(partition_schema), K_(check_partition_mode), K_(part_idx), K_(subpart_idx));
private:
  const ObPartitionSchema &partition_schema_;
  ObCheckPartitionMode check_partition_mode_;
  ObPartIterator part_iter_;
  ObSubPartIterator subpart_iter_;
  const ObPartition *part_;
  int64_t part_idx_;
  int64_t subpart_idx_;
  DISALLOW_COPY_AND_ASSIGN(ObPartitionSchemaIter);
};

struct ObPartitionNameCmp
{
  ObPartitionNameCmp(const ObCollationType collation_type) : collation_type_(collation_type) {}
  ~ObPartitionNameCmp() {}
  bool operator()(const ObPartition *lhs, const ObPartition *rhs) {
    return 0 > ObCharset::strcmp(collation_type_, lhs->get_part_name(), rhs->get_part_name());
  }
  ObCollationType collation_type_;
};

} // namespace schema
} // namespace share
} // namespace oceanbase

#endif // OCEANBASE_SHARE_SCHEMA_OB_PARTITION_SCHEMA_ITER_
