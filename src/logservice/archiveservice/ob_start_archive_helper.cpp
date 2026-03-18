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

#include "ob_start_archive_helper.h"
#include "logservice/ob_log_service.h"      // ObLogService

namespace oceanbase
{
namespace archive
{
using namespace oceanbase::logservice;
using namespace oceanbase::palf;
using namespace oceanbase::share;
StartArchiveHelper::StartArchiveHelper(const ObLSID &id,
    const uint64_t tenant_id,
    const ArchiveWorkStation &station,
    const SCN &min_scn,
    const int64_t piece_interval,
    const SCN &genesis_scn,
    const int64_t base_piece_id,
    ObArchivePersistMgr *persist_mgr)
  : id_(id),
    tenant_id_(tenant_id),
    station_(station),
    log_gap_exist_(false),
    min_scn_(min_scn),
    piece_interval_(piece_interval),
    genesis_scn_(genesis_scn),
    base_piece_id_(base_piece_id),
    max_offset_(),
    start_offset_(),
    archive_file_id_(OB_INVALID_ARCHIVE_FILE_ID),
    archive_file_offset_(OB_INVALID_ARCHIVE_FILE_OFFSET),
    max_archived_scn_(),
    piece_(),
    persist_mgr_(persist_mgr)
{}

StartArchiveHelper::~StartArchiveHelper()
{
  id_.reset();
  tenant_id_ = OB_INVALID_TENANT_ID;
  station_.reset();
  log_gap_exist_ = false;
  min_scn_.reset();
  piece_interval_ = 0;
  genesis_scn_.reset();
  base_piece_id_ = 0;
  max_offset_.reset();
  start_offset_.reset();
  archive_file_id_ = OB_INVALID_ARCHIVE_FILE_ID;
  archive_file_offset_ = OB_INVALID_ARCHIVE_FILE_OFFSET;
  max_archived_scn_.reset();
  piece_.reset();
  persist_mgr_ = NULL;
}

bool StartArchiveHelper::is_valid() const
{
  return id_.is_valid()
    && OB_INVALID_TENANT_ID != tenant_id_
    && station_.is_valid()
    && piece_.is_valid()
    && max_archived_scn_.is_valid()
    && max_offset_.is_valid()
    && (log_gap_exist_
        || (start_offset_.is_valid()
          && OB_INVALID_ARCHIVE_FILE_ID != archive_file_id_
          && OB_INVALID_ARCHIVE_FILE_OFFSET != archive_file_offset_));
}

int StartArchiveHelper::handle()
{
  int ret = OB_SUCCESS;
  int64_t piece_id = 0;
  bool archive_progress_exist = false;

  if (OB_UNLIKELY(! id_.is_valid()
        || ! station_.is_valid()
        || ! min_scn_.is_valid()
        || NULL == persist_mgr_)) {
    ret = OB_INVALID_ARGUMENT;
    ARCHIVE_LOG(WARN, "invalid argumetn", K(ret), K(id_), K(station_), K(persist_mgr_));
  } else if (OB_FAIL(fetch_exist_archive_progress_(archive_progress_exist))) {
    ARCHIVE_LOG(WARN, "fetch exist archive progress failed", K(ret), K(id_));
  } else if (archive_progress_exist) {
  } else if (OB_FAIL(locate_round_start_archive_point_())) {
    ARCHIVE_LOG(WARN, "locate round start archive point failed", K(ret));
  }

  if (OB_SUCC(ret)) {
    palf::PalfHandleGuard guard;
    if (OB_FAIL(MTL(ObLogService*)->open_palf(id_, guard))) {
      ARCHIVE_LOG(WARN, "open_palf failed", K(id_));
    } else if (OB_FAIL(guard.get_end_lsn(max_offset_))) {
      ARCHIVE_LOG(WARN, "get end_lsn failed", K(id_));
    }
  }
  return ret;
}

int StartArchiveHelper::fetch_exist_archive_progress_(bool &record_exist)
{
  int ret = OB_SUCCESS;
  const ArchiveKey &key = station_.get_round();
  ObLSArchivePersistInfo persist_info;
  record_exist = false;

  if (OB_FAIL(persist_mgr_->get_archive_persist_info(id_, key, persist_info))) {
    if (OB_ENTRY_NOT_EXIST == ret) {
      ret = OB_SUCCESS;
      ARCHIVE_LOG(INFO, "no persist archive record exist", K(ret), K(id_), K(station_));
    } else {
      ARCHIVE_LOG(WARN, "load archive persist info failed", K(ret), K(id_), K(station_));
    }
  } else if (OB_UNLIKELY(! persist_info.is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    ARCHIVE_LOG(ERROR, "invalid archive persist info", K(ret), K(persist_info), K(id_));
  } else if (key != ArchiveKey(persist_info.incarnation_, persist_info.key_.dest_id_, persist_info.key_.round_id_)) {
    ARCHIVE_LOG(INFO, "max archive persist info in different round, just skip",
        K(id_), K(station_), K(persist_info));
  } else if (OB_FAIL(cal_archive_file_id_offset_(
                                                 LSN(persist_info.lsn_),
                                                 persist_info.archive_file_id_,
                                                 persist_info.archive_file_offset_))) {
    ARCHIVE_LOG(WARN, "cal archive file id offset failed", K(ret), K(id_), K(persist_info));
  } else {
    record_exist = true;
    piece_min_lsn_ = LSN(persist_info.start_lsn_);
    start_offset_ = LSN(persist_info.lsn_);
    max_archived_scn_ = persist_info.checkpoint_scn_;
    piece_.set(persist_info.key_.piece_id_, piece_interval_, genesis_scn_, base_piece_id_);
    ARCHIVE_LOG(INFO, "fetch exist archive progress succ", KPC(this));
  }
  return ret;
}

int StartArchiveHelper::locate_round_start_archive_point_()
{
  int ret = OB_SUCCESS;
  LSN lsn;
  bool log_gap = false;
  SCN start_scn;

  if (OB_FAIL(get_local_base_lsn_(lsn, log_gap))) {
    ARCHIVE_LOG(WARN, "get local base lsn failed", K(ret));
  } else if (OB_FAIL(get_local_start_scn_(start_scn))) {
    ARCHIVE_LOG(WARN, "get local start scn failed", K(ret));
  } else {
    piece_ = ObArchivePiece(start_scn, piece_interval_, genesis_scn_, base_piece_id_);
    max_archived_scn_ = start_scn;
    // In the scenario of missing logs, still initialize the maximum archive progress with a sufficiently safe value
    // This is for creating a log stream after archiving is enabled, to archive any logs and reclaim them, with a sufficiently large piece_id
    // piece_id is both the primary key of the archive progress table and the benchmark for statistics, which cannot be rolled back
    if (log_gap) {
      ARCHIVE_LOG(ERROR, "locate round start archive point, log gap exist", K(id_), K(min_scn_));
      log_gap_exist_ = log_gap;
    } else if (OB_FAIL(cal_archive_file_id_offset_(lsn, OB_INVALID_ARCHIVE_FILE_ID, 0))) {
      ARCHIVE_LOG(WARN, "cal archive file id and offset failed", K(ret), K_(id));
    } else {
      piece_min_lsn_ = lsn;
      start_offset_ = lsn;
      ARCHIVE_LOG(INFO, "locate_round_start_archive_point_ succ", KPC(this));
    }
  }
  return ret;
}

ERRSIM_POINT_DEF(ERRSIM_LOG_EXIST_GAP);
// Locate the archive starting point based on the log stream
//
// 1. OB_SUCCESS
//    can locate to logs less than or equal to start_scn, will locate to accurate block
//    where offline logs are written and are less than the archive start_scn, it will locate to the last block,
//    For this situation, do not rely on palf implementation, during gc even if there is no archiving progress, rely on the archiving start_scn to check if it can be recycled
//
// 2. OB_ENTRY_NOT_EXIST
//    For newly created log streams that have not written any logs, retrying will suffice
//
// 3. OB_ERR_OUT_OF_LOWER_BOUND
//    All remaining logs in the log stream are greater than start_scn
//    a) If the log stream base_lsn equals 0, it means it is a new log stream, archiving starts from 0
//    b) For log stream base_lsn greater than 0, it indicates that the log has been recycled, and the stream needs to be interrupted
int StartArchiveHelper::get_local_base_lsn_(palf::LSN &lsn, bool &log_gap)
{
  int ret = OB_SUCCESS;
  ObLS *ls = NULL;
  ObLSHandle ls_handle;
  ObLogHandler *log_handler = NULL;
  ObLSService *ls_service = MTL(ObLSService*);
  if (OB_ISNULL(ls_service)) {
    ret = OB_ERR_UNEXPECTED;
    ARCHIVE_LOG(WARN, "ls_service is NULL", K(ret), K_(id), KP(ls), KP(log_handler));
  } else if (OB_FAIL(ls_service->get_ls(id_, ls_handle, ObLSGetMod::ARCHIVE_MOD))) {
    ARCHIVE_LOG(WARN, "get_ls failed", K_(id));
  } else if (OB_ISNULL(ls = ls_handle.get_ls()) || OB_ISNULL(log_handler = ls->get_log_handler())) {
    ret = OB_ERR_UNEXPECTED;
    ARCHIVE_LOG(WARN, "ls or log_handle is NULL", K(ret), K_(id), KP(ls), KP(log_handler));
  } else if (OB_FAIL(log_handler->locate_by_scn_coarsely(min_scn_, lsn))) {
    if (OB_ENTRY_NOT_EXIST == ret) {
      ret = OB_EAGAIN;
      ARCHIVE_LOG(WARN, "no log bigger than min_scn_, wait next turn", K(ret), K_(id), K_(min_scn));
    } else if (OB_ERR_OUT_OF_LOWER_BOUND == ret) {
      int tmp_ret = OB_SUCCESS;
      if (OB_SUCCESS != (tmp_ret = log_handler->get_begin_lsn(lsn))) {
        ARCHIVE_LOG(WARN, "get begin lsn failed", K(tmp_ret), KPC(this));
        ret = OB_EAGAIN;
      } else if (lsn == LSN(palf::PALF_INITIAL_LSN_VAL)) {
        lsn = LSN(palf::PALF_INITIAL_LSN_VAL);
        ret = OB_SUCCESS;
      } else {
        log_gap = true;
        ARCHIVE_LOG(WARN, "log gap exist, mark fatal error");
        ret = OB_SUCCESS;
      }
    } else {
      ARCHIVE_LOG(WARN, "locate by scn coarsely failed", K(ret), KPC(this));
    }
  }
  if (OB_SUCC(ret) && ERRSIM_LOG_EXIST_GAP) {
    log_gap = true;
  }
  return ret;
}
// For the scenario where streaming is immediately cut off upon archiving, it is necessary to set an accurate initial piece_id
// 1. piece_id is the primary key of the archive progress table, it must have a reasonable value, when re-enabling archiving, piece_id should also increment in order, and cannot be too large
// 2. piece is the benchmark for summarizing overall archive progress, for already frozen piece, it cannot be smaller
//
// Use the archive start time and log stream creation time as the base piece_id
int StartArchiveHelper::get_local_start_scn_(SCN &scn)
{
  int ret = OB_SUCCESS;
  SCN create_scn;
  if (OB_FAIL(persist_mgr_->get_ls_create_scn(id_, create_scn))) {
    ARCHIVE_LOG(WARN, "get ls create scn failed", K(ret), K(id_));
  } else {
    SCN last_scn = SCN::minus(min_scn_, 1);
    scn = create_scn > last_scn ? create_scn : last_scn;
    if (!scn.is_valid()) {
      ret = OB_ERR_UNEXPECTED;
      ARCHIVE_LOG(WARN, "scn is invalid", K(ret), K(id_), K(scn));
    }
  }
  return ret;
}
// Due to independent compression/encryption of the archive, the archive data offset cannot be completely consistent with the ob log offset
// Only ensure that the archived file_id contains the corresponding ob log range, the archived file_offset is independently maintained
int StartArchiveHelper::cal_archive_file_id_offset_(const LSN &lsn,
    const int64_t archive_file_id,
    const int64_t archive_file_offset)
{
  int ret = OB_SUCCESS;
  int64_t file_id = OB_INVALID_ARCHIVE_FILE_ID;

  if (OB_UNLIKELY(OB_INVALID_ARCHIVE_FILE_ID ==
        (file_id = cal_archive_file_id(lsn, MAX_ARCHIVE_FILE_SIZE)))) {
    ret = OB_ERR_UNEXPECTED;
    ARCHIVE_LOG(ERROR, "invalid file id", K(ret), K(file_id), K(lsn), K(id_));
  } else {
    archive_file_id_ = file_id;
    archive_file_offset_ = file_id == archive_file_id ? archive_file_offset : 0;
  }
  return ret;
}

} // namespace archive
} // namespace oceanbase
