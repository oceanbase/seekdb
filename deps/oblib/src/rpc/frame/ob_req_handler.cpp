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
