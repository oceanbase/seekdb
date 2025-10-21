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

#ifndef OCEANBASE_LOGSERVICE_OB_NET_KEEPALIVE_ADPATER_H_
#define OCEANBASE_LOGSERVICE_OB_NET_KEEPALIVE_ADPATER_H_
#include <stdint.h>  // for int64_t etc.

namespace oceanbase
{
namespace common
{
class ObAddr;
}
namespace obrpc
{
class ObNetKeepAlive;
}
namespace logservice 
{
class IObNetKeepAliveAdapter {
public:
  IObNetKeepAliveAdapter() {}
  virtual ~IObNetKeepAliveAdapter() {}
  virtual bool in_black_or_stopped(const common::ObAddr &server) = 0;
  virtual bool is_server_stopped(const common::ObAddr &server) = 0;
  virtual bool in_black(const common::ObAddr &server) = 0;
  virtual int get_last_resp_ts(const common::ObAddr &server, int64_t &last_resp_ts) = 0;
};

class ObNetKeepAliveAdapter : public IObNetKeepAliveAdapter {
public:
  ObNetKeepAliveAdapter(obrpc::ObNetKeepAlive *net_keepalive);
  ~ObNetKeepAliveAdapter() override;
  bool in_black_or_stopped(const common::ObAddr &server) override final;
  bool is_server_stopped(const common::ObAddr &server) override final;
  bool in_black(const common::ObAddr &server) override final;
  int get_last_resp_ts(const common::ObAddr &server, int64_t &last_resp_ts) override final;
private:
  int in_black_or_stopped_(const common::ObAddr &server,
                           bool &in_black,
                           bool &server_is_stopped);
  obrpc::ObNetKeepAlive *net_keepalive_;
};

} // end namespace logservice
} // end namespace oceanbase
#endif
