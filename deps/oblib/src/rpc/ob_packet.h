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

#ifndef _OCEABASE_RPC_OB_PACKET_H_
#define _OCEABASE_RPC_OB_PACKET_H_

#include "lib/utility/ob_print_utils.h"

namespace oceanbase
{
namespace rpc
{

// This is the base class for oceanbase packets. Contruct directly is
// not allowed, so that the its destructor is unaccessible.
class ObPacket
{
public:
  ObPacket() {}
  virtual ~ObPacket() {}

  DECLARE_VIRTUAL_TO_STRING = 0;
}; // end of class ObPacket


enum class ConnectionPhaseEnum
{
  CPE_CONNECTED = 0,//server will send handshake pkt
  CPE_SSL_CONNECT,  //server will do ssl connect
  CPE_AUTHED,       //server will do auth check and send ok pkt
  CPE_AUTH_SWITCH,  //server will do auth switch check and send ok pkt
};


} // end of namespace rpc
} // end of namespace oceanbase

#endif /* _OCEABASE_RPC_OB_PACKET_H_ */
