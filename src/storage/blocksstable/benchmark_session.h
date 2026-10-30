/*
 * Copyright (c) 2026 OceanBase.
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

#pragma once

#include <mutex>
#include <optional>
#include <vector>
#include "rust/storage_io.h"
#include "storage/blocksstable/ob_macro_block_handle.h"

namespace oceanbase { namespace storage {

class BenchmarkSession final
{
public:
  static BenchmarkSession &get_instance();
  int bind();
  int start();
  int status(int64_t &started, int64_t &finished, int &result) const;
  void stop_and_drain();
  void shutdown();
  static int select_target_count(int64_t free_count, int64_t total_count, int64_t &selected);
private:
  BenchmarkSession() = default;
  void retire_locked();
  std::mutex session_mutex_;
  std::vector<blocksstable::ObMacroBlockHandle> blocks_;
  std::optional<rust::Box<storage_io::BenchmarkRunSlot>> run_;
};

}}
