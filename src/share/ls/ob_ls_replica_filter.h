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

#ifndef OCEANBASE_SHARE_OB_LS_REPLICA_FILTER
#define OCEANBASE_SHARE_OB_LS_REPLICA_FILTER

#include "lib/list/ob_dlist.h" // ObDLinkBase
#include "lib/net/ob_addr.h" // ObAddr

namespace oceanbase
{
namespace share
{
class ObLSReplica;

// Base class of all ls replica filter
class ObLSReplicaFilter : public common::ObDLinkBase<ObLSReplicaFilter>
{
public:
  ObLSReplicaFilter() {}
  virtual ~ObLSReplicaFilter() {}
  virtual int check(const ObLSReplica &replica, bool &pass) const = 0;
};

// reserve ls replica by server
class ObServerLSReplicaFilter : public ObLSReplicaFilter
{
public:
  explicit ObServerLSReplicaFilter(const common::ObAddr &server) : server_(server) {}
  virtual ~ObServerLSReplicaFilter() {}
  virtual int check(const ObLSReplica &replica, bool &pass) const;
private:
  common::ObAddr server_;
};

// Apply multiple filters at the same time
class ObLSReplicaFilterHolder : public ObLSReplicaFilter
{
public:
  ObLSReplicaFilterHolder() {}
  virtual ~ObLSReplicaFilterHolder();
  virtual int check(const ObLSReplica &replica, bool &pass) const;
  void reset();
  int set_reserved_server(const common::ObAddr &server);
private:
  int add_(ObLSReplicaFilter &filter);
  void try_free_filter_(ObLSReplicaFilter *filter, void *ptr);

  common::ObDList<ObLSReplicaFilter> filter_list_;
  common::ObArenaAllocator filter_allocator_;
  DISALLOW_COPY_AND_ASSIGN(ObLSReplicaFilterHolder);
};
} // end namespace share
} // end namespace oceanbase
#endif
