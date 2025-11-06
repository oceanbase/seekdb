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

#ifndef OCEANBASE_STORAGE_OB_TABLET_ITERATOR
#define OCEANBASE_STORAGE_OB_TABLET_ITERATOR

#include <stdint.h>
#include "lib/container/ob_se_array.h"
#include "lib/utility/ob_print_utils.h"
#include "common/ob_tablet_id.h"
#include "share/ob_ls_id.h"
#include "storage/tablet/ob_tablet_common.h"
#include "storage/meta_mem/ob_tablet_pointer.h"

namespace oceanbase
{
namespace storage
{
struct ObMetaDiskAddr;
class ObLSTabletService;
class ObTabletHandle;
class ObTabletMapKey;

class ObLSTabletIterator final
{
  friend class ObLSTabletService;
public:
  // mode only affect get_next_tablet interface, the other three get_xxx interfaces
  // retrieve data from ObTabletPointer thus doesn't need ObMDSGetTabletMode
  explicit ObLSTabletIterator(const ObMDSGetTabletMode mode);
  ~ObLSTabletIterator();
  ObLSTabletIterator(const ObLSTabletIterator&) = delete;
  ObLSTabletIterator &operator=(const ObLSTabletIterator&) = delete;
public:
  int get_next_tablet(ObTabletHandle &handle);
  int get_next_ddl_kv_mgr(ObDDLKvMgrHandle &handle);
  int get_tablet_ids(ObIArray<common::ObTabletID> &ids) const;

  void reset();
  bool is_valid() const;

  TO_STRING_KV(KP_(ls_tablet_service), K_(tablet_ids), K_(idx), K_(mode));
private:
  ObLSTabletService *ls_tablet_service_;
  common::ObSEArray<common::ObTabletID, ObTabletCommon::DEFAULT_ITERATOR_TABLET_ID_CNT> tablet_ids_;
  int64_t idx_;
  ObMDSGetTabletMode mode_;
};

class ObLSTabletAddrIterator final
{
  friend class ObLSTabletService;
public:
  // iter all tablets' addr without filter
  ObLSTabletAddrIterator();
  ~ObLSTabletAddrIterator();
  ObLSTabletAddrIterator(const ObLSTabletAddrIterator&) = delete;
  ObLSTabletAddrIterator &operator=(const ObLSTabletAddrIterator&) = delete;
public:
  int get_next_tablet_addr(ObTabletMapKey &key, ObMetaDiskAddr &addr);
  void reset() { 
    ls_tablet_service_ = nullptr; 
    idx_ = -1;
    tablet_ids_.reset();
  }
  bool is_valid() const { return nullptr != ls_tablet_service_; }

  TO_STRING_KV(KP_(ls_tablet_service), K_(tablet_ids), K_(idx));
private:
  ObLSTabletService *ls_tablet_service_;
  common::ObSEArray<common::ObTabletID, ObTabletCommon::DEFAULT_ITERATOR_TABLET_ID_CNT> tablet_ids_;
  int64_t idx_;
};

class ObHALSTabletIDIterator final
{
  friend class ObLSTabletService;
public:
  ObHALSTabletIDIterator(
      const share::ObLSID &ls_id,
      const bool need_initial_state,
      const bool need_sorted_tablet_id);
  ~ObHALSTabletIDIterator();
  ObHALSTabletIDIterator(const ObHALSTabletIDIterator&) = delete;
  ObHALSTabletIDIterator &operator=(const ObHALSTabletIDIterator&) = delete;
public:
  int get_next_tablet_id(common::ObTabletID &tablet_id);

  void reset();
  bool is_valid() const;

  TO_STRING_KV(K_(ls_id), K_(tablet_ids), K_(idx));

private:
  int sort_tablet_ids_if_need();

private:
  share::ObLSID ls_id_;
  common::ObSEArray<common::ObTabletID, ObTabletCommon::DEFAULT_ITERATOR_TABLET_ID_CNT> tablet_ids_;
  int64_t idx_;
  const bool need_initial_state_;
  const bool need_sorted_tablet_id_;
};


class ObHALSTabletIterator final
{
  friend class ObLSTabletService;
public:
  ObHALSTabletIterator(const share::ObLSID &ls_id,
                       const bool need_initial_state,
                       const bool need_sorted_tablet_id);
  ~ObHALSTabletIterator();
  ObHALSTabletIterator(const ObHALSTabletIterator&) = delete;
  ObHALSTabletIterator &operator=(const ObHALSTabletIterator&) = delete;
public:
  int get_next_tablet(ObTabletHandle &handle);

  void reset();

  TO_STRING_KV(KP_(ls_tablet_service), K_(tablet_id_iter));
private:
  ObLSTabletService *ls_tablet_service_;
  ObHALSTabletIDIterator tablet_id_iter_;
};

class ObLSTabletFastIter final
{
  friend class ObLSTabletService;
public:
  ObLSTabletFastIter(ObITabletFilterOp &op,
                     const ObMDSGetTabletMode mode);
  ~ObLSTabletFastIter() = default;
  bool is_valid() const;
  void reset();
  TO_STRING_KV(K_(idx), K_(mode));
private:
  ObLSTabletService *ls_tablet_service_;
  common::ObSEArray<common::ObTabletID, ObTabletCommon::DEFAULT_ITERATOR_TABLET_ID_CNT> tablet_ids_;
  int64_t idx_;
  ObMDSGetTabletMode mode_;
  ObITabletFilterOp &op_;
  DISALLOW_COPY_AND_ASSIGN(ObLSTabletFastIter);
};




} // namespace storage
} // namespace oceanbase

#endif // OCEANBASE_STORAGE_OB_TABLET_ITERATOR
