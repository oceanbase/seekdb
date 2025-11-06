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

#ifndef OCEANBASE_LIB_OB_STORAGE_IO_USAGE_REPORTER_H
#define OCEANBASE_LIB_OB_STORAGE_IO_USAGE_REPORTER_H

#include "lib/ob_define.h"
// #include "share/ob_thread_pool.h"
#include "share/ob_thread_mgr.h"
#include "common/storage/ob_device_common.h"
#include "observer/net/ob_shared_storage_net_throt_rpc_struct.h"

namespace oceanbase
{
namespace common
{
class ObMySQLTransaction;
class ObString;
class ObMySQLProxy;
}
namespace share
{

class ObStorageIOUsageRepoter
{
public:
    class TenantIOUsageReportTask : public common::ObTimerTask
    {
    public:
        TenantIOUsageReportTask() : reporter_(nullptr) {}
        virtual ~TenantIOUsageReportTask() {}
        int init(ObStorageIOUsageRepoter *reporter);
        virtual void runTimerTask(void) override;

        static const uint64_t SLEEP_SECONDS = 5 * 1000L * 1000L; // 5s
        static const uint64_t BOOTSTRAP_PERIOD = 20 * 1000L * 1000L; //5s
    private:
        DISALLOW_COPY_AND_ASSIGN(TenantIOUsageReportTask);
        ObStorageIOUsageRepoter *reporter_;
    };
private:
    class UpdateIOUsageFunctor
    {
    public:
        UpdateIOUsageFunctor(const int64_t tenant_id): tenant_id_(tenant_id) {}
        ~UpdateIOUsageFunctor() {}
        int operator()(common::hash::HashMapPair<oceanbase::common::ObTrafficControl::ObIORecordKey,
                                                  common::ObTrafficControl::ObSharedDeviceIORecord> &entry);
    private:
        int64_t tenant_id_;
    };
public:
    explicit ObStorageIOUsageRepoter() : is_inited_(false),
                                         io_usage_report_task_()
    {}
    ~ObStorageIOUsageRepoter() { destroy(); }
    static int mtl_init(ObStorageIOUsageRepoter *&reporter);
    int init();
    void reset();
    void destroy();
    int start();
    void stop();
    void wait();
    int report_tenant_io_usage(const uint64_t tenant_id);
    int cancel_report_task();
    static const ObString &get_storage_mod_str(const ObStorageInfoType table_type);
    static const ObString &get_type_str(const obrpc::ResourceType resource_type);

    TO_STRING_KV(K_(is_inited));

private:
    bool is_inited_;
    TenantIOUsageReportTask io_usage_report_task_;
};

}
}
#endif
