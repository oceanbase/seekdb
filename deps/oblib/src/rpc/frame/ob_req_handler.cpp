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

#define USING_LOG_PREFIX RPC_FRAME

#include "io/easy_io.h"
#include "ob_req_handler.h"
#include "common/ob_clock_generator.h"

using namespace oceanbase::rpc::frame;
using namespace oceanbase::common::serialization;

const uint8_t ObReqHandler::MAGIC_HEADER_FLAG[4] =
{ ObReqHandler::API_VERSION, 0xDB, 0xDB, 0xCE };
const uint8_t ObReqHandler::MAGIC_COMPRESS_HEADER_FLAG[4] =
{ ObReqHandler::API_VERSION, 0xDB, 0xDB, 0xCC };

void *ObReqHandler::decode(easy_message_t */* m */)
{
  return NULL;
}

int ObReqHandler::encode(easy_request_t */* r */, void */* packet */)
{
  return EASY_OK;
}

int ObReqHandler::process(easy_request_t */* r */)
{
  return EASY_OK;
}

int ObReqHandler::batch_process(easy_message_t */* m */)
{
  return EASY_OK;
}

int ObReqHandler::on_connect(easy_connection_t */* c */)
{
  return EASY_OK;
}

int ObReqHandler::on_disconnect(easy_connection_t */* c */)
{
  return EASY_OK;
}

int ObReqHandler::new_packet(easy_connection_t */* c */)
{
  return EASY_OK;
}

uint64_t ObReqHandler::get_packet_id(easy_connection_t */* c */, void */* packet */)
{
  return 0;
}

void ObReqHandler::set_trace_info(easy_request_t */*  r */, void */*  packet */)
{
}

int ObReqHandler::on_idle(easy_connection_t */* c */)
{
  return EASY_OK;
}

void ObReqHandler::send_buf_done(easy_request_t */* r */)
{
}

void ObReqHandler::sending_data(easy_connection_t */* c */)
{
}

int ObReqHandler::send_data_done(easy_connection_t */* c */)
{
  return EASY_OK;
}

int ObReqHandler::on_redispatch(easy_connection_t */* c */)
{
  return EASY_OK;
}

int ObReqHandler::on_close(easy_connection_t */* c */)
{
  return EASY_OK;
}

int ObReqHandler::cleanup(easy_request_t */* r */, void */* packet */)
{
  return EASY_OK;
}

namespace oceanbase
{
namespace easy
{

// easy callbacks


















} // end of namespace easy
} // end of namespace oceanbase
