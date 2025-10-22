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
  int assign(ObIAllocator &allocator, const ObTabletDDLCompleteMdsUserData &other);
  int generate_merge_param(ObDDLTableMergeDagParam &merge_param);
  int set_with_merge_arg(const ObTabletDDLCompleteArg &merge_param, ObIAllocator &allocator);
  int set_storage_schema(const ObStorageSchema &other, ObIAllocator &allocator);
  ObStorageSchema &get_storage_schema() { return storage_schema_; }
  int serialize(char *buf, const int64_t buf_len, int64_t &pos) const;
  int deserialize(ObIAllocator &allocator, const char *buf, const int64_t data_len, int64_t &pos);
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
};
} // namespace storage
} // namespace oceanbase

#endif // OCEANBASE_STORAGE_OB_TABLET_CREATE_DELETE_MDS_USER_DATA
