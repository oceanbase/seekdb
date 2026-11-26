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
namespace wal
{
class LSN;
class Handle;
} // namespace wal
namespace transaction
{

class ObITxLogParam
{
public:
private:
  // nothing, base struct for test
};

class ObTxWalParam : public ObITxLogParam
{
public:
  ObTxWalParam(logservice::ObLogHandler *handler)
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
                         ObTxBaseLogCb *cb) = 0;

  virtual int get_max_decided_scn(share::SCN &scn) = 0;
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
                 ObTxBaseLogCb *cb);
  int get_max_decided_scn(share::SCN &scn);

private:
  logservice::ObLogHandler *log_handler_;
  ObTxTable *tx_table_;
};

} // namespace transaction
} // namespace oceanbase

#endif
