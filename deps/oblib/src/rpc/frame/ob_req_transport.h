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

#ifndef _OCEABASE_RPC_FRAME_OB_REQ_TRANSPORT_H_
#define _OCEABASE_RPC_FRAME_OB_REQ_TRANSPORT_H_

#include "lib/ob_errno.h"
#include "lib/net/ob_addr.h"
#include "lib/oblog/ob_log.h"
#include "lib/utility/ob_print_utils.h"
#include "lib/statistic_event/ob_stat_event.h"
#include "rpc/ob_packet.h"
#include "lib/allocator/ob_malloc.h"

namespace oceanbase
{
namespace common
{
class ObDataBuffer;
} // end of namespace common

namespace rpc
{
class ObPacket;
namespace frame
{

using common::ObAddr;

class SPAlloc {
public:
  SPAlloc() {}
  virtual ~SPAlloc() {}

  void *operator()(int64_t size) const
  {
    return alloc(size);
  }
  virtual void* alloc(int64_t size) const = 0;
};

class ObReqTransport
{
public:
  // asynchronous callback class.
  //
  // Every asynchronous request will hold an object of this class that
  // been called after easy has detected the response packet.
  class AsyncCB
  {
  public:
    AsyncCB(int pcode)
        : low_level_cb_(NULL), gtid_(0), pkt_id_(0),
          dst_(), timeout_(0),
          err_(0), pcode_(pcode), send_ts_(0), payload_(0)
    {}
    virtual ~AsyncCB() {}

    virtual AsyncCB *clone(const SPAlloc &alloc) const = 0;

    virtual int decode(void *pkt) = 0;
    virtual int process() = 0;
    virtual int get_rcode() = 0;
    virtual void reset_rcode() = 0;
    virtual void set_cloned(bool cloned) = 0;
    virtual bool get_cloned() = 0;

    // invoke when get a valid packet on protocol level, but can't decode it.
    virtual void on_invalid() {
      int ret = err_;
    }
    // invoke when can't get a valid or completed packet.
    virtual void on_timeout() {  }
    virtual int on_error(int err);
    void set_error(int err) { err_ = err; }
    int get_error() const { return err_; }

    void set_dst(const ObAddr &dst) { dst_ = dst; }
    void set_timeout(int64_t timeout) { timeout_ = timeout; }
    
    void set_send_ts(const int64_t send_ts) { send_ts_ = send_ts; }
    int64_t get_send_ts() { return send_ts_; }
    void set_payload(const int64_t payload) { payload_ = payload; }
    int64_t get_payload() { return payload_; }
    int get_pcode() const { return pcode_; }

    void* low_level_cb_;
    uint64_t gtid_;
    uint32_t pkt_id_;
  private:
    static const int64_t REQUEST_ITEM_COST_RT = 100 * 1000; // 100ms
  protected:
    ObAddr dst_;
    int64_t timeout_;
    
    int err_;
    int pcode_;
    int64_t send_ts_;
    int64_t payload_;
  };

}; // end of class ObReqTransport
} // end of namespace frame
} // end of namespace rpc
} // end of namespace oceanbase

#endif /* _OCEABASE_RPC_FRAME_OB_REQ_TRANSPORT_H_ */
