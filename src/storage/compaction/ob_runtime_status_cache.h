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
#ifndef OB_STORAGE_COMPACTION_RUNTIME_STATUS_CACHE_H_
#define OB_STORAGE_COMPACTION_RUNTIME_STATUS_CACHE_H_
#include "lib/utility/ob_print_utils.h"
namespace oceanbase
{
namespace compaction
{

struct ObRuntimeStatusCache final
{
  ObRuntimeStatusCache()
    : is_inited_(false),
      during_restore_(false),
      enable_adaptive_compaction_(false)
  {}
  ~ObRuntimeStatusCache() {}
  void reset()
  {
    is_inited_ = false;
    during_restore_ = false;
    enable_adaptive_compaction_ = false;
  }
  int during_restore(bool &during_restore) const;
  bool is_inited() const { return is_inited_; }
  bool should_skip_merge() const;
  bool enable_adaptive_compaction() const { return enable_adaptive_compaction_; }
  int init_or_refresh();
  int refresh_runtime_config(const bool enable_adaptive_compaction);

  TO_STRING_KV(K_(is_inited), K_(during_restore), K_(enable_adaptive_compaction));

private:
  int inner_refresh_restore_status();
  static const int64_t REFRESH_SERVER_RUNTIME_STATUS_INTERVAL = 30 * 1000 * 1000L; // 30s
  bool is_inited_;
  bool during_restore_;
  // Runtime configuration remains valid while the restore status is being initialized.
  bool enable_adaptive_compaction_;
};

} // namespace compaction
} // namespace oceanbase

#endif // OB_STORAGE_COMPACTION_RUNTIME_STATUS_CACHE_H_
