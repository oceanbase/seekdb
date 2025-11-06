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

#ifndef MOCK_OB_BASE_STORAGE_LOGGER_H_
#define MOCK_OB_BASE_STORAGE_LOGGER_H_

#include <gmock/gmock.h>

namespace oceanbase {
namespace blocksstable {

class MockObBaseStorageLogger : public ObBaseStorageLogger {
 public:
  MOCK_METHOD1(begin, int(const enum LogCommand cmd));
  MOCK_METHOD4(write_log, int(ActiveTransEntry &trans_entry, const int64_t subcmd, const ObStorageLogAttribute &log_attr, ObIBaseStorageLogEntry &data));
  MOCK_METHOD3(write_log, int(const int64_t subcmd, const ObStorageLogAttribute &log_attr, ObIBaseStorageLogEntry &data));
  MOCK_METHOD1(commit, int(int64_t &log_seq_num));
  MOCK_METHOD0(abort, int());
};

}  // namespace storage
}  // namespace oceanbase


#endif
