/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
 */

#ifndef OCEANBASE_STORAGE_OB_TABLET_DDL_COMPLETE_MDS_DATA
#define OCEANBASE_STORAGE_OB_TABLET_DDL_COMPLETE_MDS_DATA

#include <stdint.h>
#include "lib/utility/ob_print_utils.h"
#include "share/ob_ls_id.h"
#include "share/scn.h"
#include "common/ob_tablet_id.h"
#include "storage/tablet/ob_tablet_common.h"
#include "storage/ob_i_table.h"
#include "storage/ddl/ob_ddl_struct.h"

namespace oceanbase
{
namespace storage
{
class ObDDLTableMergeDagParam;
class ObTabletDDLCompleteArg;
class ObTabletDDLCompleteMdsUserData
{
public:
  ObTabletDDLCompleteMdsUserData();
  ~ObTabletDDLCompleteMdsUserData();
  void reset();
  bool is_valid() const ;
  int assign(const ObTabletDDLCompleteMdsUserData &other);
  int generate_merge_param(ObDDLTableMergeDagParam &merge_param);
  int set_with_merge_arg(const ObTabletDDLCompleteArg &merge_param);
  int set_storage_schema(const ObStorageSchema &other);
  ObStorageSchema &get_storage_schema() { return storage_schema_; }
  int serialize(char *buf, const int64_t buf_len, int64_t &pos) const;
  int deserialize(const char *buf, const int64_t data_len, int64_t &pos);
  int64_t get_serialize_size() const;
  TO_STRING_KV(K_(has_complete), K_(direct_load_type), K_(has_complete),
               K_(data_format_version), K_(snapshot_version),
               K_(table_key), K_(write_stat), K_(storage_schema));
public:
  bool has_complete_;
  /* for merge param */
  ObDirectLoadType direct_load_type_;
  uint64_t data_format_version_;
  int64_t snapshot_version_;
  ObITable::TableKey table_key_;
  ObStorageSchema storage_schema_;
  ObDDLWriteStat write_stat_;
  ObArenaAllocator allocator_;
};
} // namespace storage
} // namespace oceanbase

#endif // OCEANBASE_STORAGE_OB_TABLET_CREATE_DELETE_MDS_USER_DATA
