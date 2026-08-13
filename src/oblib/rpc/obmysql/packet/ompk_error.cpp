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

#define USING_LOG_PREFIX RPC_OBMYSQL

#include "rpc/obmysql/packet/ompk_error.h"

using namespace oceanbase::common;
using namespace oceanbase::obmysql;

OMPKError::OMPKError()
{
  errcode_ = 2000;
  sqlstate_ = ObString::make_string("HY000");
  message_ = ObString::make_string("");
}

OMPKError::~OMPKError()
{
}

int OMPKError::set_message(const ObString &message)
{
  int ret = OB_SUCCESS;
  if (NULL == message.ptr() || 0 > message.length()) {
    LOG_WARN("invalid argument message", K(message));
    ret = OB_INVALID_ARGUMENT;
  } else {
    message_.assign(const_cast<char *>(message.ptr()), message.length());
  }
  return ret;
}

int OMPKError::set_sqlstate(const char *sqlstate)
{
  int ret = OB_SUCCESS;
  if (SQLSTATE_SIZE == strlen(sqlstate)) {
    sqlstate_ = ObString::make_string(sqlstate);
  } else {
    ret = OB_INVALID_ARGUMENT;
  }
  return ret;
}

int64_t OMPKError::to_string(char *buf, const int64_t buf_len) const
{
  int64_t pos = 0;
  J_OBJ_START();
  J_KV(K_(errcode), K_(sqlstate), K_(message));
  J_OBJ_END();
  return pos;
}
