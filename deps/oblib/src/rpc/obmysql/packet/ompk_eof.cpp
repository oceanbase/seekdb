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

#include "rpc/obmysql/packet/ompk_eof.h"

using namespace oceanbase::common;
using namespace oceanbase::obmysql;

OMPKEOF::OMPKEOF()
    : field_count_(0xfe),
      warning_count_(0),
      server_status_()
{}

OMPKEOF::~OMPKEOF()
{}

int64_t OMPKEOF::to_string(char *buf, const int64_t buf_len) const
{
  int64_t pos = 0;
  J_OBJ_START();
  J_KV(K_(field_count), K_(warning_count), K_(server_status_.flags));
  J_OBJ_END();
  return pos;
}
