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

#include "ob_interrupt_rpc_proxy.h"
#include "share/interrupt/ob_global_interrupt_call.h"

using namespace oceanbase::common;
namespace oceanbase
{
namespace obrpc
{

OB_SERIALIZE_MEMBER(ObInterruptStackInfo, buf1_);
OB_SERIALIZE_MEMBER(ObInterruptMessage, first_, last_, code_, info_);

int ObInterruptProcessor::process()
{
  int ret = OB_SUCCESS;
  const ObInterruptMessage &msg = arg_;
  ObInterruptibleTaskID tid(msg.first_, msg.last_);
  LIB_LOG(TRACE, "receive a interrupt from peer",
          "peer", get_peer(), K(tid), "int_code", msg.code_, "info", msg.info_);
  ObInterruptCode code(msg.code_, msg.info_);
  ObGlobalInterruptManager::getInstance()->interrupt(tid, code);
  return ret;
}
} // namespace obrpc
} // namespace oceanbase
