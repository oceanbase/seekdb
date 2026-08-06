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

#define USING_LOG_PREFIX TEST
#include <getopt.h>
#include <gtest/gtest.h>
#define protected public
#define private public
#include "observer/scheduler/ob_dag_warning_history_mgr.h"
#include "observer/scheduler/ob_dag_scheduler.h"
#include "share/rc/ob_module_provider.h"

namespace oceanbase
{
class TestDagModuleProvider : public share::ObIModuleProvider
{
public:
  TestDagModuleProvider() : dag_scheduler_(nullptr), dag_history_mgr_(nullptr), diagnose_mgr_(nullptr) {}
  virtual share::ObDagScheduler *dag_scheduler() override { return dag_scheduler_; }
  virtual share::ObDagWarningHistoryManager *dag_warning_history_manager() override { return dag_history_mgr_; }
  virtual compaction::ObDiagnoseTabletMgr *diagnose_tablet_mgr() override { return diagnose_mgr_; }
  share::ObDagScheduler *dag_scheduler_;
  share::ObDagWarningHistoryManager *dag_history_mgr_;
  compaction::ObDiagnoseTabletMgr *diagnose_mgr_;
};
} // namespace oceanbase

int64_t dag_cnt = 1;
int64_t stress_time= 1; // 100ms
char log_level[20] = "INFO";
uint32_t time_slice = 1000;
uint64_t check_waiting_list_period = 1000;
uint32_t sleep_slice = 2 * time_slice;
const int64_t CHECK_TIMEOUT = 1 * 1000 * 1000;

#define CHECK_EQ_UTIL_TIMEOUT(expected, expr) \
  { \
    int64_t start_time = oceanbase::common::ObTimeUtility::current_time(); \
    auto expr_result = (expr); \
    do { \
      if ((expected) == (expr_result)) { \
        break; \
      } else { \
        expr_result = (expr); \
      }\
    } while(oceanbase::common::ObTimeUtility::current_time() - start_time < CHECK_TIMEOUT); \
    EXPECT_EQ((expected), (expr_result)); \
  }

namespace oceanbase
{
using namespace common;
using namespace share;
using namespace lib;

namespace unittest
{

class TestDagScheduler : public ::testing::Test
{
public:
  TestDagScheduler()
    : scheduler_(nullptr),
      dag_history_mgr_(nullptr),
      diagnose_mgr_(nullptr),
      old_mp_(nullptr)
  {}
  ~TestDagScheduler() {}
  void SetUp()
  {
    scheduler_ = OB_NEW(ObDagScheduler, ObModIds::TEST);
    dag_history_mgr_ = OB_NEW(ObDagWarningHistoryManager, ObModIds::TEST);
    diagnose_mgr_ = OB_NEW(compaction::ObDiagnoseTabletMgr, ObModIds::TEST);

    provider_.dag_scheduler_ = scheduler_;
    provider_.dag_history_mgr_ = dag_history_mgr_;
    provider_.diagnose_mgr_ = diagnose_mgr_;
    old_mp_ = share::g_mp;
    share::g_mp = &provider_;

    ObMallocAllocator *ma = ObMallocAllocator::get_instance();
    ASSERT_EQ(OB_SUCCESS, ma->set_allocator_limit(1LL << 30));

    ASSERT_EQ(OB_SUCCESS, scheduler_->init(time_slice, check_waiting_list_period, MAX_DAG_CNT));
    ASSERT_EQ(OB_SUCCESS, diagnose_mgr_->init());
    ObAddr addr(1683068975,9999);
    if (OB_SUCCESS != (ObSysTaskStatMgr::get_instance().set_self_addr(addr))) {
      COMMON_LOG_RET(WARN, OB_ERROR, "failed to add sys task", K(addr));
    }
  }
  void TearDown()
  {
    scheduler_->destroy();
    scheduler_ = nullptr;
    dag_history_mgr_->~ObDagWarningHistoryManager();
    dag_history_mgr_ = nullptr;
    diagnose_mgr_->destroy();
    diagnose_mgr_ = nullptr;
    share::g_mp = old_mp_;
  }
private:
  const static int64_t MAX_DAG_CNT = 64;
  ObDagScheduler *scheduler_;
  ObDagWarningHistoryManager *dag_history_mgr_;
  compaction::ObDiagnoseTabletMgr *diagnose_mgr_;
  TestDagModuleProvider provider_;
  share::ObIModuleProvider *old_mp_;
  DISALLOW_COPY_AND_ASSIGN(TestDagScheduler);
};

void wait_scheduler() {
  ObDagScheduler *scheduler = share::g_mp->dag_scheduler();
  ASSERT_TRUE(nullptr != scheduler);
  while (!scheduler->is_empty()) {
    usleep(100000);
  }
  ObIAllocator &basic_allocator =
      scheduler->get_allocator(false /*use_reserved_allocator*/);
  ObIAllocator *basic_root_allocator = nullptr;
  if (is_ob_malloc_backend()) {
    basic_root_allocator =
        &static_cast<ObParallelAllocator *>(&basic_allocator)->root_allocator_;
  }
  while ((basic_allocator.used()
          - (nullptr == basic_root_allocator ? 0 : basic_root_allocator->used())) != 0) {
    ::usleep(100000);
  }
  while ((basic_allocator.total()
          - (nullptr == basic_root_allocator ? 0 : basic_root_allocator->total())) != 0) {
    ::usleep(100000);
  }
}

class ObBasicDag : public ObIDag
{
public:
  ObBasicDag() :
    ObIDag(ObDagType::DAG_TYPE_MAJOR_MERGE),
    id_(ObTimeUtility::current_time() + random())
  {}
  void init(int64_t id) { id_ = id; }
  virtual uint64_t hash() const { return murmurhash(&id_, sizeof(id_), 0);}
  virtual bool operator == (const ObIDag &other) const
  {
    bool bret = false;
    if (get_type() == other.get_type()) {
      const ObBasicDag &dag = static_cast<const ObBasicDag &>(other);
      bret = dag.id_ == id_;
    }
    return bret;
  }
  virtual int fill_info_param(compaction::ObIBasicInfoParam *&out_param,
      ObIAllocator &allocator) const override
  {
    int ret = OB_SUCCESS;
    if (!is_inited_) {
      ret = OB_NOT_INIT;
    } else if (OB_FAIL(ADD_DAG_WARN_INFO_PARAM(out_param, allocator, get_type(), id_, id_+1, "is_test", true))) {
      COMMON_LOG(WARN, "fail to add dag warning info param", K(ret));
    }
    return ret;
  }
  virtual int fill_dag_key(char *buf,const int64_t size) const override { UNUSEDx(buf, size); return OB_SUCCESS; }

  INHERIT_TO_STRING_KV("ObIDag", ObIDag, K_(is_inited), K_(type), K_(id), K(task_list_.get_size()), K_(dag_ret));

private:
  int64_t id_;
  DISALLOW_COPY_AND_ASSIGN(ObBasicDag);
};

/*
 * check dag wait to schedule
 * */

class ObWaitTask : public ObITask
{
public:
  ObWaitTask() : ObITask(ObITaskType::TASK_TYPE_UT), cnt_(0), start_time_(0), finish_time_(0) {}
  virtual ~ObWaitTask() {}
  virtual int process()
  {
    int ret = OB_SUCCESS;
    if (cnt_ == 0) {
      start_time_ = ObTimeUtility::current_time();
    } else if (cnt_ < FINISH_CNT) {
      cnt_++;
      if (OB_FAIL(dag_yield())) {
        if (OB_CANCELED != ret) {
          COMMON_LOG(WARN, "Invalid return value for dag_yield", K(ret));
        }
      }
    } else {
      finish_time_ = ObTimeUtility::current_time();
      COMMON_LOG(INFO, "finish process", K(start_time_), K_(finish_time));
    }
    return ret;
  }
private:
  const static int64_t FINISH_CNT = 5;
  int cnt_;
  int64_t start_time_;
  int64_t finish_time_;
};

class ObWaitDag : public ObBasicDag
{
public:
  ObWaitDag() :
    ObBasicDag(),
    retry_times_(0),
    last_run_time_(0)
  {}
  virtual int create_first_task() override
  {
    int ret = OB_SUCCESS;
    ObWaitTask *task = NULL;
    if (OB_FAIL(alloc_task(task))) {
      COMMON_LOG(WARN, "Fail to alloc task", K(ret));
    } else if (OB_FAIL(add_task(*task))) {
      COMMON_LOG(WARN, "Fail to add task", K(ret));
    }
    return common::OB_SUCCESS;
  }
  bool inner_check_can_retry()
  {
    bool bret = true;
    if (retry_times_++ > MAX_RETRY_TIMES) {
      bret = false;
    }
    return bret;
  }

  virtual bool check_can_schedule() override
  {
    bool bret = true;
    if (ObTimeUtility::current_time() - last_run_time_ < MAX_CHECK_INTERVAL) {
      bret = false;
    } else {
      last_run_time_ = ObTimeUtility::current_time();
      STORAGE_LOG(INFO, "check_can_schedule", KPC(this));
    }
    return bret;
  }
  INHERIT_TO_STRING_KV("ObBasicDag", ObBasicDag, K_(retry_times), K_(last_run_time));
private:
  const int64_t MAX_RETRY_TIMES = 20;
  const int64_t MAX_CHECK_INTERVAL = 1000L * 100L; // 100ms

  int64_t retry_times_;
  int64_t last_run_time_;
  DISALLOW_COPY_AND_ASSIGN(ObWaitDag);
};


TEST_F(TestDagScheduler, test_task_wait_to_schedule)
{
  ObDagScheduler *scheduler = share::g_mp->dag_scheduler();
  ASSERT_TRUE(nullptr != scheduler);
  ObDagWarningHistoryManager* manager = share::g_mp->dag_warning_history_manager();
  ASSERT_TRUE(nullptr != manager);
  EXPECT_EQ(OB_SUCCESS, share::g_mp->dag_warning_history_manager()->init(true, "DagWarnHis"));

  for (int i = 0; i < 10; ++i) {
    EXPECT_EQ(OB_SUCCESS, scheduler->create_and_add_dag<ObWaitDag>(nullptr));
  }

  wait_scheduler();
  EXPECT_EQ(0, share::g_mp->dag_warning_history_manager()->size());
}

/*
 * check task retry
 * */
class ObRetryTask : public ObITask
{
public:
  ObRetryTask() : ObITask(ObITaskType::TASK_TYPE_NORMAL_MINOR_MERGE), cnt_(0), seq_(0) {}
  virtual ~ObRetryTask() {}
  virtual int process()
  {
    int ret = OB_SUCCESS;
    if (cnt_++ < FINISH_CNT) {
      ret = OB_ERROR;
    }
    return ret;
  }
  void init(int64_t seq) { seq_ = seq; }
  virtual int generate_next_task(ObITask *&next_task)
  {
    int ret = OB_SUCCESS;
    if (seq_ >= MAX_SEQ) {
      ret = OB_ITER_END;
      COMMON_LOG(INFO, "generate task end", K_(seq));
    } else {
      ObIDag *dag = get_dag();
      ObRetryTask *ntask = NULL;
      if (NULL == dag) {
        ret = OB_ERR_UNEXPECTED;
        COMMON_LOG(WARN, "dag is null", K(ret));
      } else if (OB_FAIL(dag->alloc_task(ntask))) {
        COMMON_LOG(WARN, "failed to alloc task", K(ret));
      } else if (NULL == ntask) {
        ret = OB_ERR_UNEXPECTED;
        COMMON_LOG(WARN, "task is null", K(ret));
      } else {
        ntask->init(seq_ + 1);
        next_task = ntask;
      }
    }
    return ret;
  }
private:
  const int64_t FINISH_CNT = 3;
  const int64_t MAX_SEQ = 3;
  int cnt_;
  int64_t seq_;
};

class ObDagRetryTask : public ObITask
{
public:
  ObDagRetryTask() : ObITask(ObITaskType::TASK_TYPE_NORMAL_MINOR_MERGE) {}
  virtual ~ObDagRetryTask() {}
  virtual int process()
  {
    static int cnt_ = 0;
    int ret = OB_SUCCESS;
    if (cnt_++ < FINISH_CNT) {
      ret = OB_ERROR;
    }
    return ret;
  }
private:
  const int64_t FINISH_CNT = 1;
};

struct ObRetryDagInitParam : public ObIDagInitParam
{
  ObRetryDagInitParam() : id_(0), str_() {}
  virtual ~ObRetryDagInitParam() {}
  virtual bool is_valid() const override
  {
    return id_ > 0 && !str_.empty();
  }

  int assign(const ObRetryDagInitParam &other)
  {
    int ret = OB_SUCCESS;
    id_ = other.id_;
    if (OB_FAIL(deep_copy_str(other.str_.ptr(), str_))) {
      STORAGE_LOG(WARN, "deep copy string", K(ret));
    }
    return ret;
  }

  int deep_copy_str(const char *src, ObString &dest)
  {
    int ret = OB_SUCCESS;
    char *buf = NULL;

    if (OB_ISNULL(src)) {
      ret = OB_INVALID_ARGUMENT;
      STORAGE_LOG(WARN, "The src is NULL, ", K(ret));
    } else {
      int64_t len = strlen(src) + 1;
      if (NULL == (buf = static_cast<char *>(allocator_.alloc(len)))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        STORAGE_LOG(ERROR, "Fail to allocate memory, ", K(len), K(ret));
      } else {
        MEMCPY(buf, src, len-1);
        buf[len-1] = '\0';
        dest.assign_ptr(buf, static_cast<ObString::obstr_size_t>(len-1));
      }
    }
    return ret;
  }
  int64_t id_;
  ObString str_;
  ObArenaAllocator allocator_;
};

class ObDagRetryDag : public ObBasicDag
{
public:
  ObDagRetryDag() : ObBasicDag() {}
  virtual int init_by_param(const ObIDagInitParam *param) override
  {
    int ret = OB_SUCCESS;
    if (OB_ISNULL(param) || !param->is_valid()) {
      ret = OB_INVALID_ARGUMENT;
      COMMON_LOG(WARN, "invalid argument", K(ret), K(param));
    } else if (OB_FAIL(param_.assign(*(static_cast<const ObRetryDagInitParam *>(param))))) {
      COMMON_LOG(WARN, "failed to assign param", K(ret));
    }
    return ret;
  }
  virtual int create_first_task() override
  {
    int ret = OB_SUCCESS;
    ObDagRetryTask *task = NULL;
    if (OB_FAIL(alloc_task(task))) {
      COMMON_LOG(WARN, "Fail to alloc task", K(ret));
    } else if (OB_FAIL(add_task(*task))) {
      COMMON_LOG(WARN, "Fail to add task", K(ret));
    }
    return common::OB_SUCCESS;
  }
  virtual int inner_reset_status_for_retry() override
  {
    int ret = OB_SUCCESS;
    if (OB_FAIL(init_by_param(&param_))) {
      COMMON_LOG(WARN, "failed to init param", K(ret));
    } else if (OB_FAIL(create_first_task())) {
      COMMON_LOG(WARN, "failed to create first task", K(ret));
    }
    return ret;
  }

private:
  ObRetryDagInitParam param_;
  DISALLOW_COPY_AND_ASSIGN(ObDagRetryDag);
};

TEST_F(TestDagScheduler, test_dag_retry)
{
  ObDagScheduler *scheduler = share::g_mp->dag_scheduler();
  ASSERT_TRUE(nullptr != scheduler);
  ObDagWarningHistoryManager* manager = share::g_mp->dag_warning_history_manager();
  ASSERT_TRUE(nullptr != manager);
  EXPECT_EQ(OB_SUCCESS, share::g_mp->dag_warning_history_manager()->init(true, "DagWarnHis"));

  int ret = OB_SUCCESS;
  for (int i = 0; OB_SUCC(ret) && i < 5; ++i) {
    ObDagRetryDag *dag = NULL;
    ObRetryDagInitParam param;
    const int64_t str_len = 100;
    char str[str_len];
    param.id_ = i + 1;
    snprintf(str, str_len, "Hello OceanBase_%d", i);
    param.str_ = ObString(str);
    if (OB_FAIL(scheduler->create_dag(&param, dag))) {
      COMMON_LOG(WARN, "failed to create dag", K(ret));
    } else if (FALSE_IT(dag->set_max_retry_times(3))) {
    } else if (OB_FAIL(scheduler->add_dag(dag))) {
      COMMON_LOG(WARN, "failed to add dag", K(ret));
    }
    EXPECT_EQ(OB_SUCCESS, ret);
  }

  wait_scheduler();
  EXPECT_EQ(0, share::g_mp->dag_warning_history_manager()->size());
}

class ObDagRetryFailedTask : public ObITask
{
public:
  ObDagRetryFailedTask() : ObITask(ObITaskType::TASK_TYPE_NORMAL_MINOR_MERGE) {}
  virtual ~ObDagRetryFailedTask() {}
  virtual int process()
  {
    int ret = OB_ERROR;
    return ret;
  }
};

class ObRetryFailedDag : public ObDagRetryDag
{
public:
  ObRetryFailedDag() : ObDagRetryDag() {}
  virtual ~ObRetryFailedDag() {}
  virtual int create_first_task() override
  {
    int ret = OB_SUCCESS;
    ObDagRetryFailedTask *task = nullptr;
    if (OB_FAIL(alloc_task(task))) {
      COMMON_LOG(WARN, "Fail to alloc task", K(ret));
    } else if (OB_FAIL(add_task(*task))) {
      COMMON_LOG(WARN, "Fail to add task", K(ret));
    } else if (running_times_ >= 1) {
      ret = OB_ERR_UNEXPECTED;
      COMMON_LOG(WARN, "create first task failed when dag retry", K_(running_times), KPC(this));
    }
    return ret;
  }
};

TEST_F(TestDagScheduler, test_dag_retry_failed)
{
  ObDagScheduler *scheduler = share::g_mp->dag_scheduler();
  ASSERT_TRUE(nullptr != scheduler);
  ObDagWarningHistoryManager* manager = share::g_mp->dag_warning_history_manager();
  ASSERT_TRUE(nullptr != manager);
  EXPECT_EQ(OB_SUCCESS, share::g_mp->dag_warning_history_manager()->init(true, "DagWarnHis"));

  int ret = OB_SUCCESS;
  for (int i = 0; OB_SUCC(ret) && i < 5; ++i) {
    ObRetryFailedDag *dag = NULL;
    ObRetryDagInitParam param;
    const int64_t str_len = 100;
    char str[str_len];
    param.id_ = i + 1;
    snprintf(str, str_len, "Hello OceanBase_%d", i);
    param.str_ = ObString(str);
    if (OB_FAIL(scheduler->create_dag(&param, dag))) {
      COMMON_LOG(WARN, "failed to create dag", K(ret));
    } else if (FALSE_IT(dag->set_max_retry_times(3))) {
    } else if (OB_FAIL(scheduler->add_dag(dag))) {
      COMMON_LOG(WARN, "failed to add dag", K(ret));
    }
    EXPECT_EQ(OB_SUCCESS, ret);
  }

  wait_scheduler();
  EXPECT_EQ(5, share::g_mp->dag_warning_history_manager()->size());
}

/*
 * check task retry
 * */
static int64_t generate_cnt = 1;
class ObGenerateFailedTask : public ObITask
{
public:
  ObGenerateFailedTask() : ObITask(ObITaskType::TASK_TYPE_NORMAL_MINOR_MERGE), cnt_(0), seq_(0) {}
  virtual ~ObGenerateFailedTask() {}
  virtual int process()
  {
    return OB_SUCCESS;
  }
  void init(int64_t seq) { seq_ = seq; }
  virtual int generate_next_task(ObITask *&next_task)
  {
    int ret = OB_SUCCESS;
    if (seq_ >= MAX_SEQ) {
      ret = OB_ITER_END;
      COMMON_LOG(INFO, "generate task end", K_(seq));
    } else if (generate_cnt++ < 2) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      ObIDag *dag = get_dag();
      ObDagRetryTask *ntask = NULL;
      if (NULL == dag) {
        ret = OB_ERR_UNEXPECTED;
        COMMON_LOG(WARN, "dag is null", K(ret));
      } else if (OB_FAIL(dag->alloc_task(ntask))) {
        COMMON_LOG(WARN, "failed to alloc task", K(ret));
      } else if (NULL == ntask) {
        ret = OB_ERR_UNEXPECTED;
        COMMON_LOG(WARN, "task is null", K(ret));
      } else {
        next_task = ntask;
      }
    }
    return ret;
  }
private:
  const int64_t MAX_SEQ = 2;
  int cnt_;
  int64_t seq_;
};

class ObGenerateFailedDag : public ObBasicDag
{
public:
  virtual int create_first_task() override
  {
    int ret = OB_SUCCESS;
    ObGenerateFailedTask *task = NULL;
    if (OB_FAIL(alloc_task(task))) {
      COMMON_LOG(WARN, "Fail to alloc task", K(ret));
    } else if (OB_FAIL(add_task(*task))) {
      COMMON_LOG(WARN, "Fail to add task", K(ret));
    } else {
      task->init(0);
    }
    return common::OB_SUCCESS;
  }
  virtual int fill_info_param(compaction::ObIBasicInfoParam *&out_param,
      ObIAllocator &allocator) const override
  {
    int ret = OB_SUCCESS;
    if (!is_inited_) {
      ret = OB_NOT_INIT;
    } else if (OB_FAIL(ADD_DAG_WARN_INFO_PARAM(out_param, allocator, get_type(), id_, id_+1))) {
      COMMON_LOG(WARN, "fail to add dag warning info param", K(ret));
    }
    return ret;
  }
  int inner_reset_status_for_retry() { return OB_SUCCESS; }
  INHERIT_TO_STRING_KV("ObIDag", ObIDag, K_(is_inited), K_(type), K_(id), K(task_list_.get_size()), K_(dag_ret));
};

TEST_F(TestDagScheduler, test_generage_task_failed)
{
  ObDagScheduler *scheduler = share::g_mp->dag_scheduler();
  ASSERT_TRUE(nullptr != scheduler);
  ObDagWarningHistoryManager* manager = share::g_mp->dag_warning_history_manager();
  ASSERT_TRUE(nullptr != manager);
  ASSERT_EQ(OB_SUCCESS, share::g_mp->dag_warning_history_manager()->init(true, "DagWarnHis"));

  int ret = OB_SUCCESS;
  ObGenerateFailedDag *dag = nullptr;
  for (int i = 0; i < 1; ++i) {
    if (OB_FAIL(scheduler->create_dag(nullptr, dag))) {
      COMMON_LOG(WARN, "failed to create dag", K(ret));
    } else if (FALSE_IT(dag->set_max_retry_times(7))) {
    } else if (OB_FAIL(scheduler->add_dag(dag))) {
      COMMON_LOG(WARN, "failed to add dag", K(ret));
    }
    EXPECT_EQ(OB_SUCCESS, ret);
  }

  wait_scheduler();
  ASSERT_EQ(1, share::g_mp->dag_warning_history_manager()->size());
}

//generate next dag

class ObGenerateNextDagCtx
{
public:
  ObGenerateNextDagCtx()
    : index_(0)
  {
  }

  virtual ~ObGenerateNextDagCtx() {}

  int get_next_index(int64_t &index)
  {
    int ret = OB_SUCCESS;
    index = 0;
    common::SpinWLockGuard guard(lock_);
    if (index_ >= MAX_INDEX) {
      ret = OB_ITER_END;
    } else {
      index = index_;
      index_++;
    }
    return ret;
  }

  bool is_empty() const
  {
    common::SpinRLockGuard guard(lock_);
    return index_ == MAX_INDEX;
  }

private:
  const int64_t MAX_INDEX = 10;
  common::SpinRWLock lock_;
  int64_t index_;
};

class ObFinishGeneratNextDag : public ObBasicDag
{
public:
  ObFinishGeneratNextDag() :
    ObBasicDag(),
    is_inited_(false),
    ctx_()
  {}

  int init()
  {
    int ret = OB_SUCCESS;
    if (is_inited_) {
      ret = OB_INIT_TWICE;
      COMMON_LOG(WARN, "start generate next dag init twice", K(ret));
    } else {
      id_ = FINISH_DAG_ID;
      is_inited_ = true;
    }
    return ret;
  }

  virtual int create_first_task() override
  {
    int ret = OB_SUCCESS;
    ObFakeTask *task = NULL;
    if (OB_FAIL(alloc_task(task))) {
      COMMON_LOG(WARN, "Fail to alloc task", K(ret));
    } else if (OB_FAIL(add_task(*task))) {
      COMMON_LOG(WARN, "Fail to add task", K(ret));
    }
    return common::OB_SUCCESS;
  }
  ObGenerateNextDagCtx *get_ctx() { return &ctx_; }

private:
  const int64_t FINISH_DAG_ID = 1000001;
  bool is_inited_;
  ObGenerateNextDagCtx ctx_;
  DISALLOW_COPY_AND_ASSIGN(ObFinishGeneratNextDag);
};

class ObDagGenerateNextDag : public ObBasicDag
{
public:
  ObDagGenerateNextDag() :
    ObBasicDag(),
    is_inited_(false),
    ctx_(nullptr)
  {}

  int init(const int64_t id, ObGenerateNextDagCtx *ctx)
  {
    int ret = OB_SUCCESS;
    if (is_inited_) {
      ret = OB_INIT_TWICE;
      COMMON_LOG(WARN, "dag generate next dag init twice", K(ret));
    } else if (id < 0) {
      ret = OB_INVALID_ARGUMENT;
      COMMON_LOG(WARN, "init dag generate next dag get invalid argument", K(ret), K(id));
    } else {
      id_ = id;
      ctx_ = ctx;
      is_inited_ = true;
      COMMON_LOG(INFO, "succeed init next dag", K(id));
    }
    return ret;
  }

  virtual int generate_next_dag(share::ObIDag *&dag)
  {
    int ret = OB_SUCCESS;
    dag = nullptr;
    ObDagScheduler *scheduler = nullptr;
    int64_t next_id = 0;
    ObDagGenerateNextDag *next_dag = nullptr;

    if (!is_inited_) {
      ret = OB_NOT_INIT;
      COMMON_LOG(WARN, "generate next dag do not init", K(ret));
    } else if (OB_FAIL(ctx_->get_next_index(next_id))) {
      if (OB_ITER_END == ret) {
        //do nothing
      } else {
        COMMON_LOG(WARN, "failed to get next index", K(ret));
      }
    } else if (OB_ISNULL(scheduler = share::g_mp->dag_scheduler())) {
      ret = OB_ERR_UNEXPECTED;
      COMMON_LOG(WARN, "failed to get ObDagScheduler from module provider", K(ret));
    } else if (OB_FAIL(scheduler->alloc_dag(next_dag))) {
      COMMON_LOG(WARN, "failed to alloc next_dag", K(ret));
    } else if (OB_FAIL(next_dag->init(next_id, ctx_))) {
      COMMON_LOG(WARN, "failed to init tablet migration dag", K(ret));
    } else if (OB_FAIL(next_dag->create_first_task())) {
      COMMON_LOG(WARN, "failed to create first task", K(ret));
    } else {
      dag = next_dag;
      next_dag = nullptr;
    }

    if (OB_NOT_NULL(next_dag)) {
      scheduler->free_dag(*next_dag);
    }
    return ret;
  }

  virtual int create_first_task() override
  {
    int ret = OB_SUCCESS;
    ObFakeTask *task = NULL;
    if (OB_FAIL(alloc_task(task))) {
      COMMON_LOG(WARN, "Fail to alloc task", K(ret));
    } else if (OB_FAIL(add_task(*task))) {
      COMMON_LOG(WARN, "Fail to add task", K(ret));
    }
    return common::OB_SUCCESS;
  }

  INHERIT_TO_STRING_KV("ObIDag", ObIDag, K_(is_inited), K_(type), K_(id), K(task_list_.get_size()), K_(dag_ret));
private:
  bool is_inited_;
  ObGenerateNextDagCtx *ctx_;
  DISALLOW_COPY_AND_ASSIGN(ObDagGenerateNextDag);
};

class ObStartGeneratNextDagTask : public ObITask
{
public:
  ObStartGeneratNextDagTask() : ObITask(ObITaskType::TASK_TYPE_NORMAL_MINOR_MERGE), is_inited_(false), id_(0) {}
  virtual ~ObStartGeneratNextDagTask() {}
  virtual int process()
  {
    int ret = OB_SUCCESS;
    if (!is_inited_) {
      ret = OB_NOT_INIT;
      COMMON_LOG(WARN, "generate next dag task do not init", K(ret));
    } else {
      ObDagGenerateNextDag *next_dag = nullptr;
      ObFinishGeneratNextDag *finish_dag = nullptr;
      ObGenerateNextDagCtx *ctx = nullptr;
      ObDagScheduler *scheduler = nullptr;
      int64_t id = 0;

      if (!is_inited_) {
        ret = OB_NOT_INIT;
        COMMON_LOG(WARN, "start prepare migration task do not init", K(ret));
      } else if (OB_ISNULL(scheduler = share::g_mp->dag_scheduler())) {
        ret = OB_ERR_UNEXPECTED;
        COMMON_LOG(WARN, "failed to get ObDagScheduler from module provider", K(ret));
      } else if (OB_FAIL(scheduler->alloc_dag(finish_dag))) {
        COMMON_LOG(WARN, "failed to alloc finish backfill tx migration dag ", K(ret));
      } else if (OB_FAIL(finish_dag->init())) {
        COMMON_LOG(WARN, "failed to init data tablets migration dag", K(ret));
      } else if (OB_ISNULL(ctx = finish_dag->get_ctx())) {
        ret = OB_ERR_UNEXPECTED;
        COMMON_LOG(WARN, "backfill tx ctx should not be NULL", K(ret), KP(ctx));
      } else if (ctx->is_empty()) {
        if (OB_FAIL(this->get_dag()->add_child(*finish_dag))) {
          COMMON_LOG(WARN, "failed to add finish_dag as chilid", K(ret));
        }
      } else {
        if (OB_FAIL(ctx->get_next_index(id))) {
          COMMON_LOG(WARN, "failed to get tablet id", K(ret));
        } else if (OB_FAIL(scheduler->alloc_dag(next_dag))) {
          COMMON_LOG(WARN, "failed to alloc next_dag", K(ret));
        } else if (OB_FAIL(next_dag->init(id, ctx))) {
          COMMON_LOG(WARN, "failed to init next_dag", K(ret));
        } else if (OB_FAIL(this->get_dag()->add_child(*next_dag))) {
          COMMON_LOG(WARN, "failed to add next_dag as chilid", K(ret));
        } else if (OB_FAIL(next_dag->create_first_task())) {
          COMMON_LOG(WARN, "failed to create first task", K(ret));
        } else if (OB_FAIL(next_dag->add_child(*finish_dag))) {
          COMMON_LOG(WARN, "failed to add child dag", K(ret));
        } else if (OB_FAIL(scheduler->add_dag(next_dag))) {
          COMMON_LOG(WARN, "failed to add tablet backfill tx dag", K(ret));
          if (OB_SIZE_OVERFLOW != ret && OB_EAGAIN != ret) {
            COMMON_LOG(WARN, "Fail to add task", K(ret));
            ret = OB_EAGAIN;
          }
        }
      }

      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(finish_dag->create_first_task())) {
        COMMON_LOG(WARN, "failed to create first task", K(ret));
      } else if (OB_FAIL(scheduler->add_dag(finish_dag))) {
        COMMON_LOG(WARN, "failed to add finish_dag", K(ret));
        int tmp_ret = OB_SUCCESS;
        if (OB_SIZE_OVERFLOW != ret && OB_EAGAIN != ret) {
          COMMON_LOG(WARN, "Fail to add task", K(ret));
          ret = OB_EAGAIN;
        }

        if (OB_NOT_NULL(next_dag)) {
          if (OB_SUCCESS != (tmp_ret = scheduler->cancel_dag(next_dag))) {
            COMMON_LOG(WARN, "failed to cancel next_dag", K(ret));
          }
          next_dag = nullptr;
        }
      } else {
        next_dag = nullptr;
        finish_dag = nullptr;
      }

      if (OB_FAIL(ret)) {
        if (OB_NOT_NULL(next_dag)) {
          scheduler->free_dag(*next_dag);
        }

        if (OB_NOT_NULL(finish_dag)) {
          scheduler->free_dag(*finish_dag);
        }
      }
    }

    return ret;
  }
  int init(const int64_t id)
  {
    int ret = OB_SUCCESS;
    if (is_inited_) {
      ret = OB_INIT_TWICE;
      COMMON_LOG(WARN, "generate next dag task init twice", K(ret));
    } else if (id < 0) {
      ret = OB_INVALID_ARGUMENT;
      COMMON_LOG(WARN, "init generate next dag task get invalid argument", K(ret), K(id));
    } else {
      id_ = id;
      is_inited_ = true;
    }
    return ret;
  }
private:
  bool is_inited_;
  int64_t id_;
};

class ObStartGenerateNextDag : public ObBasicDag
{
public:
  ObStartGenerateNextDag() :
    ObBasicDag(),
    is_inited_(false)
  {}

  int init()
  {
    int ret = OB_SUCCESS;
    if (is_inited_) {
      ret = OB_INIT_TWICE;
      COMMON_LOG(WARN, "start generate next dag init twice", K(ret));
    } else {
      id_ = START_DAG_ID;
      is_inited_ = true;
    }
    return ret;
  }

  virtual int create_first_task() override
  {
    int ret = OB_SUCCESS;
    ObStartGeneratNextDagTask *task = NULL;
    if (OB_FAIL(alloc_task(task))) {
      COMMON_LOG(WARN, "Fail to alloc task", K(ret));
    } else if (OB_FAIL(task->init(id_))) {
      COMMON_LOG(WARN, "failed to init task", K(ret));
    } else if (OB_FAIL(add_task(*task))) {
      COMMON_LOG(WARN, "Fail to add task", K(ret));
    }
    return common::OB_SUCCESS;
  }

private:
  const int64_t START_DAG_ID = 1000000;
  bool is_inited_;
  DISALLOW_COPY_AND_ASSIGN(ObStartGenerateNextDag);
};

TEST_F(TestDagScheduler, generate_next_dag)
{
  ObDagScheduler *scheduler = share::g_mp->dag_scheduler();
  ASSERT_TRUE(nullptr != scheduler);
  ObDagWarningHistoryManager* manager = share::g_mp->dag_warning_history_manager();
  ASSERT_TRUE(nullptr != manager);
  EXPECT_EQ(OB_SUCCESS, share::g_mp->dag_warning_history_manager()->init(true, "DagWarnHis"));

  ObStartGenerateNextDag *dag = nullptr;
  EXPECT_EQ(OB_SUCCESS, scheduler->alloc_dag(dag));
  EXPECT_EQ(OB_SUCCESS, dag->init());
  EXPECT_EQ(OB_SUCCESS, dag->create_first_task());
  EXPECT_EQ(OB_SUCCESS, scheduler->add_dag(dag));

  wait_scheduler();
  EXPECT_EQ(0, share::g_mp->dag_warning_history_manager()->size());
}


}
}

void parse_cmd_arg(int argc, char **argv)
{
  int opt = 0;
  const char *opt_string = "p:s:l:";

  struct option longopts[] = {
      {"dag cnt for performance test", 1, NULL, 'p'},
      {"stress test time", 1, NULL, 's'},
      {"log level", 1, NULL, 'l'},
      {0,0,0,0} };

  while (-1 != (opt = getopt_long(argc, argv, opt_string, longopts, NULL))) {
    switch(opt) {
    case 'p':
      dag_cnt = strtoll(optarg, NULL, 10);
      break;
    case 's':
      stress_time = strtoll(optarg, NULL, 10);
      break;
    case 'l':
      snprintf(log_level, 20, "%s", optarg);
      break;
    default:
      break;
    }
  }
}

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  parse_cmd_arg(argc, argv);
  OB_LOGGER.set_enable_async_log(false);
  OB_LOGGER.set_log_level("DEBUG");
  OB_LOGGER.set_max_file_size(256*1024*1024);
  system("rm -f test_dag_scheduler.log*");
  OB_LOGGER.set_file_name("test_dag_scheduler.log");
  return RUN_ALL_TESTS();
}
