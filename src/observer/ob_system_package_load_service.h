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
#ifndef _OCEANBASE_ROOTSERVER_OB_SYSTEM_PACKAGE_LOAD_SERVICE_H_
#define _OCEANBASE_ROOTSERVER_OB_SYSTEM_PACKAGE_LOAD_SERVICE_H_ 1

#include "lib/utility/ob_macro_utils.h"
#include "lib/task/ob_timer.h"
#include "logservice/ob_log_base_type.h"
#include "observer/ob_system_package_load_task.h"
#include "share/scn.h"

namespace oceanbase
{
namespace rootserver
{
class ObSystemPackageLoadService : public logservice::ObILocalLogHandler,
                                   public logservice::ObICheckpointSubHandler,
                                   public logservice::ObIReplaySubHandler
{
public:
  ObSystemPackageLoadService();
  virtual ~ObSystemPackageLoadService() {}

  static int server_module_init(ObSystemPackageLoadService *&service);

  int init();
  int start();
  void stop();
  int wait();
  void destroy();

  // for ObILocalLogHandler
  virtual int activate() override;
  virtual void deactivate() override;

  // for checkpoint/replay
  virtual share::SCN get_rec_scn() override { return share::SCN::max_scn(); }
  virtual int flush(share::SCN &rec_scn) override { return OB_SUCCESS; }
  virtual int replay(const void *buffer, const int64_t nbytes, const palf::LSN &lsn, const share::SCN &scn) override
  {
    int ret = OB_SUCCESS;
    UNUSEDx(buffer, nbytes, lsn, scn);
    return ret;
  }

private:
  bool inited_;
  common::ObTimer timer_;
  ObSystemPackageLoadTask task_;

private:
  DISALLOW_COPY_AND_ASSIGN(ObSystemPackageLoadService);
};
}
}

#endif // _OCEANBASE_ROOTSERVER_OB_SYSTEM_PACKAGE_LOAD_SERVICE_H_
