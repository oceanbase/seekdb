/*
 * Copyright (c) 2025 OceanBase.
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#define USING_LOG_PREFIX STORAGE

#include "data_plane/access/ob_data_access_retry.h"
#include "lib/worker.h"

namespace oceanbase
{
namespace data_plane
{

int ObDataAccessRetry::wait(const int64_t retry_count)
{
  int ret = common::OB_SUCCESS;
  const uint32_t timeout_factor = static_cast<uint32_t>(retry_count > 100 ? 100 : retry_count);
  const int64_t retry_sleep_us = 10000L * timeout_factor;
  const int64_t sleep_us = MIN(retry_sleep_us, THIS_WORKER.get_timeout_remain());
  if (sleep_us > 0) {
    LOG_INFO("data access retry will sleep", K(sleep_us), K(THIS_WORKER.get_timeout_remain()));
    THIS_WORKER.sched_wait();
    ob_usleep(static_cast<uint32_t>(sleep_us));
    THIS_WORKER.sched_run();
    if (THIS_WORKER.is_timeout()) {
      ret = common::OB_TIMEOUT;
      LOG_WARN("worker timed out after data access retry sleep", K(ret));
    }
  }
  return ret;
}

} // namespace data_plane
} // namespace oceanbase
