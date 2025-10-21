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

#pragma once

#include "storage/ob_i_table.h"

namespace oceanbase {

namespace share{
class SCN;
}

namespace storage {
class ObTablet;
class ObDDLKV;

class ObDirectLoadTableGuard {
private:
  static const int64_t MAX_RETRY_CREATE_MEMTABLE_TIME = 1LL * 1000LL * 1000LL; // 1 second

public:
  DISABLE_COPY_ASSIGN(ObDirectLoadTableGuard);
  ObDirectLoadTableGuard(ObTablet &tablet, const share::SCN &scn, const bool for_replay);
  ~ObDirectLoadTableGuard() { reset(); }
  void reset();
  void clear_write_ref(ObIArray<ObTableHandleV2> &table_handles);
  int prepare_memtable(ObDDLKV *&res_memtable);

  bool is_write_filtered() { return is_write_filtered_; }

  TO_STRING_KV(KP(this),
               K(has_acquired_memtable_),
               K(is_write_filtered_),
               K(for_replay_),
               K(ls_id_),
               K(tablet_id_),
               K(ddl_redo_scn_),
               K(table_handle_),
               K(construct_timestamp_));

private:
  void async_freeze_();
  int acquire_memtable_once_();
  int do_create_memtable_(ObLSHandle &ls_handle);
  int try_get_direct_load_memtable_for_write(ObLSHandle &ls_handle, bool &need_create_new_memtable);

private:
  bool has_acquired_memtable_;
  bool is_write_filtered_;
  const bool for_replay_;
  share::ObLSID ls_id_;
  ObTabletID tablet_id_;
  const share::SCN &ddl_redo_scn_;
  ObTableHandleV2 table_handle_; 
  int64_t construct_timestamp_;
};

}  // namespace storage
}  // namespace oceanbase
