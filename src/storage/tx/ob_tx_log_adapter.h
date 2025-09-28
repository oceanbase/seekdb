/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
 */

#ifndef OCEANBASE_STORAGE_TX_OB_LS_TX_LOG_ADAPTER
#define OCEANBASE_STORAGE_TX_OB_LS_TX_LOG_ADAPTER

#include "share/ob_define.h"
#include "logservice/ob_log_handler.h"
#include "ob_trans_submit_log_cb.h"

namespace oceanbase
{
namespace share
{
class SCN;
}
namespace palf
{
class LSN;
class PalfHandle;
} // namespace palf
namespace transaction
{

class ObITxLogParam
{
public:
private:
  // nothing, base struct for test
};

class ObTxPalfParam : public ObITxLogParam
{
public:
  ObTxPalfParam(logservice::ObLogHandler *handler)
      : handler_(handler)
  {}
  logservice::ObLogHandler *get_log_handler() { return handler_; }

private:
  logservice::ObLogHandler *handler_;
};

class ObITxLogAdapter
{
public:

  virtual void reset() {}

  virtual int submit_log(const char *buf,
                         const int64_t size,
                         const share::SCN &base_ts,
                         ObTxBaseLogCb *cb,
                         const bool need_nonblock,
                         const int64_t retry_timeout_us = 1000) = 0;

  virtual int get_role(bool &is_leader, int64_t &epoch) = 0;
  virtual int get_max_decided_scn(share::SCN &scn) = 0;
  virtual int get_palf_committed_max_scn(share::SCN &scn) const = 0;
  virtual int get_append_mode_initial_scn(share::SCN &ref_scn) = 0;
};

class ObLSTxLogAdapter : public ObITxLogAdapter
{
public:
  ObLSTxLogAdapter() : log_handler_(nullptr), tx_table_(nullptr) {}

  void reset();

  int init(ObITxLogParam *param, ObTxTable *tx_table);
  int submit_log(const char *buf,
                 const int64_t size,
                 const share::SCN &base_ts,
                 ObTxBaseLogCb *cb,
                 const bool need_nonblock,
                 const int64_t retry_timeout_us = 1000);
  int get_role(bool &is_leader, int64_t &epoch);
  int get_max_decided_scn(share::SCN &scn);
  int get_palf_committed_max_scn(share::SCN &scn) const;

  int get_append_mode_initial_scn(share::SCN &ref_scn);

private:
  logservice::ObLogHandler *log_handler_;
  ObTxTable *tx_table_;
};

} // namespace transaction
} // namespace oceanbase

#endif
