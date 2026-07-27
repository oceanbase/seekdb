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

#ifndef OCEANBASE_TRANSACTION_KEEP_ALIVE_LS_HANDLER
#define OCEANBASE_TRANSACTION_KEEP_ALIVE_LS_HANDLER

#include "logservice/palf/palf_callback.h"
#include "logservice/ob_log_base_header.h"
#include "logservice/ob_log_base_type.h"
#include "logservice/ob_append_callback.h"

namespace oceanbase
{

namespace logservice
{
class ObLogHandler;
}

namespace transaction
{

enum class MinStartScnStatus
{
  UNKOWN = 0, // collect failed
  NO_CTX,
  HAS_CTX,

  MAX
};

class ObKeepAliveLogBody
{
public:
  OB_UNIS_VERSION(1);

public:
  ObKeepAliveLogBody()
    : min_start_scn_(),
    min_start_status_(MinStartScnStatus::UNKOWN)
  {}
  ObKeepAliveLogBody(const share::SCN &min_start_scn,
                     MinStartScnStatus min_status)
    : min_start_scn_(min_start_scn),
    min_start_status_(min_status)
  {}

  static int64_t get_max_serialize_size();
  const share::SCN &get_min_start_scn() const { return min_start_scn_; };
  MinStartScnStatus get_min_start_status() { return min_start_status_; }

  TO_STRING_KV(K_(min_start_scn), K_(min_start_status));

private:
  share::SCN min_start_scn_;
  MinStartScnStatus min_start_status_;
};

struct KeepAliveLsInfo
{
  share::SCN loop_job_succ_scn_;
  palf::LSN lsn_;
  share::SCN min_start_scn_;
  MinStartScnStatus min_start_status_;

  void reset()
  {
    loop_job_succ_scn_.reset();
    lsn_.reset();
    min_start_scn_.reset();
    min_start_status_ = MinStartScnStatus::UNKOWN;
  }

  void replace(KeepAliveLsInfo info)
  {
    /* We only update in the NO_CTX and HAS_CTX states here because most of the KEEP_ALIVE logs are in the UNKNOW state.
     * If we update this in UNKNOW state, TX_DATA_TABLE will retrieve UNKNOW state in most cases, thus causing a
     * prolonged inability to calculate the upper_trans_version of TX_DATA_TABLE */
    if (info.min_start_status_ == MinStartScnStatus::NO_CTX || info.min_start_status_ == MinStartScnStatus::HAS_CTX) {
      min_start_scn_ = info.min_start_scn_;
      min_start_status_ = info.min_start_status_;
      loop_job_succ_scn_ = info.loop_job_succ_scn_;
      lsn_ = info.lsn_;
    }
  }

  TO_STRING_KV(K(loop_job_succ_scn_), K(lsn_), K(min_start_scn_), K(min_start_status_));
};

class ObLSKeepAliveStatInfo
{
public:
  ObLSKeepAliveStatInfo() { reset(); }
  void reset()
  {
    cb_busy_cnt = 0;
    near_to_gts_cnt = 0;
    other_error_cnt = 0;
    submit_succ_cnt = 0;
    stat_keepalive_info_.reset();
  }

  void clear_cnt()
  {
    cb_busy_cnt = 0;
    near_to_gts_cnt = 0;
    other_error_cnt = 0;
    submit_succ_cnt = 0;
  }

  int64_t cb_busy_cnt;
  int64_t near_to_gts_cnt;
  int64_t other_error_cnt;
  int64_t submit_succ_cnt;
  KeepAliveLsInfo stat_keepalive_info_;

private:
  // none
};

// after init , we should register in logservice
class ObKeepAliveLSHandler : public logservice::ObIReplaySubHandler,
                             public logservice::ObICheckpointSubHandler,
                             public logservice::ObILocalLogHandler,
                             public logservice::AppendCb
{
public:
  const int64_t KEEP_ALIVE_GTS_INTERVAL = 100 * 1000;
public:
  ObKeepAliveLSHandler() : submit_buf_(nullptr) { reset(); }
  int init(logservice::ObLogHandler *log_handler_ptr);

  void stop();
  // false - can not safe destroy
  void destroy();

  void reset();

  int try_submit_log(const share::SCN &min_start_scn, MinStartScnStatus status);
  void clear_keep_alive_smaller_scn_info();
  void print_stat_info();
  const char *get_cb_name() const override { return "KeepAliveLSHandler"; }
public:

  bool is_busy() { return ATOMIC_LOAD(&is_busy_); }
  int on_success();
  int on_failure();

  int replay(const void *buffer, const int64_t nbytes, const palf::LSN &lsn, const share::SCN &scn);
  void deactivate() override {}
  int activate() override { return OB_SUCCESS; }
  share::SCN get_rec_scn() { return share::SCN::max_scn(); }
  int flush(share::SCN &rec_scn) { return OB_SUCCESS;}

  void get_min_start_scn(share::SCN &min_start_scn,
                         share::SCN &keep_alive_scn,
                         MinStartScnStatus &status);
private:
  bool check_gts_();
  int serialize_keep_alive_log_(const share::SCN &min_start_scn, MinStartScnStatus status);
private :
  SpinRWLock lock_;

  bool is_busy_;
  bool is_stopped_;

  logservice::ObLogHandler * log_handler_ptr_;

  char *submit_buf_;
  int64_t submit_buf_len_;
  int64_t submit_buf_pos_;

  share::SCN last_gts_;

  KeepAliveLsInfo tmp_keep_alive_info_;
  KeepAliveLsInfo durable_keep_alive_info_;

  ObLSKeepAliveStatInfo stat_info_;
};

}
}
#endif
