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

#define USING_LOG_PREFIX SHARE

#include "ob_tablet_filter.h"
#include "share/tablet/ob_tablet_info.h"

namespace oceanbase
{
namespace share
{
using namespace schema;
using namespace common;

int ObServerTabletReplicaFilter::check(const ObTabletReplica &replica, bool &pass) const
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!replica.is_valid() || !server_.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(replica), K_(server));
  } else {
    pass = (server_ == replica.get_server());
  }
  return ret;
}

int ObTabletReplicaFilterHolder::add_(ObTabletReplicaFilter &filter)
{
  int ret = OB_SUCCESS;
  if (!filter_list_.add_last(&filter)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("add filter to filter list failed", KR(ret));
  }
  return ret;
}

int ObTabletReplicaFilterHolder::check(const ObTabletReplica &replica, bool &pass) const
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!replica.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(replica));
  } else {
    pass = true;
    DLIST_FOREACH(it, filter_list_) {
      if (OB_FAIL(it->check(replica, pass))) {
        LOG_WARN("check replica failed", KR(ret));
      } else {
        if (!pass) {
          break;
        }
      }
    }
  }
  return ret;
}

void ObTabletReplicaFilterHolder::try_free_filter_(ObTabletReplicaFilter *filter, void *ptr)
{
  if (OB_NOT_NULL(filter)) {
    filter->~ObTabletReplicaFilter();
    filter_allocator_.free(filter);
  } else if (OB_NOT_NULL(ptr)) {
    filter_allocator_.free(ptr);
  }
}

int ObTabletReplicaFilterHolder::set_reserved_server(const ObAddr &server)
{
  int ret = OB_SUCCESS;
  ObServerTabletReplicaFilter *server_filter = nullptr;
  void *ptr = filter_allocator_.alloc(sizeof(ObServerTabletReplicaFilter));
  if (OB_UNLIKELY(!server.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(server));
  } else if (OB_ISNULL(ptr)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to alloc memory", KR(ret));
  } else {
    server_filter = new (ptr) ObServerTabletReplicaFilter(server);
    if (OB_ISNULL(server_filter)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("server filter is null", KR(ret));
    } else if (OB_FAIL(add_(*server_filter))) {
      LOG_WARN("add filter failed", KR(ret));
    }
  }
  if (OB_FAIL(ret)) {
    try_free_filter_(server_filter, ptr);
  }
  return ret;
}

} // end namespace share
} // end namespace oceanbase
