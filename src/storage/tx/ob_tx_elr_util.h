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

#ifndef OCEANBASE_TX_ELR_UTIL_
#define OCEANBASE_TX_ELR_UTIL_

#include "ob_trans_define.h"

namespace oceanbase
{

namespace transaction
{

class ObTxDesc;

class ObTxELRUtil
{
public:
  ObTxELRUtil() : last_refresh_ts_(0),
                  can_tenant_elr_(false) {}
  int check_and_update_tx_elr_info();
  bool is_can_tenant_elr() const { return can_tenant_elr_; }
  void reset()
  {
    last_refresh_ts_ = 0;
    can_tenant_elr_ = false;
  }
  TO_STRING_KV(K_(last_refresh_ts), K_(can_tenant_elr));
private:
  void refresh_elr_tenant_config_();
private:
  static const int64_t REFRESH_INTERVAL = 5000000;
private:
  int64_t last_refresh_ts_;
  bool can_tenant_elr_;
};

} // transaction
} // oceanbase

#endif
