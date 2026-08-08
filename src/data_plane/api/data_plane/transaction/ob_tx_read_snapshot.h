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

#ifndef OCEANBASE_DATA_PLANE_API_TRANSACTION_OB_TX_READ_SNAPSHOT_H_
#define OCEANBASE_DATA_PLANE_API_TRANSACTION_OB_TX_READ_SNAPSHOT_H_

#include "share/transaction/ob_tx_id.h"
#include "data_plane/transaction/ob_tx_seq.h"
#include "lib/utility/ob_macro_utils.h"
#include "lib/utility/ob_print_utils.h"
#include "lib/utility/ob_unify_serialize.h"
#include "share/scn.h"

namespace oceanbase
{
namespace transaction
{

class ObTransService;

// Core MVCC snapshot value.  It is part of the query/data-plane protocol, not
// an implementation object owned by either side.
class ObTxSnapshot
{
  friend class ObTxReadSnapshot;
  friend class ObTransService;
public:
  share::SCN version_;
  ObTransID tx_id_;
  ObTxSEQ scn_;
  bool elr_;

  TO_STRING_KV(K_(version), K_(tx_id), K_(scn));
  ObTxSnapshot();
  ObTxSnapshot(const share::SCN &version);
  ~ObTxSnapshot();
  void reset();
  ObTxSnapshot &operator=(const ObTxSnapshot &other);
  bool is_valid() const { return version_.is_valid(); }
  const share::SCN &version() const { return version_; }
  const ObTransID &tx_id() const { return tx_id_; }
  void set_tx_id(const ObTransID &tx_id) { tx_id_ = tx_id; }
  const ObTxSEQ &tx_seq() const { return scn_; }
  void set_elr(bool elr) { elr_ = elr; }
  bool is_elr() const { return elr_; }
  OB_UNIS_VERSION(1);
};

class ObTxReadSnapshot
{
  friend class ObTransService;
public:
  enum class SRC {
    INVL = 0,
    GLOBAL = 1,
    LS = 2,
    WEAK_READ_SERVICE = 3,
    SPECIAL = 4,
    NONE = 5,
  };

  bool valid_;
  bool committed_;
  ObTxSnapshot core_;
  SRC source_;
  int64_t uncertain_bound_;
  bool has_write_state_;

  void init_weak_read(share::SCN snapshot);
  void init_none_read() { valid_ = true; source_ = SRC::NONE; }
  void init_ls_read(const ObTxSnapshot &core);
  void specify_snapshot_scn(share::SCN snapshot);
  void reset_write_state();
  void mark_write_state() { has_write_state_ = true; }
  bool has_write_state() const { return has_write_state_; }
  const char *get_source_name() const;
  const ObTxSnapshot &snapshot() const { return core_; }
  const share::SCN &version() const { return core_.version(); }
  const ObTransID &tx_id() const { return core_.tx_id(); }
  void set_tx_id(const ObTransID &tx_id) { core_.set_tx_id(tx_id); }
  const ObTxSEQ &tx_seq() const { return core_.tx_seq(); }
  bool is_weak_read() const { return SRC::WEAK_READ_SERVICE == source_; }
  bool is_none_read() const { return SRC::NONE == source_; }
  bool is_special() const { return SRC::SPECIAL == source_; }
  bool is_ls_snapshot() const { return SRC::LS == source_; }
  bool is_valid() const { return valid_; }
  void invalid() { valid_ = false; }
  bool is_committed() const { return committed_; }
  void reset();
  int assign(const ObTxReadSnapshot &other);
  void try_set_read_elr();
  bool read_elr() const { return core_.is_elr(); }
  int serialize_for_lob(const share::SCN &fallback_snapshot, SERIAL_PARAMS) const;
  int deserialize_for_lob(share::SCN &fallback_snapshot, DESERIAL_PARAMS);
  int64_t get_serialize_size_for_lob(const share::SCN &fallback_snapshot) const;
  int build_snapshot_for_lob(const ObTxSnapshot &core);
  int build_snapshot_for_lob(int64_t snapshot_version,
                             int64_t snapshot_tx_id,
                             int64_t snapshot_seq);
  bool is_not_in_tx_snapshot() const
  {
    return !core_.tx_id_.is_valid() && !core_.scn_.is_valid() && core_.version_.is_valid();
  }
  int refresh_seq_no(int64_t tx_seq_base);
  ObTxReadSnapshot();
  ~ObTxReadSnapshot();
  TO_STRING_KV(KP(this),
               K_(valid),
               K_(source),
               K_(core),
               K_(uncertain_bound),
               K_(has_write_state),
               K_(committed));
  OB_UNIS_VERSION(1);
  DISABLE_COPY_ASSIGN(ObTxReadSnapshot);
};

} // namespace transaction
} // namespace oceanbase

#endif // OCEANBASE_DATA_PLANE_API_TRANSACTION_OB_TX_READ_SNAPSHOT_H_
