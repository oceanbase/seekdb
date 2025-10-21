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
#ifndef DEV_SRC_SQL_DAS_OB_DAS_RETRY_CTRL_H_
#define DEV_SRC_SQL_DAS_OB_DAS_RETRY_CTRL_H_

namespace oceanbase {
namespace sql {
class ObIDASTaskOp;
class ObDASRef;
class ObDASRetryCtrl
{
public:
  /**
   * retry_func: is to determine whether tablet-level retry is necessary based on the status of the task
   *             and maybe change some status of the das task in this function,
   *             such as refresh_partition_location_cache,
   *             DAS retry only can be attempted within the current thread
   * [param in]: ObDASRef &: the das context reference
   * [param in/out] ObIDASTaskOp &: which das task need to retry
   * [param out] bool &: whether need DAS retry
   * */
  typedef void (*retry_func)(ObDASRef &, ObIDASTaskOp &, bool &);

  static void tablet_location_retry_proc(ObDASRef &, ObIDASTaskOp &, bool &);
  static void tablet_nothing_readable_proc(ObDASRef &, ObIDASTaskOp &, bool &);
  static void task_network_retry_proc(ObDASRef &, ObIDASTaskOp &, bool &);
};

}  // namespace sql
}  // namespace oceanbase

#endif /* DEV_SRC_SQL_DAS_OB_DAS_RETRY_CTRL_H_ */
