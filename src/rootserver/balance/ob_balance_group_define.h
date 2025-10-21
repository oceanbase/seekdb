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

#ifndef OCEANBASE_ROOTSERVER_OB_BALANCE_GROUP_DEFINE_H_
#define OCEANBASE_ROOTSERVER_OB_BALANCE_GROUP_DEFINE_H_
#include "lib/ob_define.h"
#include "lib/string/ob_fixed_length_string.h"
#include "share/ob_ls_id.h"

namespace oceanbase
{

namespace share
{
namespace schema
{
class ObSimpleTablegroupSchema;
class ObSimpleTableSchemaV2;
class ObPartition;
}
}

namespace rootserver
{
typedef common::ObFixedLengthString<
        common::OB_MAX_BALANCE_GROUP_NAME_LENGTH>
        ObBalanceGroupName;

struct ObBalanceGroupID
{
public:
  ObBalanceGroupID() : id_high_(common::OB_INVALID_ID),
                     id_low_(common::OB_INVALID_ID) {}
  ObBalanceGroupID(const uint64_t id_high,
                 const uint64_t id_low)
    : id_high_(id_high),
      id_low_(id_low) {}

  int hash(uint64_t &res) const {
    res = 0;
    res = murmurhash(&id_high_, sizeof(id_high_), res);
    res = murmurhash(&id_low_, sizeof(id_low_), res);
    return common::OB_SUCCESS;
  }
  bool operator ==(const ObBalanceGroupID &other) const {
    return id_high_ == other.id_high_ &&
      id_low_ == other.id_low_;
  }
  bool operator !=(const ObBalanceGroupID &other) const {
    return !(*this == other);
  }

  TO_STRING_KV(K_(id_high),
               K_(id_low));

  bool is_valid() const {
    return OB_INVALID_ID != id_high_
           && OB_INVALID_ID != id_low_;
  }

  void reset() {
    id_high_ = common::OB_INVALID_ID;
    id_low_ = common::OB_INVALID_ID;
  }

public:
  uint64_t id_high_;
  uint64_t id_low_;
};

static const ObBalanceGroupID NON_PART_BG_ID(0, 0);
static const ObBalanceGroupID DUP_TABLE_BG_ID(0, 1);

class ObBalanceGroup
{
public:
  ObBalanceGroup() : id_(), name_() {}
  ~ObBalanceGroup() {}

  const ObBalanceGroupID &id() const { return id_; }
  const ObBalanceGroupName &name() const { return name_; }

  int init_by_tablegroup(const share::schema::ObSimpleTablegroupSchema &tg,
      const int64_t max_part_level,
      const int64_t part_group_index = 0);
  int init_by_table(const share::schema::ObSimpleTableSchemaV2 &table_schema,
      const share::schema::ObPartition *partition = NULL);

  int hash(uint64_t &res) const;
  bool operator ==(const ObBalanceGroup &other) const {
    return id_ == other.id_;
  }
  bool operator !=(const ObBalanceGroup &other) const {
    return !(*this == other);
  }

  TO_STRING_KV(K_(id), K_(name));
public:
  const static char* NON_PART_BG_NAME;
  const static char* DUP_TABLE_BG_NAME;
private:
  ObBalanceGroupID id_;
  ObBalanceGroupName name_;
  // TODO: add type
};

}//end namespace rootserver
}//end namespace oceanbase

#endif // OCEANBASE_ROOTSERVER_OB_BALANCE_GROUP_DEFINE_H_
