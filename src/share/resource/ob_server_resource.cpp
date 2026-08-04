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

#define USING_LOG_PREFIX    SHARE

#include <cmath>
#include "share/resource/ob_server_resource.h"
#include "share/ob_server_struct.h"

namespace oceanbase
{
using namespace common;
namespace share
{

#define CALCULATE_CONFIG(l, op, r, result) \
  do { \
    result.max_cpu_ = l.max_cpu_ op r.max_cpu_; \
    result.min_cpu_ = l.min_cpu_ op r.min_cpu_; \
    result.memory_size_ = l.memory_size_ op r.memory_size_; \
    result.log_disk_size_ = l.log_disk_size_ op r.log_disk_size_; \
    result.max_iops_ = l.max_iops_ op r.max_iops_; \
    result.min_iops_ = l.min_iops_ op r.min_iops_; \
    result.iops_weight_ = l.iops_weight_ op r.iops_weight_; \
    result.max_net_bandwidth_ = l.max_net_bandwidth_ op r.max_net_bandwidth_; \
    result.net_bandwidth_weight_ = l.net_bandwidth_weight_ op r.net_bandwidth_weight_; \
  } while (false)

#define CALCULATE_CONFIG_WITH_CONSTANT(l, op, c, result) \
  do { \
    result.max_cpu_ = l.max_cpu_ op static_cast<double>(c); \
    result.min_cpu_ = l.min_cpu_ op static_cast<double>(c); \
    result.memory_size_ = l.memory_size_ op (c); \
    result.log_disk_size_ = l.log_disk_size_ op (c); \
    result.max_iops_ = l.max_iops_ op (c); \
    result.min_iops_ = l.min_iops_ op (c); \
    result.iops_weight_ = l.iops_weight_ op (c); \
    result.max_net_bandwidth_ = l.max_net_bandwidth_ op (c); \
    result.net_bandwidth_weight_ = l.net_bandwidth_weight_ op (c); \
  } while (false)

ObServerResource::ObServerResource(
    const double max_cpu,
    const double min_cpu,
    const int64_t memory_size,
    const int64_t log_disk_size,
    const int64_t max_iops,
    const int64_t min_iops,
    const int64_t iops_weight,
    const int64_t max_net_bandwidth,
    const int64_t net_bandwidth_weight) :
    max_cpu_(max_cpu),
    min_cpu_(min_cpu),
    memory_size_(memory_size),
    log_disk_size_(log_disk_size),
    max_iops_(max_iops),
    min_iops_(min_iops),
    iops_weight_(iops_weight),
    max_net_bandwidth_(max_net_bandwidth),
    net_bandwidth_weight_(net_bandwidth_weight)
{
}

void ObServerResource::reset()
{
  reset_all_invalid();
  // following members are reset as DEFAULT values
  max_net_bandwidth_ = DEFAULT_NET_BANDWIDTH;
  net_bandwidth_weight_ = DEFAULT_NET_BANDWIDTH_WEIGHT;
}

void ObServerResource::reset_all_invalid()
{
  max_cpu_ = 0;
  min_cpu_ = 0;
  memory_size_ = 0;
  log_disk_size_ = INVALID_LOG_DISK_SIZE;
  max_iops_ = 0;
  min_iops_ = 0;
  iops_weight_ = INVALID_IOPS_WEIGHT;
  max_net_bandwidth_ = INVALID_NET_BANDWIDTH;
  net_bandwidth_weight_ = INVALID_NET_BANDWIDTH_WEIGHT;
}


bool ObServerResource::is_valid() const
{
  return is_max_cpu_valid()
      && is_min_cpu_valid()
      && max_cpu_ >= min_cpu_
      && is_memory_size_valid()
      && is_log_disk_size_valid()
      && is_max_iops_valid()
      && is_min_iops_valid()
      && max_iops_ >= min_iops_
      && is_iops_weight_valid()
      && is_max_net_bandwidth_valid()
      && is_net_bandwidth_weight_valid();
}

bool ObServerResource::is_valid_for_server() const
{
  return is_max_cpu_valid_for_server()
      && is_min_cpu_valid_for_server()
      && max_cpu_ >= min_cpu_
      && is_log_disk_size_valid_for_server()
      && is_max_iops_valid_for_server()
      && is_min_iops_valid_for_server()
      && max_iops_ >= min_iops_
      && is_iops_weight_valid_for_server()
      && is_max_net_bandwidth_valid_for_server()
      && is_net_bandwidth_weight_valid_for_server();
}

int64_t ObServerResource::get_default_log_disk_size(const int64_t memory_size)
{
  return max(memory_size * MEMORY_TO_LOG_DISK_FACTOR, SERVER_MIN_LOG_DISK_SIZE);
}

bool ObServerResource::is_log_disk_size_valid_for_server() const
{
  return 0 == log_disk_size_ || log_disk_size_ >= SERVER_MIN_LOG_DISK_SIZE;
}

int ObServerResource::init_and_check_cpu_(const ObServerResource &requested)
{
  int ret = OB_SUCCESS;
  const double server_min_cpu = SERVER_MIN_CPU;
  // max_cpu must be specified
  if (! requested.is_max_cpu_valid()) {
    ret = OB_MISS_ARGUMENT;
    LOG_WARN("missing max_cpu argument", KR(ret), K(requested));
    LOG_USER_ERROR(OB_MISS_ARGUMENT, "MAX_CPU");
  } else if (requested.max_cpu() < server_min_cpu) {
    ret = OB_RESOURCE_UNIT_VALUE_BELOW_LIMIT;
    LOG_WARN("max_cpu is below limit", KR(ret), K(requested), K(server_min_cpu));
    LOG_USER_ERROR(OB_RESOURCE_UNIT_VALUE_BELOW_LIMIT, "MAX_CPU", SERVER_MIN_CPU_STR);
  } else {
    // max_cpu valid
    max_cpu_ = requested.max_cpu();

    if (requested.is_min_cpu_valid()) {
      if (requested.min_cpu() < server_min_cpu) {
        ret = OB_RESOURCE_UNIT_VALUE_BELOW_LIMIT;
        LOG_WARN("min_cpu is below limit", KR(ret), K(requested), K(server_min_cpu));
        LOG_USER_ERROR(OB_RESOURCE_UNIT_VALUE_BELOW_LIMIT, "MIN_CPU", SERVER_MIN_CPU_STR);
      } else if (requested.min_cpu() > requested.max_cpu()) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("min_cpu greater than max_cpu", KR(ret), K(requested), K(server_min_cpu));
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, "MIN_CPU, MIN_CPU is greater than MAX_CPU");
      } else {
        // min_cpu valid
        min_cpu_ = requested.min_cpu();
      }
    } else {
      // user not specified, default min_cpu = max_cpu
      min_cpu_ = max_cpu_;
    }
  }

  LOG_INFO("ObServerResource init_and_check: CPU", KR(ret), K(max_cpu_), K(min_cpu_), K(requested),
      KPC(this), K(server_min_cpu));
  return ret;
}

int ObServerResource::init_and_check_mem_(const ObServerResource &requested)
{
  int ret = OB_SUCCESS;
  const int64_t server_min_memory = SERVER_MIN_MEMORY;
  // memory_size must be specified
  if (! requested.is_memory_size_valid()) {
    ret = OB_MISS_ARGUMENT;
    LOG_WARN("missing 'memory_size' argument", KR(ret), K(requested));
    LOG_USER_ERROR(OB_MISS_ARGUMENT, "MEMORY_SIZE");
  } else {
    // memory_size valid
    memory_size_ = requested.memory_size();
  }

  LOG_INFO("ObServerResource init_and_check: MEMORY", KR(ret), K(memory_size_), K(requested),
      KPC(this), K(server_min_memory));
  return ret;
}

int ObServerResource::init_and_check_log_disk_(const ObServerResource &requested)
{
  int ret = OB_SUCCESS;
  const int64_t server_min_log_disk_size = SERVER_MIN_LOG_DISK_SIZE;
  // user specify log_disk_size
  if (requested.is_log_disk_size_valid()) {
    if (0 == requested.log_disk_size()) {
      // log_disk_size is only allowed to be 0 for hidden SYS
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("Log_disk_size can only be specified as 0 for hidden SYS, not for normal server.", KR(ret), K(requested));
    } else if (requested.log_disk_size() < server_min_log_disk_size) {
      ret = OB_RESOURCE_UNIT_VALUE_BELOW_LIMIT;
      LOG_WARN("log_disk_size is below limit", KR(ret), K(requested),
          K(server_min_log_disk_size));
      ObCStringHelper helper;
      LOG_USER_ERROR(OB_RESOURCE_UNIT_VALUE_BELOW_LIMIT, "LOG_DISK_SIZE",
          helper.convert(server_min_log_disk_size));
    } else {
      // log_disk_size valid
      log_disk_size_ = requested.log_disk_size();
    }
  } else {
    // user not specify log_disk_size
    // use the default value
    log_disk_size_ = get_default_log_disk_size(memory_size_);
  }

  LOG_INFO("ObServerResource init_and_check: LOG_DISK", KR(ret), K(log_disk_size_), K(requested),
      KPC(this));
  return ret;
}

int ObServerResource::init_and_check_iops_(const ObServerResource &requested)
{
  int ret = OB_SUCCESS;
  const int64_t server_min_iops = SERVER_MIN_IOPS;
  // max_iops and min_iops are not specified, auto configure by min_cpu
  if (! requested.is_max_iops_valid() && ! requested.is_min_iops_valid()) {
    max_iops_ = get_default_iops();
    min_iops_ = max_iops_;

    // if iops_weight is not specified, auto configure by min_cpu
    // NOTE: default min_iops may be too large that exceeds disk IOPS upper limit.
    //       so configure iops_weight to support islation by iops weight
    if (! requested.is_iops_weight_valid()) {
      iops_weight_ = get_default_iops_weight(min_cpu_);
    } else {
      // user speicified
      iops_weight_ = requested.iops_weight();
    }
  } else {
    // at least one of min_iops and max_iops are specified, use user specified value
    max_iops_ = requested.max_iops();
    min_iops_ = requested.min_iops();

    // min_iops == max_iops if only one is specified
    if (! requested.is_max_iops_valid()) {
      max_iops_ = requested.min_iops();
    } else if (! requested.is_min_iops_valid()) {
      min_iops_ = requested.max_iops();
    }

    if (max_iops_ < min_iops_) {
      // it must be: two are all specified.
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("max_iops is little than min_iops", KR(ret), K(min_iops_), K(max_iops_), K(requested));
      LOG_USER_ERROR(OB_INVALID_ARGUMENT, "MAX_IOPS, MAX_IOPS is little than MIN_IOPS");
    } else if (min_iops_ < server_min_iops) {
      // NEED check which one is invalid
      if (requested.is_min_iops_valid()) {
        ret = OB_RESOURCE_UNIT_VALUE_BELOW_LIMIT;
        LOG_WARN("min_iops is below limit", KR(ret), K(min_iops_), K(max_iops_), K(requested), K(server_min_iops));
        ObCStringHelper helper;
        LOG_USER_ERROR(OB_RESOURCE_UNIT_VALUE_BELOW_LIMIT, "MIN_IOPS",
            helper.convert(server_min_iops));
      } else {
        ret = OB_RESOURCE_UNIT_VALUE_BELOW_LIMIT;
        LOG_WARN("max_iops is below limit", KR(ret), K(min_iops_), K(max_iops_), K(requested), K(server_min_iops));
        ObCStringHelper helper;
        LOG_USER_ERROR(OB_RESOURCE_UNIT_VALUE_BELOW_LIMIT, "MAX_IOPS",
            helper.convert(server_min_iops));
      }
    } else {
      // min_iops_ and max_iops_ are all valid
    }

    // init iops_weight
    if (OB_SUCCESS == ret) {
      if (requested.is_iops_weight_valid()) {
        // user specified
        iops_weight_ = requested.iops_weight();
      } else {
        // not specified, init to min_cpu
        iops_weight_ = get_default_iops_weight(min_cpu_);
      }
    }
  }

  LOG_INFO("ObServerResource init_and_check: IOPS", KR(ret), K(min_iops_), K(max_iops_),
      K(iops_weight_), K(requested), KPC(this), K(server_min_iops));
  return ret;
}

int ObServerResource::init_and_check_net_bandwidth_(const ObServerResource &requested)
{
  int ret = OB_SUCCESS;
  const int64_t server_min_net_bandwidth = SERVER_MIN_NET_BANDWIDTH;
  if (! requested.is_max_net_bandwidth_valid()) {
    // max_net_bandwidth not specified, set by DEFAULT value INT64_MAX
    max_net_bandwidth_ = get_default_net_bandwidth();

    // if net_bandwidth_weight is not specified, auto configure net_bandwidth_weight by min_cpu
    if (! requested.is_net_bandwidth_weight_valid()) {
      net_bandwidth_weight_ = get_default_net_bandwidth_weight(min_cpu_);
    } else {
      // user speicified
      net_bandwidth_weight_ = requested.net_bandwidth_weight();
    }
  } else {
    // max_net_bandwidth is specified, use user specified value
    if (requested.max_net_bandwidth_ < server_min_net_bandwidth) {
      ret = OB_RESOURCE_UNIT_VALUE_BELOW_LIMIT;
      LOG_WARN("max_net_bandwidth is below limit", KR(ret), K(requested), K(server_min_net_bandwidth));
      ObCStringHelper helper;
      LOG_USER_ERROR(OB_RESOURCE_UNIT_VALUE_BELOW_LIMIT, "MAX_NET_BANDWIDTH", helper.convert(server_min_net_bandwidth));
    } else {
      max_net_bandwidth_ = requested.max_net_bandwidth();

      // if net_bandwidth_weight is not specified, set as DEFAULT value
      if (! requested.is_net_bandwidth_weight_valid()) {
        // not specified, init to DEFAULT value
        net_bandwidth_weight_ = get_default_iops_weight(min_cpu_);
      } else {
        // user specified
        net_bandwidth_weight_ = requested.net_bandwidth_weight();
      }
    }
  }

  LOG_INFO("ObServerResource init_and_check: NET_BANDWIDTH", KR(ret), K(max_net_bandwidth_),
      K(net_bandwidth_weight_), K(requested), KPC(this));
  return ret;
}

int ObServerResource::init_and_check_valid(const ObServerResource &requested)
{
  int ret = OB_SUCCESS;

  // reset before init
  reset();

  // check CPU
  ret = init_and_check_cpu_(requested);

  // check MEMORY
  if (OB_SUCCESS == ret) {
    ret = init_and_check_mem_(requested);
  }

  // check LOGDISK
  if (OB_SUCCESS == ret) {
    ret = init_and_check_log_disk_(requested);
  }

  // check IOPS
  if (OB_SUCCESS == ret) {
    ret = init_and_check_iops_(requested);
  }

 // check NET_BANDWIDTH
  if (OB_SUCCESS == ret) {
    ret = init_and_check_net_bandwidth_(requested);
  }

  LOG_INFO("init server resource by user spec and check valid", KR(ret), K(requested), KPC(this));

  if (OB_FAIL(ret)) {
    // reset self after fail
    reset();
  }
  return ret;
}

int ObServerResource::update_and_check_cpu_(const ObServerResource &requested)
{
  int ret = OB_SUCCESS;
  const double server_min_cpu = SERVER_MIN_CPU;

  if (! is_valid_for_server()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("unexpected, should be valid for server", KR(ret), KPC(this));
  } else if (! requested.is_min_cpu_valid() && ! requested.is_max_cpu_valid()) {
    // not specified, need not update
  } else {
    double new_min_cpu = min_cpu_;
    double new_max_cpu = max_cpu_;

    // user specify min_cpu
    if (requested.is_min_cpu_valid()) {
      new_min_cpu = requested.min_cpu();
    }

    // user specify max_cpu
    if (requested.is_max_cpu_valid()) {
      new_max_cpu = requested.max_cpu();
    }

    if (new_max_cpu < new_min_cpu) {
      // specify max_cpu, report max_cpu error
      if (requested.is_max_cpu_valid()) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("max_cpu is little than min_cpu", KR(ret), K(new_max_cpu), K(new_min_cpu), K(requested),
            KPC(this), K(server_min_cpu));
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, "MAX_CPU, MAX_CPU is little than MIN_CPU");
      } else if (requested.is_min_cpu_valid()) {
        ret = OB_INVALID_ARGUMENT;
        // min_cpu is specified, report min_cpu error
        LOG_WARN("min_cpu greater than max_cpu", KR(ret), K(new_min_cpu), K(new_max_cpu), K(requested),
            KPC(this), K(server_min_cpu));
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, "MIN_CPU, MIN_CPU is greater than MAX_CPU");
      } else {
        ret = OB_ERR_UNEXPECTED;
        LOG_ERROR("cpu resource is invalid", K(new_max_cpu), K(new_min_cpu), K(new_max_cpu), K(requested), KPC(this));
      }
    } else if (new_min_cpu < server_min_cpu) {
      ret = OB_RESOURCE_UNIT_VALUE_BELOW_LIMIT;
      LOG_WARN("min_cpu is below limit", KR(ret), K(new_min_cpu), K(requested), KPC(this), K(server_min_cpu));
      LOG_USER_ERROR(OB_RESOURCE_UNIT_VALUE_BELOW_LIMIT, "MIN_CPU", SERVER_MIN_CPU_STR);
    } else {
      // all is valid
      min_cpu_ = new_min_cpu;
      max_cpu_ = new_max_cpu;
    }

    LOG_INFO("ObServerResource update_and_check: CPU", KR(ret), K(max_cpu_), K(min_cpu_), K(requested),
        KPC(this));
  }

  return ret;
}

int ObServerResource::update_and_check_mem_(const ObServerResource &requested)
{
  int ret = OB_SUCCESS;
  const int64_t server_min_memory = SERVER_MIN_MEMORY;

  if (! requested.is_memory_size_valid()) {
    // memory not specified, need not update
  } else {
    if (requested.memory_size() < server_min_memory) {
      ret = OB_RESOURCE_UNIT_VALUE_BELOW_LIMIT;
      LOG_WARN("memory_size is below limit", KR(ret), K(requested), K(server_min_memory));
      ObCStringHelper helper;
      LOG_USER_ERROR(OB_RESOURCE_UNIT_VALUE_BELOW_LIMIT, "MEMORY_SIZE",
          helper.convert(server_min_memory));
    } else {
      // memory_size valid
      memory_size_ = requested.memory_size();
    }

    LOG_INFO("ObServerResource update_and_check: MEMORY", KR(ret), K(memory_size_), K(requested),
        KPC(this));
  }
  return ret;
}

int ObServerResource::update_and_check_log_disk_(const ObServerResource &requested)
{
  int ret = OB_SUCCESS;
  const int64_t server_min_log_disk_size = SERVER_MIN_LOG_DISK_SIZE;
  if (! requested.is_log_disk_size_valid()) {
    // not specified, need not update
  } else {
    if (0 == requested.log_disk_size()) {
      // log_disk_size is only allowed to be 0 for hidden SYS
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("Log_disk_size can only be specified as 0 for hidden SYS, not for normal server.", KR(ret), K(requested));
    } else if (requested.log_disk_size() < server_min_log_disk_size) {
      ret = OB_RESOURCE_UNIT_VALUE_BELOW_LIMIT;
      LOG_WARN("log_disk_size is below limit", KR(ret), K(requested), K(server_min_log_disk_size));
      ObCStringHelper helper;
      LOG_USER_ERROR(OB_RESOURCE_UNIT_VALUE_BELOW_LIMIT, "LOG_DISK_SIZE",
          helper.convert(server_min_log_disk_size));
    } else {
      log_disk_size_ = requested.log_disk_size();
    }

    LOG_INFO("ObServerResource update_and_check: LOG_DISK", KR(ret), K(log_disk_size_), K(requested),
        KPC(this));
  }
  return ret;
}

int ObServerResource::update_and_check_iops_(const ObServerResource &requested)
{
  int ret = OB_SUCCESS;
  const int64_t server_min_iops = SERVER_MIN_IOPS;

  if (! requested.is_max_iops_valid() &&
      ! requested.is_min_iops_valid() &&
      ! requested.is_iops_weight_valid()) {
    // not specified, need not update
  } else {
    int64_t new_min_iops = min_iops_;
    int64_t new_max_iops = max_iops_;

    if (requested.is_max_iops_valid()) {
      new_max_iops = requested.max_iops();
    }

    if (requested.is_min_iops_valid()) {
      new_min_iops = requested.min_iops();
    }

    if (new_max_iops < new_min_iops) {
      if (requested.is_max_iops_valid()) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("max_iops is little than min_iops", KR(ret), K(new_min_iops), K(new_max_iops),
            K(requested), KPC(this));
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, "MAX_IOPS, MAX_IOPS is little than MIN_IOPS");
      } else if (requested.is_min_iops_valid()) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("min_iops is greater than max_iops", KR(ret), K(new_min_iops), K(new_max_iops),
            K(requested), KPC(this));
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, "MIN_IOPS, MIN_IOPS is greater than MAX_IOPS");
      } else {
        ret = OB_ERR_UNEXPECTED;
        LOG_ERROR("unexpected, user not specify max_iops and min_iops, but max_iops < min_iops",
            K(new_max_iops), K(new_min_iops), K(requested), KPC(this));
      }
    } else if (new_min_iops < server_min_iops) {
      // min_iops must be specified, so report error on min_iops
      ret = OB_RESOURCE_UNIT_VALUE_BELOW_LIMIT;
      LOG_WARN("min_iops is below limit", KR(ret), K(min_iops_), K(max_iops_), K(requested), K(server_min_iops));
      ObCStringHelper helper;
      LOG_USER_ERROR(OB_RESOURCE_UNIT_VALUE_BELOW_LIMIT, "MIN_IOPS", helper.convert(server_min_iops));
    } else {
      // all valid
      min_iops_ = new_min_iops;
      max_iops_ = new_max_iops;
    }

    if (OB_SUCCESS == ret) {
      if (requested.is_iops_weight_valid()) {
        // user specified
        iops_weight_ = requested.iops_weight();
      }
    }
  }

  LOG_INFO("ObServerResource update_and_check: IOPS", KR(ret), K(min_iops_), K(max_iops_),
      K(iops_weight_), K(requested), KPC(this));
  return ret;
}

int ObServerResource::update_and_check_net_bandwidth_(const ObServerResource &requested)
{
  int ret = OB_SUCCESS;
  const int64_t server_min_net_bandwidth = SERVER_MIN_NET_BANDWIDTH;
  if (requested.is_max_net_bandwidth_valid()) {
    if (requested.max_net_bandwidth_ < server_min_net_bandwidth) {
      ret = OB_RESOURCE_UNIT_VALUE_BELOW_LIMIT;
      LOG_WARN("max_net_bandwidth is below limit", KR(ret), K(requested), K(server_min_net_bandwidth));
      ObCStringHelper helper;
      LOG_USER_ERROR(OB_RESOURCE_UNIT_VALUE_BELOW_LIMIT, "MAX_NET_BANDWIDTH", helper.convert(server_min_net_bandwidth));
    } else {
      // user specified
      max_net_bandwidth_ = requested.max_net_bandwidth();
    }
  }
  if (OB_SUCC(ret)) {
    if (requested.is_net_bandwidth_weight_valid()) {
      // user specified
      net_bandwidth_weight_ = requested.net_bandwidth_weight();
    }
  }

  LOG_INFO("ObServerResource update_and_check: NET_BANDWIDTH", KR(ret), K(max_net_bandwidth_),
      K(net_bandwidth_weight_), K(requested), KPC(this));
  return ret;
}

int ObServerResource::update_and_check_valid(const ObServerResource &requested)
{
  int ret = OB_SUCCESS;

  // generate a self copy
  const ObServerResource self_copy = *this;

  if (OB_UNLIKELY(!is_valid_for_server())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("self is not valid for server", KR(ret), KPC(this));
  }

  // check CPU
  if (OB_SUCCESS == ret) {
    ret = update_and_check_cpu_(requested);
  }

  // check MEMORY
  if (OB_SUCCESS == ret) {
    ret = update_and_check_mem_(requested);
  }

  // check LOG_DISK_SIZE
  if (OB_SUCCESS == ret) {
    ret = update_and_check_log_disk_(requested);
  }

  // check IOPS
  if (OB_SUCCESS == ret) {
    ret = update_and_check_iops_(requested);
  }

  // check NET_BANDWIDTH
  if (OB_SUCCESS == ret) {
    ret = update_and_check_net_bandwidth_(requested);
  }

  LOG_INFO("update server resource by user spec and check valid for server", KR(ret), K(requested), KPC(this));

  if (OB_FAIL(ret)) {
    // reset self after fail
    *this = self_copy;
  }
  return ret;
}

ObServerResource &ObServerResource::operator=(const ObServerResource &other)
{
  if (this != &other) {
    max_cpu_ = other.max_cpu_;
    min_cpu_ = other.min_cpu_;
    memory_size_ = other.memory_size_;
    log_disk_size_ = other.log_disk_size_;
    max_iops_ = other.max_iops_;
    min_iops_ = other.min_iops_;
    iops_weight_ = other.iops_weight_;
    max_net_bandwidth_ = other.max_net_bandwidth_;
    net_bandwidth_weight_ = other.net_bandwidth_weight_;
  }
  return *this;
}

ObServerResource ObServerResource::operator+(const ObServerResource &r) const
{
  ObServerResource result;
  CALCULATE_CONFIG((*this), +, r, result);
  return result;
}

ObServerResource ObServerResource::operator-(const ObServerResource &r) const
{
  ObServerResource result;
  CALCULATE_CONFIG((*this), -, r, result);
  return result;
}

ObServerResource &ObServerResource::operator+=(const ObServerResource &r)
{
  CALCULATE_CONFIG((*this), +, r, (*this));
  return *this;
}

ObServerResource &ObServerResource::operator-=(const ObServerResource &r)
{
  CALCULATE_CONFIG((*this), -, r, (*this));
  return *this;
}

ObServerResource ObServerResource::operator*(const int64_t count) const
{
  ObServerResource result;
  CALCULATE_CONFIG_WITH_CONSTANT((*this), *, count, result);
  return result;
}

#undef CALCULATE_CONFIG

#define COMPARE_INT_CONFIG(left, op, right) \
        ((left).memory_size_ op (right).memory_size_) \
        && ((left).log_disk_size_ op (right).log_disk_size_) \
        && ((left).max_iops_ op (right).max_iops_) \
        && ((left).min_iops_ op (right).min_iops_) \
        && ((left).iops_weight_ op (right).iops_weight_) \
        && ((left).max_net_bandwidth_ op (right).max_net_bandwidth_) \
        && ((left).net_bandwidth_weight_ op (right).net_bandwidth_weight_)

bool ObServerResource::operator==(const ObServerResource &config) const
{
  bool result = false;
  result = std::fabs(this->max_cpu_ - config.max_cpu_) < CPU_EPSILON
      && std::fabs(this->min_cpu_ - config.min_cpu_) < CPU_EPSILON
      && COMPARE_INT_CONFIG((*this), ==, config);
  return result;
}

#undef COMPARE_INT_CONFIG

DEF_TO_STRING(ObServerResource)
{
  int64_t pos = 0;
  J_OBJ_START();
  // cpu, mem
  (void)databuff_printf(buf, buf_len, pos, "min_cpu:%.6g, max_cpu:%.6g, memory_size:\"%.9gGB\", ",
      min_cpu_, max_cpu_, (double)memory_size_/1024/1024/1024);
  // log_disk
  if (log_disk_size_ > 0) {
    (void)databuff_printf(buf, buf_len, pos, "log_disk_size:\"%.9gGB\", ", (double)log_disk_size_/1024/1024/1024);
  } else {
    (void)databuff_printf(buf, buf_len, pos, "log_disk_size:%ld, ", log_disk_size_);
  }
  // iops
  (void)databuff_printf(buf, buf_len, pos, "min_iops:%ld, max_iops:%ld, iops_weight:%ld, ", min_iops_, max_iops_, iops_weight_);
  // net bandwidth
  if (INT64_MAX == max_net_bandwidth_) {
    (void)databuff_printf(buf, buf_len, pos, "max_net_bandwidth:INT64_MAX, ");
  } else {
    (void)databuff_printf(buf, buf_len, pos, "max_net_bandwidth:\"%.9gGB\", ", (double)max_net_bandwidth_/1024/1024/1024);
  }
  (void)databuff_printf(buf, buf_len, pos, "net_bandwidth_weight:%ld, ", net_bandwidth_weight_);
  J_OBJ_END();
  return pos;
}

OB_SERIALIZE_MEMBER(ObServerResource,
                    max_cpu_,
                    min_cpu_,
                    memory_size_,
                    log_disk_size_,
                    max_iops_,
                    min_iops_,
                    iops_weight_,
                    max_net_bandwidth_,
                    net_bandwidth_weight_);


bool ObServerResource::has_expanded_resource_than(const ObServerResource &other) const
{
  // check if any of max_cpu, min_cpu, memory_size, log_disk_size is greater than other
  bool b_ret = false;
  if ((is_max_cpu_valid() && other.is_max_cpu_valid() && max_cpu_ > other.max_cpu())
      || (is_min_cpu_valid() && other.is_min_cpu_valid() && min_cpu_ > other.min_cpu())
      || (is_memory_size_valid() && other.is_memory_size_valid() && memory_size_ > other.memory_size())
      || (is_log_disk_size_valid() && other.is_log_disk_size_valid() && log_disk_size_ > other.log_disk_size()))
  {
    b_ret = true;
  } else {
    b_ret = false;
  }
  return b_ret;
}

bool ObServerResource::has_shrunk_resource_than(const ObServerResource &other) const
{
  // check if any of max_cpu, min_cpu, memory_size, log_disk_size is smaller than other
  bool b_ret = false;
  if ((is_max_cpu_valid() && other.is_max_cpu_valid() && max_cpu_ < other.max_cpu())
      || (is_min_cpu_valid() && other.is_min_cpu_valid() && min_cpu_ < other.min_cpu())
      || (is_memory_size_valid() && other.is_memory_size_valid() && memory_size_ < other.memory_size())
      || (is_log_disk_size_valid() && other.is_log_disk_size_valid() && log_disk_size_ < other.log_disk_size()))
  {
    b_ret = true;
  } else {
    b_ret = false;
  }
  return b_ret;
}

int ObServerResource::generate_default(const int64_t log_disk_size)
{
  int ret = OB_SUCCESS;

  reset();
  memory_size_ = GMEMCONF.get_server_memory_budget();
  max_cpu_ = GCONF.get_server_default_max_cpu();
  min_cpu_ = GCONF.get_server_default_min_cpu();
  log_disk_size_ = log_disk_size;
  max_iops_ = get_default_iops();
  min_iops_ = max_iops_;
  iops_weight_ = get_default_iops_weight(min_cpu_);
  max_net_bandwidth_ = get_default_net_bandwidth();
  net_bandwidth_weight_ = get_default_net_bandwidth_weight(min_cpu_);
  if (OB_UNLIKELY(!is_valid_for_server())) {
    ret = OB_RESOURCE_UNIT_VALUE_INVALID;
    LOG_ERROR("default server resource is invalid", KR(ret), KPC(this));
  }

  LOG_INFO("generate default server resource", KR(ret), KPC(this), K(lbt()));

  if (OB_FAIL(ret)) {
    reset();
  }
  return ret;
}

} // namespace share
} // namespace oceanbase
