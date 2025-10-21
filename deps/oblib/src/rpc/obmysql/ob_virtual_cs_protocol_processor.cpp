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

#define USING_LOG_PREFIX RPC_OBMYSQL
#include "rpc/obmysql/ob_virtual_cs_protocol_processor.h"
#include "rpc/obmysql/obsm_struct.h"


using namespace oceanbase::common;
using namespace oceanbase::obmysql;
using namespace oceanbase::observer;

class ObCSEasyMemPool: public ObICSMemPool
{
public:
  ObCSEasyMemPool(easy_pool_t* easy_pool): easy_pool_(easy_pool) {}
  virtual ~ObCSEasyMemPool() {}
  void* alloc(int64_t sz) { return easy_pool_alloc(easy_pool_, sz); }
private:
  easy_pool_t* easy_pool_;
};

int ObVirtualCSProtocolProcessor::easy_decode(easy_message_t *m, rpc::ObPacket *&pkt)
{
  int ret = OB_SUCCESS;
  ObSMConnection *conn = reinterpret_cast<ObSMConnection*>(m->c->user_data);
  ObCSEasyMemPool pool(m->pool);
  int64_t next_read_bytes = 0;
  if (OB_FAIL(do_decode(*conn, pool, (const char*&)m->input->pos, m->input->last, pkt, next_read_bytes))) {
    LOG_ERROR("fail do_decode", K(ret));
  } else {
    if (next_read_bytes > 0 ) {
      m->next_read_len = next_read_bytes;
    }
  }
  return ret;
}

int ObVirtualCSProtocolProcessor::easy_process(easy_request_t *r, bool &need_read_more)
{
  int ret = OB_SUCCESS;
  ObSMConnection *conn = reinterpret_cast<ObSMConnection*>(r->ms->c->user_data);
  ObCSEasyMemPool pool(r->ms->pool);
  need_read_more = true;
  if (!conn->is_in_authed_phase() && !conn->is_in_auth_switch_phase()) {
    need_read_more = false;
  } else if (OB_FAIL(do_splice(*conn, pool, r->ipacket, need_read_more))) {
    LOG_ERROR("fail to splice mysql packet", K(ret));
  }
  return ret;
}

