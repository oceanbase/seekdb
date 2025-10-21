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

#ifndef OCEABASE_STORAGE_OB_LOB_META_MANAGER_
#define OCEABASE_STORAGE_OB_LOB_META_MANAGER_

#include <cstdint>
#include "lib/ob_errno.h"
#include "storage/lob/ob_lob_access_param.h"
#include "storage/blocksstable/ob_datum_row_iterator.h"
#include "storage/lob/ob_lob_persistent_adaptor.h"

namespace oceanbase
{
namespace storage
{
class ObLobMetaSingleGetter;
class ObLobMetaInfo;
class ObLobMetaWriteIter;
class ObLobMetaScanIter;
class ObLobMetaManager {
public:
  explicit ObLobMetaManager(const uint64_t tenant_id) : 
    persistent_lob_adapter_(tenant_id)
  {}
  ~ObLobMetaManager() {}
  // write one lob meta row
  int write(ObLobAccessParam& param, ObLobMetaInfo& in_row);
  int batch_insert(ObLobAccessParam& param, blocksstable::ObDatumRowIterator &iter);
  int batch_delete(ObLobAccessParam& param, blocksstable::ObDatumRowIterator &iter);
  // append
  int append(ObLobAccessParam& param, ObLobMetaWriteIter& iter);
  // return ObLobMetaWriteResult
  // specified range rebuild
  // specified range LobMeta scan
  int scan(ObLobAccessParam& param, ObLobMetaScanIter &iter);
  // specified range erase
  int erase(ObLobAccessParam& param, ObLobMetaInfo& in_row);
  // specified range update
  int update(ObLobAccessParam& param, ObLobMetaInfo& old_row, ObLobMetaInfo& new_row);
  // fetch lob id
  int fetch_lob_id(ObLobAccessParam& param, uint64_t &lob_id);

  int open(ObLobAccessParam &param, ObLobMetaSingleGetter* getter);
  int getlength(ObLobAccessParam &param, uint64_t &char_len);

  TO_STRING_KV("[LOB]", "meta mngr");

private:
  int local_scan(ObLobAccessParam& param, ObLobMetaScanIter &iter);
  int remote_scan(ObLobAccessParam& param, ObLobMetaScanIter &iter);

  int getlength_local(ObLobAccessParam &param, uint64_t &char_len);
  int getlength_remote(ObLobAccessParam &param, uint64_t &char_len);

private:
  // lob adaptor
  ObPersistentLobApator persistent_lob_adapter_;
};


} // storage
} // oceanbase

#endif
