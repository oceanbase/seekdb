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

#ifndef OCEANBASE_ARCHIVE_OB_ARCHIVE_FETCHER_H_
#define OCEANBASE_ARCHIVE_OB_ARCHIVE_FETCHER_H_

#include "lib/queue/ob_lighty_queue.h"      // ObLightyQueue
#include "lib/compress/ob_compress_util.h"  // ObCompressorType
#include "common/ob_queue_thread.h"         // ObCond
#include "share/ob_thread_pool.h"           // ObThreadPool
#include "share/ob_ls_id.h"                 // ObLSID
#include "share/backup/ob_archive_piece.h"  // ObArchivePiece
#include "logservice/palf/lsn.h"            // LSN
#include "share/scn.h"                      // SCN
#include "ob_archive_define.h"
#include "logservice/palf/palf_iterator.h"  // PalfGroupBufferIterator

namespace oceanbase
{
namespace logservice
{
class ObLogService;
}

namespace share
{
class SCN;
}
namespace palf
{
struct LSN;
class PalfHandleGuard;
class LogGroupEntry;
}

namespace archive
{
class ObArchiveAllocator;
class ObArchiveSender;
class ObArchiveSequencer;
class ObArchiveLSMgr;
class ObArchiveRoundMgr;
class ArchiveWorkStation;
class ObArchiveSendTask;
class ObLSArchiveTask;
using oceanbase::logservice::ObLogService;
using oceanbase::share::ObLSID;
using oceanbase::share::ObArchivePiece;
using oceanbase::palf::LSN;
using oceanbase::palf::LogGroupEntry;

struct ObArchiveLogFetchTask;

/*
 * Archive read clog file module, consumes ObArchiveLogFetchTask, produces SendTask
 * Specifically for a single log stream, ObArchiveFetcher is a concurrent service
 * Additionally, while producing SendTask for the ObArchiveSender module, it constructs a continuous sequential task queue, facilitating sequential archiving of clog files for a single log stream
 * */
class ObArchiveFetcher : public share::ObThreadPool
{
  static const int64_t THREAD_RUN_INTERVAL = 100 * 1000L;    // 100ms
  static const int64_t MAX_CONSUME_TASK_NUM = 5;
public:
  ObArchiveFetcher();
  ~ObArchiveFetcher();

public:
  // sequencer thread constructs log read task, submits to fetcher task queue, thread safe
  //
  // @param [out], task log read task
  // @retval       OB_SUCCESS    success
  // @retval       OB_EAGAIN     retry later
  // @retval       other code    fail
  int submit_log_fetch_task(ObArchiveLogFetchTask *task);
  // Enable disable archive interface
  int set_archive_info(const int64_t piece_interval_us,
                       const share::SCN &genesis_scn,
                       const int64_t base_piece_id,
                       const int64_t unit_size,
                       const bool need_compress,
                       const ObCompressorType type,
                       const bool need_encrypt);
  void clear_archive_info();
  void signal();
  // Allocate LogFetch task
  ObArchiveLogFetchTask *alloc_log_fetch_task();
  // Release LogFetch task
  void free_log_fetch_task(ObArchiveLogFetchTask *task);

  int64_t get_log_fetch_task_count() const;

  int modify_thread_count(const int64_t thread_count);
public:
  int init(const uint64_t tenant_id,
      ObLogService *log_service,
      ObArchiveAllocator *allocator,
      ObArchiveSender *archive_sender,
      ObArchiveSequencer *archive_sequencer,
      ObArchiveLSMgr *mgr,
      ObArchiveRoundMgr *round_mgr);
  void destroy();
  int start();
  void wait();
  void stop();

private:
  class TmpMemoryHelper;
private:
  void run1();
  // fetcher thread work content: 1. consume LogFetchTask constructed by sequencer; 2. compress, encrypt according to aggregation strategy and build SendTask
  void do_thread_task_();
  int handle_single_task_();
  void handle_log_fetch_ret_(const ObLSID &id, const ArchiveKey &key, const int ret_code);
  // ============================ read data to construct send_task =============================== //
  //
  // NB: Due to concurrent processing of tasks with different LogOffsets in the same log stream, generating SendTasks need to be pushed back into the sequencing queue, single-threaded task consumption
  //
  // 1. Consume LogFetchTask constructed by sequencer, read ob log, submit to the fetch_log queue of log stream archive management task
  int handle_log_fetch_task_(ObArchiveLogFetchTask &task);

  // 1.0 get palf max lsn and scn
  int get_max_lsn_scn_(const ObLSID &id, palf::LSN &lsn, share::SCN &scn);
  // 1.1 Check if the task is delay processed
  int check_need_delay_(const ObArchiveLogFetchTask &task, const LSN &commit_lsn, bool &need_delay);
  // 1.1.1 Check if the ob log has generated data meeting the processing unit size
  void check_capacity_enough_(const LSN &commit_lsn, const LSN &cur_lsn,
      const LSN &end_offset, bool &data_full);
  // 1.1.2 Check the degree of log stream lag to determine if archiving should be triggered
  bool check_scn_enough_(const share::ObLSID &id, const bool new_block, const palf::LSN &lsn,
      const palf::LSN &max_no_limit_lsn, const share::SCN &base_scn, const share::SCN &fetch_scn,
      const int64_t last_fetch_timestamp);
  // 1.2 Initialize TmpMemoryHelper
  int init_helper_(ObArchiveLogFetchTask &task, const LSN &commit_lsn, TmpMemoryHelper &helper);
  // 1.3 Initialize log iterator
  int init_iterator_(const ObLSID &id, const TmpMemoryHelper &helper, palf::PalfGroupBufferIterator &iter);
  // 1.4 Generate archive data
  int generate_send_buffer_(palf::PalfGroupBufferIterator &iter,
                            TmpMemoryHelper &helper);
  // 1.4.1 Get controlled archive site
  int get_max_archive_point_(share::SCN &max_scn);
  // 1.4.2 helper piece information is empty, then use the first log piece of LogFetchTask to fill
  int fill_helper_piece_if_empty_(const ObArchivePiece &piece, TmpMemoryHelper &helper);
  // 1.4.3 Check if piece has changed
  bool check_piece_inc_(const ObArchivePiece &p1, const ObArchivePiece &p2);
  // 1.4.3.1 Set the next piece
  int set_next_piece_(TmpMemoryHelper &helper);
  // 1.4.4 Aggregation compression encryption unit
  int append_log_entry_(const char *buffer, LogGroupEntry &entry, TmpMemoryHelper &helper);
  // 1.4.5 Whether to aggregate data to sufficient compressed encryption unit size
  bool cached_buffer_full_(TmpMemoryHelper &helper);
  // 1.5 handle encryption and compression
  int handle_origin_buffer_(TmpMemoryHelper &helper);
  // 1.5.1 Compression
  int do_compress_(TmpMemoryHelper &helper);
  // 1.5.2 encryption
  int do_encrypt_(TmpMemoryHelper &helper);
  // 1.6 create send_task
  int build_send_task_(const ObLSID &id,
      const ArchiveWorkStation &station, TmpMemoryHelper &helper, ObArchiveSendTask *&task);
  // 1.7 update LogFetchTask, and refill piece information for the next piece of LogFetchTask
  // NB: refill next_piece information, convenient for archiving data of the next piece
  int update_log_fetch_task_(ObArchiveLogFetchTask &fetch_task,
      TmpMemoryHelper &helper, ObArchiveSendTask *send_task);
  // 1.8 submit fetch log
  int submit_fetch_log_(ObArchiveLogFetchTask &task, bool &submitted);
  // ==================================== Sequential consumption of data read by LogFetchTask ================== //
  //
  // NB: [start_offset, end_offset) may contain data from multiple pieces, therefore after consuming a certain LogFetchTask,
  // If it is not covering the entire log range, need to re-push back to fetcher task queue
  //
  // 2. Try to consume the read data
  int try_consume_fetch_log_(const ObLSID &id);
  // 2.1 Get the LogFetchTask for sequential consumption turn
  int get_sorted_fetch_log_(ObLSArchiveTask &ls_archive_task,
      ObArchiveLogFetchTask *&task, bool &task_exist);
  // 2.2 Submit SendTask
  int submit_send_task_(ObArchiveSendTask *task);
  // 2.3 Boost the fetcher module archiving progress
  int update_fetcher_progress_(ObLSArchiveTask &ls_archive_task, ObArchiveLogFetchTask &task);
  // 2.3 Check if LogFetchTask has processed all data
  bool check_log_fetch_task_consume_complete_(ObArchiveLogFetchTask &task);
  // 2.4 Submit remaining LogFetchTask tasks
  int submit_residual_log_fetch_task_(ObArchiveLogFetchTask &task);

  void inner_free_task_(ObArchiveLogFetchTask *task);

  bool is_retry_ret_(const int ret) const;

  bool in_normal_status_(const ArchiveKey &key) const;

  bool in_doing_status_(const ArchiveKey &key) const;

  void statistic(const int64_t log_size, const int64_t ts);

  bool iterator_need_retry_(const int ret) const;
private:
  class TmpMemoryHelper
  {
  public:
    TmpMemoryHelper(const int64_t unit_size, ObArchiveAllocator *allocator);
    ~TmpMemoryHelper();
  public:
    // If piece pointer is null, it indicates that piece is not specified
    int init(const uint64_t tenant_id, const ObLSID &id,
        const LSN &start_offset, const LSN &end_offset, const LSN &commit_lsn, ObArchivePiece *piece);
    const ObLSID &get_ls_id() const { return id_;}
    bool is_piece_set() { return cur_piece_.is_valid(); }
    const ObArchivePiece &get_piece() const { return cur_piece_; }
    int set_piece(const ObArchivePiece &piece);
    const ObArchivePiece &get_next_piece() const { return next_piece_; }
    int set_next_piece();
    const LSN &get_start_offset() const { return start_offset_; }
    const LSN &get_end_offset() const { return end_offset_; }
    const LSN &get_cur_offset() const { return cur_offset_; }
    const share::SCN &get_unitized_scn() const { return unitized_scn_; }
    int64_t get_capaicity() const { return end_offset_ - cur_offset_; }
    bool is_log_out_of_range(const int64_t size) const;
    bool original_buffer_enough(const int64_t size);
    int get_original_buf(const char *&buf, int64_t &buf_size);
    int append_handled_buf(char *buf, const int64_t buf_size);
    int get_handled_buf(char *&buf, int64_t &buf_size);
    int append_log_entry(const char *buffer, LogGroupEntry &entry);
    void freeze_log_entry();
    void reset_original_buffer();
    int64_t get_log_fetch_size() const { return cur_offset_ - start_offset_; }
    bool is_empty() const { return NULL == ec_buf_ || 0 == ec_buf_pos_; }
    bool reach_end() { return cur_offset_ == end_offset_ || cur_offset_ == commit_offset_; }
    ObArchiveSendTask *gen_send_task();

    TO_STRING_KV(K_(tenant_id),
                 K_(id),
                 K_(start_offset),
                 K_(end_offset),
                 K_(commit_offset),
                 K_(origin_buf_pos),
                 K_(cur_offset),
                 K_(cur_scn),
                 //K_(ec_buf),
                 K_(ec_buf_size),
                 K_(ec_buf_pos),
                 K_(unitized_offset),
                 K_(unitized_scn),
                 K_(cur_piece),
                 K_(next_piece),
                 K_(unit_size));
  private:
    int get_send_buffer_(const int64_t size);
    void inner_free_send_buffer_();
    int64_t get_reserved_buf_size_() const;
  private:
    bool inited_;
    uint64_t tenant_id_;
    ObLSID id_;
    // task start and end offset
    LSN start_offset_;
    LSN end_offset_;
    LSN commit_offset_;
    // Process the raw data buffer, since a single processing might not gather enough data to reach unit_size, this part will be re-read next time and the log_ts/offset progress needs to be rolled back
    const char *origin_buf_;
    int64_t origin_buf_pos_;
    // Current processing original log maximum offset/scn
    LSN cur_offset_;
    share::SCN cur_scn_;
    // Middle missing encryption buffer
    char *ec_buf_;
    int64_t ec_buf_size_;        // Need to be calculated based on the amount of data
    int64_t ec_buf_pos_;
    // The maximum offset/scn of logs that have been unitized
    LSN unitized_offset_;
    share::SCN unitized_scn_;
    // Currently processing piece, single helper contains all data from start_offset to current piece
    ObArchivePiece cur_piece_;
    // During the log reading process, encountering a larger piece indicates that the current piece has ended
    // Set next_piece, convenient for next processing
    ObArchivePiece next_piece_;
    int64_t unit_size_;
    ObArchiveAllocator *allocator_;
  };

private:
  bool               inited_;
  uint64_t           tenant_id_;
  // Compression encryption processing unit
  int64_t            unit_size_;
  int64_t            piece_interval_;
  share::SCN            genesis_scn_;
  int64_t            base_piece_id_;
  bool               need_compress_;
  // compression algorithm
  ObCompressorType   compress_type_;
  bool               need_encrypt_;
  ObLogService       *log_service_;
  ObArchiveAllocator *allocator_;
  ObArchiveSender    *archive_sender_;
  ObArchiveSequencer *archive_sequencer_;
  ObArchiveLSMgr     *ls_mgr_;
  ObArchiveRoundMgr  *round_mgr_;
  // Global ObArchiveLogFetchTask queue
  common::ObLightyQueue     task_queue_;
  common::ObCond            fetch_cond_;
};

} // namespace archive
} // namespace oceanbase
#endif /* OCEANBASE_ARCHIVE_OB_ARCHIVE_FETCHER_H_ */
