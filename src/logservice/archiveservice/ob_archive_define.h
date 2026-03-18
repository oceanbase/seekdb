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

#ifndef OCEANBASE_ARCHIVE_OB_ARCHIVE_DEFINE_H_
#define OCEANBASE_ARCHIVE_OB_ARCHIVE_DEFINE_H_

#include "lib/ob_define.h"                  // int64_t
#include "lib/utility/ob_print_utils.h"     // print
#include "logservice/palf/log_define.h"     // PALF_BLOCK_SIZE
#include "share/backup/ob_archive_piece.h"  // ObArchivePiece
#include "share/backup/ob_backup_struct.h"  // ObBackupPathString
#include "logservice/palf/lsn.h"            // LSN
#include "share/scn.h"            // share::SCN

namespace oceanbase
{
namespace share
{
class SCN;
}
namespace archive
{
using oceanbase::palf::LSN;
using oceanbase::share::ObArchivePiece;

// =================== const define ================= //
const int64_t OB_INVALID_ARCHIVE_INCARNATION_ID = -1;
const int64_t OB_INVALID_ARCHIVE_DEST_ID = -1;
const int64_t OB_INVALID_ARCHIVE_ROUND_ID = -1;
const int64_t OB_INVALID_ARCHIVE_PIECE_ID = -1;
const int64_t OB_INVALID_ARCHIVE_LEASE_ID = -1;
// The archive file size is N times the CLOG virtual file size, N must be at least 1
const int64_t ARCHIVE_N = 1;
const int64_t MAX_LOG_FILE_SIZE = palf::PALF_BLOCK_SIZE;  // 64M - 16K
// Archive log file size, is an integer multiple of the OB log file, defaults to the size of the OB log file
const int64_t MAX_ARCHIVE_FILE_SIZE = ARCHIVE_N * MAX_LOG_FILE_SIZE;
const int64_t OB_INVALID_ARCHIVE_FILE_ID = 0;
const int64_t OB_INVALID_ARCHIVE_FILE_OFFSET = -1;
const int64_t COMMON_HEADER_SIZE = 4 * 1024L;    // 4K
const int64_t ARCHIVE_FILE_HEADER_SIZE = COMMON_HEADER_SIZE;
const int64_t DEFAULT_ARCHIVE_UNIT_SIZE = 16 * 1024L;   // archive compression encryption unit size
const int64_t ARCHIVE_FILE_DATA_BUF_SIZE = MAX_ARCHIVE_FILE_SIZE + ARCHIVE_FILE_HEADER_SIZE;

const int64_t DEFAULT_MAX_LOG_SIZE = palf::MAX_LOG_BUFFER_SIZE;
const int64_t MAX_FETCH_TASK_NUM = 4;

const int64_t MIN_FETCHER_THREAD_COUNT = 1;
const int64_t MAX_FETCHER_THREAD_COUNT = 3;

const int64_t DEFAULT_LS_RECORD_INTERVAL = 30 * 60 * 1000 * 1000L;   // 30min
const int64_t MAX_META_RECORD_DATA_SIZE = 2 * 1024 * 1024L;
const int64_t MAX_META_RECORD_FILE_SIZE = COMMON_HEADER_SIZE + MAX_META_RECORD_DATA_SIZE;  // 2M + 4K

const int64_t MAX_LS_ARCHIVE_MEMORY_LIMIT = 4 * MAX_LOG_FILE_SIZE;
const int64_t MAX_LS_SEND_TASK_COUNT_LIMIT = 6;
// ================================================= //
// Log stream leader authorization backup zone server archive, leader through lease mechanism issues authorization to server
// Among them, the log stream copy server has a higher selection priority
//
// lease timeout is 30s, leader renews lease with server every 5s, server authorizes the log stream archiving service for 30s after receiving authorization
// If no renewal authorization is received within 30s, the server will no longer serve this log stream and discard existing tasks
//
// lease_id and lease_start_ts together indicate the range of a continuous lease, only a change in lease_id or lease_start_ts indicates that a different lease is in effect on this server
//
// When the leader does not receive an authorization response for a long time or is rejected by the server for providing the log stream archiving service, the leader will wait for the lease to expire and wait for a sufficiently long safe time afterwards,
// Select other server as the archive service for this log stream
class ObArchiveLease
{
public:
  ObArchiveLease();
  ObArchiveLease(const int64_t lease_id, const int64_t start_ts, const int64_t end_ts);
  ~ObArchiveLease();

public:
  bool is_valid() const;
  void reset();
  ObArchiveLease &operator=(const ObArchiveLease &other);
  bool operator==(const ObArchiveLease &other) const;
  TO_STRING_KV(K_(lease_id), K_(lease_start_ts), K_(lease_end_ts));

public:
  // TODO Consider using election epoch + server internal incrementing sequence number
  int64_t         lease_id_;        // uniquely identifies lease
  int64_t         lease_start_ts_;  // indicates the time when the lease becomes effective
  int64_t         lease_end_ts_;    // indicates the timeout time of the effective lease
};
// Represent archive internal progress with LSN, corresponding log commit timestamp, and belonging piece
class LogFileTuple
{
public:
  LogFileTuple();
  LogFileTuple(const LSN &lsn, const share::SCN &scn, const ObArchivePiece &piece);
  ~LogFileTuple();

public:
  bool is_valid() const;
  void reset();
  bool operator < (const LogFileTuple &other) const;
  LogFileTuple &operator=(const LogFileTuple &other);

  const LSN &get_lsn() const { return offset_; }
  const share::SCN &get_scn() const { return scn_; }
  const ObArchivePiece &get_piece() const { return piece_; };
  void compensate_piece();
  TO_STRING_KV(K_(offset), K_(scn), K_(piece));

private:
  LSN offset_;            // file offset
  share::SCN scn_;        // maximum log scn
  ObArchivePiece piece_;  // belonging piece
};

struct ArchiveKey
{
  int64_t incarnation_;
  int64_t dest_id_;
  int64_t round_;
  ArchiveKey();
  ArchiveKey(const int64_t incarnation, const int64_t dest_id, const int64_t round);
  ~ArchiveKey();
  void reset();
  bool is_valid() const;
  bool operator==(const ArchiveKey &other) const;
  bool operator!=(const ArchiveKey &other) const;
  ArchiveKey &operator=(const ArchiveKey &other);
  TO_STRING_KV(K_(incarnation), K_(dest_id), K_(round));
};
// Identify an archive Task uniquely by incarnation/round/Lease, expired Tasks are invalid tasks
class ArchiveWorkStation
{
public:
  ArchiveWorkStation();
  ArchiveWorkStation(const ArchiveKey &key, const ObArchiveLease &lease);
  ~ArchiveWorkStation();

public:
  bool is_valid() const;
  void reset();
  const ArchiveKey &get_round() const { return key_; }
  const ObArchiveLease &get_lease() const { return lease_; }
  ArchiveWorkStation &operator=(const ArchiveWorkStation &other);
  bool operator==(const ArchiveWorkStation &other) const;
  bool operator!=(const ArchiveWorkStation &other) const;
  TO_STRING_KV(K_(key), K_(lease));

private:
  ArchiveKey        key_;
  ObArchiveLease    lease_;
};

struct ObArchiveSendDestArg
{
  int64_t cur_file_id_;
  int64_t cur_file_offset_;
  LogFileTuple tuple_;
  bool piece_dir_exist_;
};

// file header for common archive log file
struct ObArchiveFileHeader
{
  int16_t magic_;                    // FH
  int16_t version_;
  int32_t flag_;//for compression and encrytion and so on
  int64_t unit_size_;
  int64_t start_lsn_;
  int64_t checksum_;

  bool is_valid() const;
  int generate_header(const LSN &lsn);
  NEED_SERIALIZE_AND_DESERIALIZE;
  TO_STRING_KV(K_(magic),
               K_(version),
               K_(flag),
               K_(unit_size),
               K_(start_lsn),
               K_(checksum));

private:
  static const int16_t ARCHIVE_FILE_HEADER_MAGIC = 0x4648; // FH means archive file header
};

// file header for ls meta file
struct ObLSMetaFileHeader
{
  int16_t magic_;
  int16_t version_;
  int32_t place_holder_;
  share::SCN timestamp_;
  int64_t data_checksum_;
  int64_t header_checksum_;

  bool is_valid() const;
  int generate_header(const share::SCN &timestamp, const int64_t data_checksum);
  NEED_SERIALIZE_AND_DESERIALIZE;
  TO_STRING_KV(K_(magic),
               K_(version),
               K_(place_holder),
               K_(timestamp),
               K_(data_checksum),
               K_(header_checksum));

private:
  static const int64_t LS_META_FILE_HEADER_MAGIC = 0x5348; // MH means ls meta file header
};

class ObArchiveInterruptReason
{
public:
  enum Factor
  {
    UNKONWN = 0,
    SEND_ERROR = 1,
    LOG_RECYCLE = 2,
    NOT_CONTINUOUS = 3,
    GC = 4,
    MAX,
  };
  ObArchiveInterruptReason() {}
  ObArchiveInterruptReason(Factor factor, char *lbt_trace, const int ret_code)
    : factor_(factor),
    lbt_trace_(lbt_trace),
    ret_code_(ret_code) {}
  const char *get_str() const;
  const char *get_lbt() const { return lbt_trace_; }
  int get_code() const { return ret_code_; }
  void set(Factor factor, char *lbt_trace, const int ret_code);
  TO_STRING_KV("reason", get_str(), K_(ret_code));
private:
  Factor factor_;
  char *lbt_trace_;
  int ret_code_;
};

} // namespace archive
} // namespace oceanbase
#endif /* OCEANBASE_ARCHIVE_OB_ARCHIVE_DEFINE_H_ */
