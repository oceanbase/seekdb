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

#define USING_LOG_PREFIX SERVER

#include "observer/mysql/obmp_statistic.h"
#include "rpc/obmysql/packet/ompk_string.h"

namespace oceanbase
{
using namespace common;
using namespace obmysql;

namespace observer
{
int ObMPStatistic::process()
{
  int ret = common::OB_SUCCESS;
  bool need_disconnect = true;
  bool need_response_error = true;
  //Attention::it is BUG when using like followers (build with release):
  //  obmysql::OMPKString pkt(ObString("Active threads not support"));
  //
  const common::ObString tmp_string("Active threads not support");
  obmysql::OMPKString pkt(tmp_string);

  if (OB_FAIL(response_packet(pkt))) {
    RPC_OBMYSQL_LOG(WARN, "fail to response statistic packet", K(ret));
  } else {
    // do nothing
  }

  if (OB_FAIL(ret) && need_response_error) {
    send_error_packet(ret, NULL);
  }
  if (OB_FAIL(ret) && need_disconnect) {
    force_disconnect();
    LOG_WARN("disconnect connection", KR(ret));
  }
  return ret;
}


} // namespace observer
} // namespace oceanbase
