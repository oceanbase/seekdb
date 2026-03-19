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
#ifndef OCEANBASE_LOGSERVICE_OB_LOG_ARCHIVE_PIECE_MGR_H_
#define OCEANBASE_LOGSERVICE_OB_LOG_ARCHIVE_PIECE_MGR_H_

#include "lib/container/ob_iarray.h"
#include "lib/container/ob_se_array.h"
#include "lib/ob_errno.h"
#include "lib/utility/ob_print_utils.h"
#include "logservice/palf/lsn.h"
#include "share/scn.h"
#include "share/backup/ob_backup_struct.h"
#include "share/ob_ls_id.h"
#include "ob_log_restore_define.h"
#include <cstdint>
namespace oceanbase
{
namespace share
{
class ObArchiveLSMetaType;
}
namespace logservice
{
// Log Archive Dest is the destination for Archive and the source For Restore and Standby.
// In archive destination, logs are organized inner rounds and pieces.
//
// Round is a continuous interval of all log streams, while piece is an interval of a specified length of time
// Through parsing rounds and pieces, locate the specified log with the scn of its previous log
//
// As rounds may be continuous with previous one, forward round is necessary.

class ObLogArchivePieceContext
{
public:
  ObLogArchivePieceContext();
  ~ObLogArchivePieceContext();

public:
  // standard archive path interface
  int init(const share::ObLSID &id,
      const share::ObBackupDest &archive_dest);

  void reset();

  bool is_valid() const;

  int get_raw_read_piece(const share::SCN &pre_scn,
      const palf::LSN &start_lsn,
      int64_t &dest_id,
      int64_t &round_id,
      int64_t &piece_id,
      int64_t &file_id,
      int64_t &file_offset);

  int get_piece(const share::SCN &pre_scn,
      const palf::LSN &start_lsn,
      int64_t &dest_id,
      int64_t &round_id,
      int64_t &piece_id,
      int64_t &file_id,
      int64_t &offset,
      palf::LSN &max_lsn,
      bool &to_newest);

  // For convenience of continuous consumption of active files, update consumed records to piece context
  int update_file_info(const int64_t dest_id,
      const int64_t round_id,
      const int64_t piece_id,
      const int64_t file_id,
      const int64_t file_offset,
      const palf::LSN &max_lsn);

  void get_max_file_info(int64_t &dest_id,
      int64_t &round_id,
      int64_t &piece_id,
      int64_t &max_file_id,
      int64_t &max_file_offset,
      palf::LSN &max_lsn);

  int deep_copy_to(ObLogArchivePieceContext &other);

  void reset_locate_info();

  int get_max_archive_log(palf::LSN &lsn, share::SCN &scn);

  // @brief locate the first log group entry whose log_scn equal or bigger than the input param scn
  // return the start lsn of the first log group entry
  // @param[in] scn, the pointed scn
  // @param[out] lsn, the start lsn of the returned log
  //
  // @ret_code    OB_SUCCESS    seek succeed
  //              OB_ENTRY_NOT_EXIST  log not exist
  //              other code    seek fail
  int seek(const share::SCN &scn, palf::LSN &lsn);

  // @brief get ls meta data by meta_type, including schema_meta...
  // @param[in] buf, buffer to cache read_data
  // @param[in] buf_size, the size of buffer, suggest 2MB
  // @param[in] meta_type, the type of ls meta, for example schema_meta
  // @param[in] timestamp, the upper limit meta version you want
  // @param[in] fuzzy_match, if the value if true, return the maximum version not bigger than the input timestamp;
  //            otherwise return the special version equals to the input timestamp, if the version not exists, return error_code
  // @param[out] real_size, the real_size of ls meta data
  //
  // @ret_code  OB_SUCCESS     get_data succeed
  //            OB_ENTRY_NOT_EXIST no suitable version exists
  //            other code     error unexpected
  int get_ls_meta_data(const share::ObArchiveLSMetaType &meta_type,
      const share::SCN &timestamp,
      char *buf,
      const int64_t buf_size,
      int64_t &real_size,
      const bool fuzzy_match = true);

  TO_STRING_KV(K_(is_inited), K_(locate_round), K_(id), K_(dest_id), K_(round_context),
      K_(min_round_id), K_(max_round_id), K_(archive_dest), K_(inner_piece_context));

private:
  enum class RoundOp
  {
    NONE = 0,        // no operation
    LOAD_RANGE = 1,  // get round range
    LOCATE = 2,      // locate the inital round with pre log scn
    LOAD = 3,        // get meta info of round, include piece range, scn range
    FORWARD = 4,     // switch round to the next
    BACKWARD = 5,    // switch round to the previous
  };

  enum class PieceOp {
    NONE = 0,      //  no operation
    LOAD = 1,      // refresh current piece info
    ADVANCE = 2,  // advance file range or piece status if piece is active
    FORWARD = 3,   // forward switch piece to next one(+1)
    BACKWARD = 4,  // backward switch piece to pre one(-1)
  };

  struct RoundContext
  {
    enum State : uint8_t // FARM COMPAT WHITELIST
    {
      INVALID = 0,
      ACTIVE = 1,
      STOP = 2,
      EMPTY = 3,
    };

    State state_;
    int64_t round_id_;
    share::SCN start_scn_;              // Minimum log SCN of this round
    share::SCN end_scn_;                // Maximum log SCN of this round, for non-STOP state this time is SCN_MAX
    int64_t min_piece_id_;          // Minimum piece of this round, this value may increase as archive data is recycled
    int64_t max_piece_id_;          // Maximum piece of this round, for ACTIVE round this value may increase

    int64_t base_piece_id_;
    int64_t piece_switch_interval_;//us
    share::SCN base_piece_scn_;

    RoundContext() { reset(); }
    ~RoundContext() { reset(); }

    void reset();
    bool is_valid() const;
    bool is_in_stop_state() const;
    bool is_in_empty_state() const;
    bool is_in_active_state() const;
    bool check_round_continuous_(const RoundContext &pre_round) const;

    RoundContext &operator=(const RoundContext &other);

    TO_STRING_KV(K_(state), K_(round_id), K_(start_scn), K_(end_scn), K_(min_piece_id), K_(max_piece_id),
        K_(base_piece_id), K_(piece_switch_interval), K_(base_piece_scn));
  };

  // Read a piece that is already in frozen state, and have finished reading the last file of this piece, then the piece file range information
  // and the maximum LSN contained in the last file are all determined, if still need to read, then switch piece
  struct InnerPieceContext
  {
    enum State : uint8_t
    {
      INVALID = 0,
      FROZEN = 1,       // Normal FROZEN state piece, contains log and meta files
      EMPTY = 2,        // Current piece is empty, only has meta information without logs, its log range is determined, a special case of FROZEN state
      LOW_BOUND = 3,    // Piece exists and is FROZEN, but log stream has not been generated in this piece yet, another special case of FROZEN state
      ACTIVE = 4,       // Current piece is not frozen, still has logs being written
      GC = 5,           // Log stream in this piece is already GC, no need to switch piece forward again, also a special case of FROZEN state
    };

    State state_;                 // Current piece state
    int64_t piece_id_;            // Current piece id
    int64_t round_id_;            // Round this piece belongs to

    palf::LSN min_lsn_in_piece_;  // Minimum LSN in piece
    palf::LSN max_lsn_in_piece_;  // Maximum LSN in piece, only valid for FROZEN state pieces
    int64_t min_file_id_;         // Maximum file id in current piece
    int64_t max_file_id_;         // Minimum file id in current piece

    int64_t file_id_;         // Maximum file id of files read in current piece
    int64_t file_offset_;     // Maximum offset in files read in current piece
    palf::LSN max_lsn_;       // Log LSN corresponding to maximum file offset read in current piece

    InnerPieceContext() { reset(); }

    void reset();
    bool is_valid() const;
    bool is_frozen_() const { return State::FROZEN == state_; }
    bool is_empty_() const { return State::EMPTY == state_; }
    bool is_low_bound_() const { return State::LOW_BOUND == state_; }
    bool is_gc_() const { return State::GC == state_; }
    bool is_active() const { return State::ACTIVE == state_; }
    int update_file(const int64_t file_id, const int64_t file_offset, const palf::LSN &lsn);
    InnerPieceContext  &operator=(const InnerPieceContext &other);

    TO_STRING_KV(K_(state), K_(piece_id), K_(round_id), K_(min_lsn_in_piece), K_(max_lsn_in_piece),
        K_(min_file_id), K_(max_file_id), K_(file_id), K_(file_offset), K_(max_lsn));
  };

private:
  int get_piece_(const share::SCN &scn,
      const palf::LSN &lsn,
      const int64_t file_id,
      int64_t &dest_id,
      int64_t &round_id,
      int64_t &piece_id,
      int64_t &offset,
      palf::LSN &max_lsn,
      bool &to_newest);

  // Get basic meta information of archive source
  virtual int load_archive_meta_();

  // Get round range
  virtual int get_round_range_();

  // Locate round based on timestamp
  virtual int get_round_(const share::SCN &scn);

  // If round does not meet data requirements, support switching round
  int switch_round_if_need_(const share::SCN &scn, const palf::LSN &lsn);

  void check_if_switch_round_(const share::SCN &scn, const palf::LSN &lsn, RoundOp &op);
  bool is_max_round_done_(const palf::LSN &lsn) const;
  bool need_backward_round_(const palf::LSN &lsn) const;
  bool need_forward_round_(const palf::LSN &lsn) const;
  bool need_load_round_info_(const share::SCN &scn, const palf::LSN &lsn) const;

  // Get specified round meta information
  virtual int load_round_(const int64_t round_id, RoundContext &round_context, bool &exist);

  // Rounds may be discontinuous, check if round exists
  virtual int check_round_exist_(const int64_t round_id, bool &exist);

  int load_round_info_();
  virtual int get_round_piece_range_(const int64_t round_id, int64_t &min_piece_id, int64_t &max_piece_id);

  // Forward and backward round switching
  int forward_round_(const RoundContext &pre_round);
  int backward_round_();

  // When piece does not match, need to switch piece
  int switch_piece_if_need_(const int64_t file_id, const share::SCN &scn, const palf::LSN &lsn);
  void check_if_switch_piece_(const int64_t file_id, const palf::LSN &lsn, PieceOp &op);

  // Load current piece information, including file range and LSN range in piece
  int get_cur_piece_info_(const share::SCN &scn);
  int advance_piece_();
  virtual int get_piece_meta_info_(const int64_t piece_id);
  int get_ls_inner_piece_info_(const share::ObLSID &id, const int64_t dest_id, const int64_t round_id,
      const int64_t piece_id, palf::LSN &min_lsn, palf::LSN &max_lsn, bool &exist, bool &gc);
  virtual int get_piece_file_range_();

  int forward_piece_();
  int backward_piece_();
  int cal_load_piece_id_(const share::SCN &scn, int64_t &piece_id);

  int64_t cal_piece_id_(const share::SCN &scn) const;
  virtual int get_min_lsn_in_piece_();
  int64_t cal_archive_file_id_(const palf::LSN &lsn) const;

  int get_(const palf::LSN &lsn,
      const int64_t file_id,
      int64_t &dest_id,
      int64_t &round_id,
      int64_t &piece_id,
      int64_t &offset,
      palf::LSN &max_lsn,
      bool &done,
      bool &to_newest);

  int get_max_archive_log_(const ObLogArchivePieceContext &origin, palf::LSN &lsn, share::SCN &scn);

  int get_max_log_in_round_(const ObLogArchivePieceContext &origin,
      const int64_t round_id,
      palf::LSN &lsn,
      share::SCN &scn,
      bool &exist);

  int get_max_log_in_piece_(const ObLogArchivePieceContext &origin,
      const int64_t round_id,
      const int64_t piece_id,
      palf::LSN &lsn,
      share::SCN &scn,
      bool &exist);

  int get_max_log_in_file_(const ObLogArchivePieceContext &origin,
      const int64_t round_id,
      const int64_t piece_id,
      const int64_t file_id,
      palf::LSN &lsn,
      share::SCN &scn,
      bool &exist);

  int seek_(const share::SCN &scn, palf::LSN &lsn);
  int seek_in_piece_(const share::SCN &scn, palf::LSN &lsn);
  int seek_in_file_(const int64_t file_id, const share::SCN &scn, palf::LSN &lsn);

  int read_part_file_(const int64_t round_id,
      const int64_t piece_id,
      const int64_t file_id,
      const int64_t file_offset,
      char *buf,
      const int64_t buf_size,
      int64_t &read_size);

  int extract_file_base_lsn_(const char *buf,
      const int64_t buf_size,
      palf::LSN &base_lsn);
  int get_ls_meta_data_(const share::ObArchiveLSMetaType &meta_type,
      const share::SCN &timestamp,
      const bool fuzzy_match,
      char *buf,
      const int64_t buf_size,
      int64_t &real_size);
  int get_ls_meta_in_piece_(const share::ObArchiveLSMetaType &meta_type,
      const share::SCN &timestamp,
      const bool fuzzy_match,
      const int64_t base_piece_id,
      char *buf,
      const int64_t buf_size,
      int64_t &real_size);
  int get_ls_meta_file_in_array_(const share::SCN &timestamp,
      const bool fuzzy_match,
      int64_t &file_id,
      common::ObIArray<int64_t> &array);

private:
  bool is_inited_;
  bool locate_round_;
  share::ObLSID id_;
  int64_t dest_id_;
  int64_t min_round_id_;
  int64_t max_round_id_;
  RoundContext round_context_;
  InnerPieceContext inner_piece_context_;

  share::ObBackupDest archive_dest_;
};

class ObLogRawPathPieceContext
{
  class GetFileEndLSN
  {
    public:
    palf::LSN operator()() const {return palf::LSN(palf::LOG_MAX_LSN_VAL);}
  };

public:
  ObLogRawPathPieceContext();
  ~ObLogRawPathPieceContext();

public:
  int init(const share::ObLSID &id, const DirArray &array);
  void reset();
  bool is_valid() const;
  int deep_copy_to(ObLogRawPathPieceContext &other);
  int get_cur_uri(char *buf, const int64_t buf_size);
  int get_cur_storage_info(char *buf, const int64_t buf_size);
  int get_file_id(int64_t &file_id);
  int locate_precise_piece(palf::LSN &fetch_lsn);
  int list_dir_files(const ObString &base,
    const share::ObBackupStorageInfo *storage_info,
    int64_t &min_file_id,
  int64_t &max_file_id);
  int get_ls_piece_info(const int64_t curr_index, bool &exist);
  int get_max_lsn(palf::LSN &lsn);
  bool piece_index_match(const palf::LSN &lsn) const;
  int cal_lsn_to_file_id(const palf::LSN &lsn);
  int update_file(const int64_t file_id,
    const int64_t file_offset,
    const palf::LSN &lsn);
  int update_max_lsn(const palf::LSN &lsn);
  int update_min_lsn(const palf::LSN &lsn);
  int get_max_archive_log(palf::LSN &lsn, share::SCN &scn);

  TO_STRING_KV(K_(is_inited), K_(id), K_(array), K_(index), K_(file_id),
      K_(min_file_id), K_(max_file_id), K_(min_lsn), K_(max_lsn), K_(file_offset));

private:
  int get_max_archive_log_(const ObLogRawPathPieceContext &origin, palf::LSN &lsn, share::SCN &scn);
  int get_max_log_in_file_(const ObLogRawPathPieceContext &origin,
    const int64_t file_id,
    palf::LSN &lsn,
    share::SCN &scn);
  int extract_file_base_lsn_(const char *buf,
    const int64_t buf_size,
    palf::LSN &base_lsn);
  int read_part_file_(const int64_t file_id,
    const int64_t file_offset,
    char *buf,
    const int64_t buf_size,
    int64_t &read_size);

private:
  bool is_inited_;
  share::ObLSID id_;
  DirArray array_;        // piece list
  int64_t index_;         // current read piece index
  int64_t file_id_;       // current read file id
  int64_t min_file_id_;   // min file id in current piece
  int64_t max_file_id_;   // max file id in current piece
  palf::LSN min_lsn_;     // min lsn in current piece
  palf::LSN max_lsn_;     // max lsn in current piece
  int64_t file_offset_;     // Maximum offset in files read in current piece
};

} // namespace logservice
} // namespace oceanbase
#endif /* OCEANBASE_LOGSERVICE_OB_LOG_ARCHIVE_PIECE_MGR_H_ */
