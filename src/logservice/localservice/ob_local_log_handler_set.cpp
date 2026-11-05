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

#include "ob_local_log_handler_set.h"
namespace oceanbase
{
using namespace common;
namespace logservice
{
ObLocalLogHandlerSet::ObLocalLogHandlerSet(): lock_(common::ObLatchIds::RCS_LOCK),
                                            local_log_handlers_(),
                                            local_log_handler_active_()
{
  reset();
}

ObLocalLogHandlerSet::~ObLocalLogHandlerSet()
{
  reset();
}

void ObLocalLogHandlerSet::reset()
{
  for (int i = 0; i < ObLogBaseType::MAX_LOG_BASE_TYPE; i++) {
    local_log_handlers_[i] = NULL;
    local_log_handler_active_[i] = false;
  }
}

int ObLocalLogHandlerSet::register_handler(const ObLogBaseType &type,
                                          ObILocalLogHandler *handler)
{
  int ret = OB_SUCCESS;
  ObSpinLockGuard guard(lock_);
  if (false == is_valid_log_base_type(type)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    local_log_handlers_[type] = handler;
    local_log_handler_active_[type] = false;
    CLOG_LOG(INFO, "register local log handler success", K(ret), K(type), KP(handler));
  }
  return ret;
}

void ObLocalLogHandlerSet::unregister_handler(const ObLogBaseType &type)
{
  ObSpinLockGuard guard(lock_);
  if (true == is_valid_log_base_type(type)) {
    local_log_handlers_[type] = NULL;
    local_log_handler_active_[type] = false;
    CLOG_LOG(INFO, "unregister_handler success", K(type));
  }
}

void ObLocalLogHandlerSet::deactivate()
{
  ObSpinLockGuard guard(lock_);
  for (int i = 0; i < ObLogBaseType::MAX_LOG_BASE_TYPE; i++) {
    ObILocalLogHandler *handler = local_log_handlers_[i];
    char local_log_handler_str[OB_LOG_BASE_TYPE_STR_MAX_LEN] = {'\0'};
    ObLogBaseType base_type = static_cast<ObLogBaseType>(i);
    bool has_defined_to_string = false;
    if (OB_SUCCESS == log_base_type_to_string(base_type, local_log_handler_str,
          OB_LOG_BASE_TYPE_STR_MAX_LEN)) {
      has_defined_to_string = true;
    }
    if (NULL != handler) {
      handler->deactivate();
      local_log_handler_active_[i] = false;
      CLOG_LOG(INFO, "deactivate local log handler",
          "cursor", i, "name", has_defined_to_string ? local_log_handler_str : "hasn't define to string");
    }
  }
}

int ObLocalLogHandlerSet::activate()
{
  return activate_(ObLogBaseType::MAX_LOG_BASE_TYPE);
}

int ObLocalLogHandlerSet::activate_except(const ObLogBaseType excluded_type)
{
  return activate_(excluded_type);
}

int ObLocalLogHandlerSet::activate_handler(const ObLogBaseType type)
{
  int ret = OB_SUCCESS;
  ObSpinLockGuard guard(lock_);
  if (!is_valid_log_base_type(type)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (local_log_handler_active_[type]) {
  } else if (OB_ISNULL(local_log_handlers_[type])) {
    ret = OB_ENTRY_NOT_EXIST;
  } else if (OB_FAIL(local_log_handlers_[type]->activate())) {
    CLOG_LOG(WARN, "activate local log handler failed", K(ret), K(type));
  } else {
    local_log_handler_active_[type] = true;
    CLOG_LOG(INFO, "activate local log handler", K(type));
  }
  return ret;
}

int ObLocalLogHandlerSet::activate_(const ObLogBaseType excluded_type)
{
  int ret = OB_SUCCESS;
  CLOG_LOG(INFO, "ObLocalLogHandlerSet::activate called");
  ObSpinLockGuard guard(lock_);
  for (int i = 0; i < ObLogBaseType::MAX_LOG_BASE_TYPE && OB_SUCC(ret); i++) {
    ObILocalLogHandler *handler = local_log_handlers_[i];
    char local_log_handler_str[OB_LOG_BASE_TYPE_STR_MAX_LEN] = {'\0'};
    ObLogBaseType base_type = static_cast<ObLogBaseType>(i);
    bool has_defined_to_string = false;
    if (OB_SUCCESS == log_base_type_to_string(base_type, local_log_handler_str,
          OB_LOG_BASE_TYPE_STR_MAX_LEN)) {
      has_defined_to_string = true;
    }
    if (excluded_type == base_type) {
      CLOG_LOG(INFO, "defer local log handler activation",
          "cursor", i, "name", has_defined_to_string ? local_log_handler_str : "hasn't define to string");
    } else if (local_log_handler_active_[i]) {
    } else if (NULL == handler) {
    } else if (OB_FAIL(handler->activate())) {
    } else {
      local_log_handler_active_[i] = true;
      CLOG_LOG(INFO, "activate local log handler",
          "cursor", i, "name", has_defined_to_string ? local_log_handler_str : "hasn't define to string");
    }
  }
  CLOG_LOG(INFO, "ObLocalLogHandlerSet::activate finished", K(ret));
  return ret;
}

} // end namespace logservice
} // end namespace oceanbase
