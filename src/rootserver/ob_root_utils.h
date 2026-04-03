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

#ifndef _OB_ROOT_UTILS_H
#define _OB_ROOT_UTILS_H 1
#include "share/unit/ob_unit_info.h"
#include "share/ob_check_stop_provider.h"
#include "lib/container/ob_iarray.h"
#include "lib/container/ob_array.h"
#include "lib/container/ob_array_iterator.h"
#include "lib/hash/ob_hashmap.h"
#include "share/ob_define.h"
#include "share/ob_common_rpc_proxy.h"
#include "rootserver/ob_replica_addr.h"
#include "share/ob_cluster_role.h"
#include "share/ob_rpc_struct.h"
namespace oceanbase
{
namespace share
{
class ObILSPropertyGetter;
namespace schema
{
class ObMultiVersionSchemaService;
class ObTableSchema;
class ObSchemaGetterGuard;
}
}


namespace rootserver
{
class ObDDLService;
template <typename T>
inline T majority(const T n)
{
  return n / 2 + 1;
}

template<typename T>
inline bool is_same_tg(const T &left, const T &right)
{
  bool same = false;
  if (left.tablegroup_id_ == right.tablegroup_id_) {
    if (common::OB_INVALID_ID != left.tablegroup_id_) {
      same = true;
    } else {
      same = left.table_id_ == right.table_id_;
    }
  }
  return same;
}

template<typename T>
inline bool is_same_pg(const T &left, const T &right)
{
  return is_same_tg(left, right) && left.partition_idx_ == right.partition_idx_;
}


enum ObResourceType
{
  RES_CPU = 0,
  RES_MEM = 1,
  RES_LOG_DISK = 2,
  RES_DATA_DISK = 3,
  RES_MAX
};

const char *resource_type_to_str(const ObResourceType &t);

class ObIServerResourceDemand
{
public:
  ObIServerResourceDemand() = default;
  virtual ~ObIServerResourceDemand() = default;
  // return -1 if resource_type is invalid
  virtual double get_demand(ObResourceType resource_type) const = 0;
};

class ObTenantUtils
{
public:
  static int get_tenant_ids(
      share::schema::ObMultiVersionSchemaService *schema_service,
      common::ObIArray<uint64_t> &tenant_ids);
private:

};

ObTraceEventRecorder *get_rs_trace_recorder();
inline ObTraceEventRecorder *get_rs_trace_recorder()
{
  auto *ptr = GET_TSI_MULT(ObTraceEventRecorder, 2);
  return ptr;
}

class ObRootUtils
{
public:
  ObRootUtils() {}
  virtual ~ObRootUtils() {}

  static int get_rs_default_timeout_ctx(ObTimeoutCtx &ctx);

  template<class T>
      static bool is_subset(const common::ObIArray<T> &superset_array,
                            const common::ObIArray<T> &array);
  template<typename T>
  static int copy_array(const common::ObIArray<T> &src_array,
                        const int64_t start_pos,
                        const int64_t end_pos,
                        common::ObIArray<T> &dst_array);
};

template<class T>
bool ObRootUtils::is_subset(const common::ObIArray<T> &superset_array,
                            const common::ObIArray<T> &array)
{
  bool bret = true;
  for (int64_t i = 0; i < array.count() && bret; i++) {
    if (has_exist_in_array(superset_array, array.at(i))) {
      //nothing todo
    } else {
      bret = false;
    }
  }
  return bret;
}

template<typename T>
int ObRootUtils::copy_array(
    const common::ObIArray<T> &src_array,
    const int64_t start_pos,
    const int64_t end_pos,
    common::ObIArray<T> &dst_array)
{
  int ret = common::OB_SUCCESS;
  dst_array.reset();
  if (OB_UNLIKELY(start_pos < 0 || start_pos > end_pos || end_pos > src_array.count())) {
    ret = common::OB_INVALID_ARGUMENT;
    COMMON_LOG(WARN, "invalid start_pos/end_pos", KR(ret),
               K(start_pos), K(end_pos), "src_array_cnt", src_array.count());
  } else if (start_pos == end_pos) {
    // do nothing
  } else if (OB_FAIL(dst_array.reserve(end_pos - start_pos))) {
    COMMON_LOG(WARN, "fail to reserve array", KR(ret), "cnt", end_pos - start_pos);
  } else {
    for (int64_t i = start_pos; OB_SUCC(ret) && i < end_pos; i++) {
      if (OB_FAIL(dst_array.push_back(src_array.at(i)))) {
        COMMON_LOG(WARN, "fail to push back", KR(ret), K(i));
      }
    } // end for
  }
  return ret;
}

class ObClusterInfoGetter
{
public:
  ObClusterInfoGetter() {}
  virtual ~ObClusterInfoGetter() {}
  static common::ObClusterRole get_cluster_role_v2();
  static common::ObClusterRole get_cluster_role();
};
} // end namespace rootserver
} // end namespace oceanbase

#ifndef FOR_BEGIN_END_E
#define FOR_BEGIN_END_E(it, obj, array, extra_condition) \
    for (__typeof__((array).begin()) it = (array).begin() + (obj).begin_; \
        (extra_condition) && (it != (array).end()) && (it != (array).begin() + (obj).end_); ++it)
#endif

#ifndef FOR_BEGIN_END
#define FOR_BEGIN_END(it, obj, array) \
    FOR_BEGIN_END_E(it, (obj), (array), true)
#endif

// record trace events into THE one recorder
#define THE_RS_TRACE ::oceanbase::rootserver::get_rs_trace_recorder()

#define RS_TRACE(...)                           \
  do {                                          \
    if (OB_LIKELY(THE_RS_TRACE != nullptr)) {     \
      REC_TRACE(*THE_RS_TRACE, __VA_ARGS__);        \
    }                                           \
  } while (0)                                   \

#define RS_TRACE_EXT(...)                       \
  do {                                          \
    if (OB_LIKELY(THE_RS_TRACE != nullptr)) {     \
      REC_TRACE_EXT(*THE_RS_TRACE, __VA_ARGS__);    \
    }                                           \
  } while (0)


#endif /* _OB_ROOT_UTILS_H */
