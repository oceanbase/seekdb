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

#ifndef OCEANBASE_SHARE_OB_STRUCTURED_EVENT_LOGGER_H_
#define OCEANBASE_SHARE_OB_STRUCTURED_EVENT_LOGGER_H_

#include "lib/oblog/ob_log.h"
#include "lib/utility/ob_print_utils.h"
#include <utility>

namespace oceanbase
{
namespace share
{

class ObStructuredEventLogger
{
public:
  template <typename ...Rest>
  static int log(const char *scope, const char *module, const char *event, Rest &&...fields)
  {
    static_assert(sizeof...(fields) <= 13
                  && (sizeof...(fields) == 13 || sizeof...(fields) % 2 == 0),
                  "support up to 6 name-value pairs and an optional extra info value");
    int ret = common::OB_SUCCESS;
    char payload[2048] = {'\0'};
    int64_t pos = 0;
    common::ObCStringHelper helper;
    if (OB_ISNULL(scope) || OB_ISNULL(module) || OB_ISNULL(event)) {
      ret = common::OB_INVALID_ARGUMENT;
    } else if (OB_FAIL(append_fields_(helper, payload, sizeof(payload), pos,
                                     std::forward<Rest>(fields)...))) {
      payload[sizeof(payload) - 1] = '\0';
    }
    SHARE_LOG(INFO, "structured diagnostic event", K(ret), K(scope), K(module), K(event), K(payload));
    // Diagnostics must not alter the control flow of the instrumented operation.
    return common::OB_SUCCESS;
  }

  template <typename ...Rest>
  static int log_tenant(const char *module,
                        const char *event,
                        const int64_t event_timestamp,
                        const int user_ret,
                        const int64_t cost_us,
                        Rest &&...fields)
  {
    static_assert(sizeof...(fields) <= 12 && sizeof...(fields) % 2 == 0,
                  "support up to 6 tenant event name-value pairs");
    int ret = common::OB_SUCCESS;
    char payload[2048] = {'\0'};
    int64_t pos = 0;
    common::ObCStringHelper helper;
    if (OB_ISNULL(module) || OB_ISNULL(event)) {
      ret = common::OB_INVALID_ARGUMENT;
    } else if (OB_FAIL(append_fields_(helper, payload, sizeof(payload), pos,
                                     std::forward<Rest>(fields)...))) {
      payload[sizeof(payload) - 1] = '\0';
    }
    SHARE_LOG(INFO, "structured tenant diagnostic event", K(ret), K(module), K(event),
              K(event_timestamp), K(user_ret), K(cost_us), K(payload));
    return common::OB_SUCCESS;
  }

private:
  static int append_fields_(common::ObCStringHelper &, char *, int64_t, int64_t &)
  {
    return common::OB_SUCCESS;
  }

  template <typename ExtraInfo>
  static int append_fields_(common::ObCStringHelper &helper,
                            char *buf,
                            const int64_t buf_len,
                            int64_t &pos,
                            ExtraInfo &&extra_info)
  {
    int ret = common::OB_SUCCESS;
    const char *value = helper.convert(extra_info);
    if (OB_ISNULL(value)) {
      ret = helper.get_ob_errno();
    } else if (OB_FAIL(common::databuff_printf(buf, buf_len, pos, "%sextra_info=%s",
                                              pos == 0 ? "" : ", ", value))) {
    }
    return ret;
  }

  template <typename Name, typename Value, typename ...Rest>
  static int append_fields_(common::ObCStringHelper &helper,
                            char *buf,
                            const int64_t buf_len,
                            int64_t &pos,
                            Name &&name,
                            Value &&value,
                            Rest &&...fields)
  {
    int ret = common::OB_SUCCESS;
    const char *name_str = helper.convert(name);
    const char *value_str = helper.convert(value);
    if (OB_ISNULL(name_str) || OB_ISNULL(value_str)) {
      ret = helper.get_ob_errno();
    } else if (OB_FAIL(common::databuff_printf(buf, buf_len, pos, "%s%s=%s",
                                              pos == 0 ? "" : ", ", name_str, value_str))) {
    } else if (OB_FAIL(append_fields_(helper, buf, buf_len, pos,
                                     std::forward<Rest>(fields)...))) {
    }
    return ret;
  }
};

} // namespace share
} // namespace oceanbase

#define SERVER_EVENT_ADD(args...) \
  (::oceanbase::share::ObStructuredEventLogger::log("server", args))
#define SERVER_EVENT_SYNC_ADD(args...) SERVER_EVENT_ADD(args)

#define ROOTSERVICE_EVENT_ADD(args...) \
  (::oceanbase::share::ObStructuredEventLogger::log("rootservice", args))
#define ROOTSERVICE_EVENT_ADD_TRUNCATE(args...) ROOTSERVICE_EVENT_ADD(args)

#define CLUSTER_EVENT_SYNC_ADD(args...) \
  (::oceanbase::share::ObStructuredEventLogger::log("cluster", args))

#define TENANT_EVENT_ADD(module, event, event_timestamp, user_ret, cost_us, args...) \
  (::oceanbase::share::ObStructuredEventLogger::log_tenant(                      \
      module, event, event_timestamp, user_ret, cost_us, ##args))

#endif // OCEANBASE_SHARE_OB_STRUCTURED_EVENT_LOGGER_H_
