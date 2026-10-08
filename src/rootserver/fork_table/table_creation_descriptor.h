/* Copyright (c) 2025 OceanBase. Licensed under the Apache License, Version 2.0. */
#ifndef OCEANBASE_ROOTSERVER_TABLE_CREATION_DESCRIPTOR_H_
#define OCEANBASE_ROOTSERVER_TABLE_CREATION_DESCRIPTOR_H_

#include "storage/tablet/ob_batch_create_tablet_arg.h"
#include <string>

namespace oceanbase {
namespace rootserver {

// One immutable physical definition per table version. Partition/tablet IDs
// belong to the source tree, so adding partitions does not duplicate this
// definition. Raw table IDs describe main/index/LOB relationships.
class TableCreationDescriptor final
{
public:
  TableCreationDescriptor() : allocator_("TableCreateDesc") {}
  int init(const share::schema::ObTableSchema &schema, uint64_t data_format_version);
  int encode(std::string &bytes) const;
  int decode(const std::string &bytes);
  // The batch owns a deep copy; this descriptor can be released immediately.
  // On failure its descriptor arrays remain unchanged.
  int append_to(obcall::ObBatchCreateTabletArg &batch, int64_t &index) const;
  bool is_valid() const;
  void reset();
  const storage::ObCreateTabletSchema &schema() const { return schema_; }
  uint64_t data_table_id() const { return data_table_id_; }
  uint64_t lob_meta_table_id() const { return lob_meta_table_id_; }
  uint64_t lob_piece_table_id() const { return lob_piece_table_id_; }
private:
  common::ObArenaAllocator allocator_;
  storage::ObCreateTabletSchema schema_;
  obcall::ObCreateTabletExtraInfo extra_;
  uint64_t data_table_id_ = common::OB_INVALID_ID;
  uint64_t lob_meta_table_id_ = common::OB_INVALID_ID;
  uint64_t lob_piece_table_id_ = common::OB_INVALID_ID;
  DISALLOW_COPY_AND_ASSIGN(TableCreationDescriptor);
};

} // namespace rootserver
} // namespace oceanbase
#endif
