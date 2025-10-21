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

#ifndef OCEANBASE_OBSERVER_OBSM_CONN_CALLBACK_H_
#define OCEANBASE_OBSERVER_OBSM_CONN_CALLBACK_H_
#include "rpc/obmysql/ob_i_sm_conn_callback.h"

namespace oceanbase
{
namespace obmysql
{

class ObSMConnectionCallback: public ObISMConnectionCallback
{
public:
  ObSMConnectionCallback() {}
  virtual ~ObSMConnectionCallback() {}
  int init(ObSqlSockSession& sess, observer::ObSMConnection& conn) override;
  void destroy(observer::ObSMConnection& conn) override;
  int on_disconnect(observer::ObSMConnection& conn) override;
};
extern ObSMConnectionCallback global_sm_conn_callback;
}; // end namespace obmysql
}; // end namespace oceanbase

#endif /* OCEANBASE_OBSERVER_OBSM_CONN_CALLBACK_H_ */
