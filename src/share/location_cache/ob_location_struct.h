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

#ifndef OCEANBASE_SHARE_OB_LS_LOCATION
#define OCEANBASE_SHARE_OB_LS_LOCATION


#include "lib/net/ob_addr.h"
#include "share/ob_ls_id.h"

namespace oceanbase
{
namespace share
{
class ObLSLocation
{
  OB_UNIS_VERSION(1);
public:
  ObLSLocation();
  int init(const ObLSID &ls_id,
           const common::ObAddr &server,
           const int64_t renew_time);
  void reset();
  bool is_valid() const;
  bool operator==(const ObLSLocation &other) const;
  inline ObLSID get_ls_id() const { return ls_id_; }
  inline const common::ObAddr &get_server() const { return server_; }
  inline int64_t get_renew_time() const { return renew_time_; }
  TO_STRING_KV(K_(ls_id), K_(server), K_(renew_time));
private:
  ObLSID ls_id_;
  common::ObAddr server_;
  int64_t renew_time_;
};

} // end namespace share
} // end namespace oceanbase
#endif
