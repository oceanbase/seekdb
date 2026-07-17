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

#ifndef OCEANBASE_SHARE_OB_SERVER_LIST_H_
#define OCEANBASE_SHARE_OB_SERVER_LIST_H_

#include "lib/net/ob_addr.h"
#include "lib/container/ob_iarray.h"
#include "lib/container/ob_array_serialization.h"
#include "lib/utility/ob_unify_serialize.h"

namespace oceanbase
{
namespace share
{

typedef common::ObIArray<common::ObAddr> ObIServerList;

class ObRootServiceList
{
  OB_UNIS_VERSION(1);
public:
  ObRootServiceList() : rootservice_list_() {}
  ~ObRootServiceList() {}
  void reset()
  {
    rootservice_list_.reset();
  }

  // Getter
  ObIServerList &get_rs_list_arr() { return rootservice_list_; }
  const ObIServerList &get_rs_list_arr() const { return rootservice_list_; }

  TO_STRING_KV(K_(rootservice_list));

private:
  common::ObSArray<common::ObAddr> rootservice_list_;
};



} // namespace share
} // namespace oceanbase

#endif // OCEANBASE_SHARE_OB_SERVER_LIST_H_
