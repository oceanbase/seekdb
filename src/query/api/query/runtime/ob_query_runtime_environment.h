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

#ifndef OCEANBASE_QUERY_RUNTIME_OB_QUERY_RUNTIME_ENVIRONMENT_H_
#define OCEANBASE_QUERY_RUNTIME_OB_QUERY_RUNTIME_ENVIRONMENT_H_

#include <cstdint>
#include <functional>

namespace oceanbase
{
namespace rpc
{
class ObRequest;
}
namespace query
{

class ObIQueryRuntimeEnvironment
{
public:
  virtual ~ObIQueryRuntimeEnvironment() = default;
  virtual uint64_t cpu_frequency_khz() = 0;
  virtual int64_t network_speed_bytes_per_second() const = 0;
  virtual bool server_stopped() = 0;
  virtual bool server_has_tenant() const = 0;
  virtual void request_ctas_cleanup() = 0;
  virtual int check_current_tenant_available() const = 0;
  virtual int get_current_tenant_cpu(double &min_cpu, double &max_cpu) const = 0;
  virtual int get_current_tenant_min_worker_count(int64_t &worker_count) const = 0;
  virtual int get_current_worker_unit_min_cpu(double &min_cpu) const = 0;
  virtual int64_t current_query_start_time() const = 0;
  virtual int submit_current_tenant_request(rpc::ObRequest &request) const = 0;
  virtual int submit_px_task(
      int64_t group_id,
      const std::function<void(bool)> &task) const = 0;
};

inline uint64_t query_cpu_frequency_khz(
    ObIQueryRuntimeEnvironment &environment)
{
  return environment.cpu_frequency_khz();
}

inline uint64_t query_cpu_frequency_khz(
    ObIQueryRuntimeEnvironment *environment)
{
  return nullptr == environment
      ? 2500U * 1000U
      : environment->cpu_frequency_khz();
}

inline int64_t query_network_speed_bytes_per_second(
    const ObIQueryRuntimeEnvironment &environment)
{
  return environment.network_speed_bytes_per_second();
}

inline int64_t query_network_speed_bytes_per_second(
    const ObIQueryRuntimeEnvironment *environment)
{
  return nullptr == environment
      ? 10000L / 8L * 1024L * 1024L
      : environment->network_speed_bytes_per_second();
}

inline bool query_server_stopped(ObIQueryRuntimeEnvironment &environment)
{
  return environment.server_stopped();
}

inline bool query_server_has_tenant(
    const ObIQueryRuntimeEnvironment &environment)
{
  return environment.server_has_tenant();
}

inline void request_query_ctas_cleanup(
    ObIQueryRuntimeEnvironment &environment)
{
  environment.request_ctas_cleanup();
}

inline void request_query_ctas_cleanup(
    ObIQueryRuntimeEnvironment *environment)
{
  if (nullptr != environment) {
    environment->request_ctas_cleanup();
  }
}

} // namespace query
} // namespace oceanbase

#endif // OCEANBASE_QUERY_RUNTIME_OB_QUERY_RUNTIME_ENVIRONMENT_H_
