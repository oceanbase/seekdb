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

#ifndef OCENABASE_SHARE_OB_TRANSFER_INFO_H
#define OCENABASE_SHARE_OB_TRANSFER_INFO_H

#include <stdint.h>
#include "lib/utility/ob_print_utils.h"
#include "lib/string/ob_sql_string.h" // ObSqlString
#include "lib/utility/ob_unify_serialize.h"
#include "share/ob_ls_id.h" // ObLSID
#include "share/ob_define.h"
#include "share/schema/ob_schema_struct.h" // ObTableType, ObIndexType
#include "share/ob_display_list.h"   // ObDisplayList
#include "common/ob_tablet_id.h" // ObTabletID
#include "share/scn.h" // SCN
#include "storage/tablelock/ob_table_lock_common.h" // ObTableLockOwnerID

namespace oceanbase
{
namespace transaction
{
namespace tablelock
{
class ObLockAloneTabletRequest;
}
}

namespace share
{
namespace schema
{
  class ObBasePartition;
}

static const int64_t OB_INVALID_TRANSFER_SEQ = -1;

/////////////// ObTransferTabletInfo ///////////////
// <TabletID:TransferSeq>
// Represents a Tablet for Transfer
struct ObTransferTabletInfo final : public ObDisplayType
{
  OB_UNIS_VERSION(1);
public:
  ObTransferTabletInfo();
  ~ObTransferTabletInfo() = default;
  void reset();
  bool is_valid() const { return tablet_id_.is_valid() && transfer_seq_ > OB_INVALID_TRANSFER_SEQ; }
  const ObTabletID &tablet_id() const { return tablet_id_; }
  int64_t transfer_seq() const { return transfer_seq_; }

  // define init to avoid initializing variables separately
  int init(const ObTabletID &tablet_id, const int64_t transfer_seq);
  bool operator==(const ObTransferTabletInfo &other) const;

  ///////////////////////// display string related ////////////////////////
  // "table_id:part_id" max length: 20 + 20 + ':' + '\0'
  int64_t max_display_str_len() const { return 42; }
  // parse from string "tablet_id:transfer_seq"
  int parse_from_display_str(const common::ObString &str);
  // generate string "tablet_id:transfer_seq"
  int to_display_str(char *buf, const int64_t len, int64_t &pos) const;

  TO_STRING_KV(K_(tablet_id), K_(transfer_seq));
public:
  ObTabletID tablet_id_;
  int64_t transfer_seq_;
};

typedef ObDisplayList<share::ObTransferTabletInfo> ObTransferTabletList;

/////////////// ObTransferPartInfo ///////////////
// <table_id:part_object_id>
// Represents a partition participating in the Transfer
struct ObTransferPartInfo final : public ObDisplayType
{
  struct Compare final
  {
  public:
    Compare() {}
    ~Compare() {}
    bool operator() (const ObTransferPartInfo &left, const ObTransferPartInfo &right)
    {
      return (left.table_id() == right.table_id())
          ? (left.part_object_id() < right.part_object_id())
          : (left.table_id() < right.table_id());
    }
  };

  ObTransferPartInfo() : table_id_(OB_INVALID_ID), part_object_id_(OB_INVALID_ID) {}
  ObTransferPartInfo(const ObObjectID &table_id, const ObObjectID &part_object_id) :
      table_id_(table_id), part_object_id_(part_object_id) {}
  ~ObTransferPartInfo() = default;
  void reset();
  bool is_valid() const { return OB_INVALID_ID != table_id_ && OB_INVALID_ID != part_object_id_; }
  const ObObjectID &table_id() const { return table_id_; }
  const ObObjectID &part_object_id() const { return part_object_id_; }

  // define init to avoid initializing variables separately
  int init(const ObObjectID &table_id, const ObObjectID &part_object_id);
  // init by partition schema

  ///////////////////////// display string related ////////////////////////
  // "table_id:part_id" max length: 20 + 20 + ':' + '\0'
  int64_t max_display_str_len() const { return 42; }
  // parse from string "table_id:part_id"
  int parse_from_display_str(const common::ObString &str);
  // generate string "table_id:part_id"
  // NOTE: can not include commas ','
  int to_display_str(char *buf, const int64_t len, int64_t &pos) const;

  bool operator==(const ObTransferPartInfo &other) const;

  TO_STRING_KV(K_(table_id), K_(part_object_id));
private:
    ObObjectID table_id_;
    ObObjectID part_object_id_;	// It means part_id for level one partition or subpart_id for subpartition.
};

typedef ObDisplayList<ObTransferPartInfo> ObTransferPartList;

struct ObDisplayTabletID final : public ObDisplayType
{
  OB_UNIS_VERSION(1);
public:
  ObDisplayTabletID() : tablet_id_() {}
  explicit ObDisplayTabletID(const ObTabletID &tablet_id) : tablet_id_(tablet_id) {}
  ~ObDisplayTabletID() = default;
  void reset() { tablet_id_.reset(); }
  bool is_valid() const { return tablet_id_.is_valid(); }
  const ObTabletID &tablet_id() const { return tablet_id_; }

  ///////////////////////// display string related ////////////////////////
  // "tablet_id" max length: 20 + '\0'
  int64_t max_display_str_len() const { return 21; }
  // parse from string "tablet_id"
  int parse_from_display_str(const common::ObString &str);
  // generate string "tablet_id"
  int to_display_str(char *buf, const int64_t len, int64_t &pos) const;

  bool operator==(const ObDisplayTabletID &other) const { return tablet_id_ == other.tablet_id_; };
  TO_STRING_KV(K_(tablet_id));
private:
  ObTabletID tablet_id_;
};

typedef ObDisplayList<ObDisplayTabletID> ObDisplayTabletList;


} // end namespace share
} // end namespace oceanbase
#endif
