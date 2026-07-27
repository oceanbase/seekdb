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

#include "ob_deadlock_detector_mgr.h"
#include "share/rc/ob_module_provider.h"
#include "storage/deadlock/ob_lcl_scheme/ob_lcl_node.h"
#include "ob_deadlock_inner_table_service.h"

namespace oceanbase
{
namespace share
{
namespace detector
{

using namespace common;
uint64_t ObDeadLockDetectorMgr::InnerAllocHandle::InnerFactory::create_count_ = 0;
uint64_t ObDeadLockDetectorMgr::InnerAllocHandle::InnerFactory::release_count_ = 0;
const char * MEMORY_LABEL = "DeadLock";
constexpr int64_t DEADLOCK_LOCAL_TASK_QUEUE_LIMIT = 64 * 1024;

ObDeadLockLocalTaskQueue::Task::Task(const TaskType type)
  : type_(type),
    lclv_(INVALID_VALUE),
    send_ts_(INVALID_VALUE)
{
}

int ObDeadLockLocalTaskQueue::Task::set_lcl_state(const UserBinaryKey &dest_key,
                                                   const int64_t lclv,
                                                   const ObLCLLabel &label,
                                                   const int64_t send_ts)
{
  int ret = OB_SUCCESS;
  if (!dest_key.is_valid() || INVALID_VALUE == lclv ||
      !label.is_valid() || INVALID_VALUE == send_ts) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    dest_key_ = dest_key;
    lclv_ = lclv;
    label_ = label;
    send_ts_ = send_ts;
  }
  return ret;
}

int ObDeadLockLocalTaskQueue::Task::set_cycle_info(const ObDeadLockCycleInfo &cycle_info)
{
  int ret = OB_SUCCESS;
  if (!cycle_info.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(cycle_info_.assign(cycle_info))) {
    DETECT_LOG(WARN, "assign deadlock cycle info failed", KR(ret));
  }
  return ret;
}

int ObDeadLockLocalTaskQueue::Task::set_parent_notification(const UserBinaryKey &parent_key,
                                                            const UserBinaryKey &child_key)
{
  int ret = OB_SUCCESS;
  if (!parent_key.is_valid() || !child_key.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    dest_key_ = parent_key;
    child_key_ = child_key;
  }
  return ret;
}

ObDeadLockLocalTaskQueue::ObDeadLockLocalTaskQueue()
  : is_inited_(false),
    is_running_(false),
    mgr_(nullptr)
{
}

ObDeadLockLocalTaskQueue::~ObDeadLockLocalTaskQueue()
{
  destroy();
}

int ObDeadLockLocalTaskQueue::init(ObDeadLockDetectorMgr *mgr)
{
  int ret = OB_SUCCESS;
  bool thread_pool_inited = false;
  if (is_inited_) {
    ret = OB_INIT_TWICE;
  } else if (OB_ISNULL(mgr)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(common::ObLinkQueueThreadPool::init(
                 1, DEADLOCK_LOCAL_TASK_QUEUE_LIMIT, "DeadLockLocal"))) {
    DETECT_LOG(WARN, "init local deadlock task queue failed", KR(ret));
  } else if (FALSE_IT(thread_pool_inited = true)) {
  } else if (OB_FAIL(common::ObLinkQueueThreadPool::set_adaptive_thread(1, 1))) {
    DETECT_LOG(WARN, "fix local deadlock task queue worker count failed", KR(ret));
  } else {
    common::ObLinkQueueThreadPool::set_run_wrapper(share::server_runtime());
    mgr_ = mgr;
    is_inited_ = true;
  }
  if (OB_FAIL(ret) && thread_pool_inited) {
    common::ObLinkQueueThreadPool::destroy();
  }
  return ret;
}

int ObDeadLockLocalTaskQueue::start()
{
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
  } else if (common::ObLinkQueueThreadPool::get_thread_count() <= 0 &&
             !common::ObLinkQueueThreadPool::try_expand_one(1)) {
    ret = OB_ERR_UNEXPECTED;
    DETECT_LOG(WARN, "start local deadlock task queue failed", KR(ret));
  } else {
    ATOMIC_STORE(&is_running_, true);
  }
  return ret;
}

void ObDeadLockLocalTaskQueue::stop()
{
  if (is_inited_) {
    ATOMIC_STORE(&is_running_, false);
    common::ObLinkQueueThreadPool::stop();
  }
}

void ObDeadLockLocalTaskQueue::wait()
{
  if (is_inited_) {
    common::ObLinkQueueThreadPool::wait();
  }
}

void ObDeadLockLocalTaskQueue::destroy()
{
  if (is_inited_) {
    common::ObLinkQueueThreadPool::stop();
    common::ObLinkQueueThreadPool::wait();
    common::ObLinkQueueThreadPool::destroy();
    ATOMIC_STORE(&is_running_, false);
    mgr_ = nullptr;
    is_inited_ = false;
  }
}

int ObDeadLockLocalTaskQueue::push_task_(Task *task)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(task)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (!is_inited_) {
    ret = OB_NOT_INIT;
  } else if (!ATOMIC_LOAD(&is_running_)) {
    ret = OB_NOT_RUNNING;
  } else if (OB_FAIL(common::ObLinkQueueThreadPool::push(task))) {
    DETECT_LOG(WARN, "push local deadlock task failed", KR(ret), K(task->type_));
  }
  if (OB_FAIL(ret) && OB_NOT_NULL(task)) {
    destroy_task_(task);
  }
  return ret;
}

int ObDeadLockLocalTaskQueue::push_lcl_state(const UserBinaryKey &dest_key,
                                             const int64_t lclv,
                                             const ObLCLLabel &label,
                                             const int64_t send_ts)
{
  int ret = OB_SUCCESS;
  Task *task = OB_NEW(Task, MEMORY_LABEL, TaskType::LCL_STATE);
  if (OB_ISNULL(task)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (OB_FAIL(task->set_lcl_state(dest_key, lclv, label, send_ts))) {
    destroy_task_(task);
  } else {
    ret = push_task_(task);
  }
  return ret;
}

int ObDeadLockLocalTaskQueue::push_cycle_info(const ObDeadLockCycleInfo &cycle_info)
{
  int ret = OB_SUCCESS;
  Task *task = OB_NEW(Task, MEMORY_LABEL, TaskType::CYCLE_INFO);
  if (OB_ISNULL(task)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (OB_FAIL(task->set_cycle_info(cycle_info))) {
    destroy_task_(task);
  } else {
    ret = push_task_(task);
  }
  return ret;
}

int ObDeadLockLocalTaskQueue::push_parent_notification(const UserBinaryKey &parent_key,
                                                       const UserBinaryKey &child_key)
{
  int ret = OB_SUCCESS;
  Task *task = OB_NEW(Task, MEMORY_LABEL, TaskType::PARENT_NOTIFICATION);
  if (OB_ISNULL(task)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (OB_FAIL(task->set_parent_notification(parent_key, child_key))) {
    destroy_task_(task);
  } else {
    ret = push_task_(task);
  }
  return ret;
}

void ObDeadLockLocalTaskQueue::handle(common::LinkTask *task)
{
  int ret = OB_SUCCESS;
  Task *local_task = static_cast<Task *>(task);
  if (OB_ISNULL(local_task) || OB_ISNULL(mgr_)) {
    ret = OB_ERR_UNEXPECTED;
    DETECT_LOG(WARN, "invalid local deadlock task", KR(ret), KP(local_task), KP_(mgr));
  } else if (mgr_->is_stopping_()) {
    // Drop queued propagation during shutdown. The copied task is released below.
  } else if (TaskType::LCL_STATE == local_task->type_) {
    if (OB_FAIL(mgr_->process_lcl_state_(local_task->dest_key_,
                                         local_task->lclv_,
                                         local_task->label_,
                                         local_task->send_ts_))) {
      DETECT_LOG(WARN, "process local lcl state failed", KR(ret), KPC(local_task));
    }
  } else if (TaskType::CYCLE_INFO == local_task->type_) {
    if (OB_FAIL(mgr_->process_cycle_info_(local_task->cycle_info_))) {
      DETECT_LOG(WARN, "process local deadlock cycle info failed", KR(ret), KPC(local_task));
    }
  } else if (TaskType::PARENT_NOTIFICATION == local_task->type_) {
    if (OB_FAIL(mgr_->process_parent_notification_(local_task->dest_key_,
                                                   local_task->child_key_))) {
      DETECT_LOG(WARN, "process local deadlock parent notification failed",
                 KR(ret), KPC(local_task));
    }
  } else {
    ret = OB_ERR_UNEXPECTED;
    DETECT_LOG(WARN, "unknown local deadlock task type", KR(ret), KPC(local_task));
  }
  if (OB_NOT_NULL(local_task)) {
    destroy_task_(local_task);
  }
}

void ObDeadLockLocalTaskQueue::handle_drop(common::LinkTask *task)
{
  destroy_task_(static_cast<Task *>(task));
}

void ObDeadLockLocalTaskQueue::destroy_task_(Task *task)
{
  if (OB_NOT_NULL(task)) {
    OB_DELETE(Task, MEMORY_LABEL, task);
  }
}

// definition and initializaion of class static member

ObDeadLockDetectorMgr::ObDeadLockDetectorMgr()
: is_inited_(false),
stop_ts_(0) {}

/* * * * * * definition of ObDeadLockDetectorMgr::InnerAllocHandle * * * * */


void ObDeadLockDetectorMgr::InnerAllocHandle::free_value(ObIDeadLockDetector *p)
{
  inner_factory_.release(p);
}

LinkHashNode<UserBinaryKey>* ObDeadLockDetectorMgr::
  InnerAllocHandle::alloc_node(ObIDeadLockDetector *p)
{
  UNUSED(p);
  return OB_NEW(LinkHashNode<UserBinaryKey>, MEMORY_LABEL);
}

void ObDeadLockDetectorMgr::InnerAllocHandle::free_node(LinkHashNode<UserBinaryKey> *node)
{
  if (node != nullptr) {
    //ob_free(node);
    OB_DELETE(LinkHashNode<UserBinaryKey>, MEMORY_LABEL, node);
  }
}

/* * * * * * define for ObDeadLockDetectorMgr::InnerFactory * * * * */

// Create a new detector instance
int ObDeadLockDetectorMgr::InnerAllocHandle::InnerFactory::create(const UserBinaryKey &key,
                                                                  const DetectCallBack &on_detect_operation,
                                                                  const CollectCallBack &on_collect_operation,
                                                                  const ObDetectorPriority &priority,
                                                                  const uint64_t start_delay,
                                                                  const uint32_t count_down_allow_detect,
                                                                  const bool auto_activate_when_detected,
                                                                  ObIDeadLockDetector *&p_detector)
{
  int ret = OB_SUCCESS;

  ObMemAttr attr(MEMORY_LABEL);
  int64_t alived_count = ATOMIC_LOAD(&create_count_) - ATOMIC_LOAD(&release_count_);
  if (alived_count > 50 * 1000) {// limit in 5w active nodes
    ret = OB_ERR_UNEXPECTED;
    DETECT_LOG(WARN, "too many detector", K(alived_count), KR(ret));
  } else if (nullptr ==
     (p_detector =
     (ObIDeadLockDetector *)ob_malloc(sizeof(ObLCLNode), attr))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    DETECT_LOG(WARN, "DetectorFactory alloc new detector failed", KR(ret));
  } else {
    p_detector = new (p_detector) ObLCLNode(key,
                                            ATOMIC_AAF(&logic_id_, 1),
                                            on_detect_operation,
                                            on_collect_operation,
                                            priority,
                                            start_delay,
                                            count_down_allow_detect,
                                            auto_activate_when_detected);
    if (false == static_cast<ObLCLNode*>(p_detector)->
                 is_successfully_constructed()) {
      ret = OB_INIT_FAIL;
      DETECT_LOG(WARN, "construct ObLCLNode obj failed", KR(ret));
      server_free(p_detector);
    } else {
      ATOMIC_INC(&create_count_);
    }
  }

  return ret;
}

// destroy a created detector instance, free its memory
void ObDeadLockDetectorMgr::InnerAllocHandle::InnerFactory::release(ObIDeadLockDetector *p_detector)
{
  if (nullptr == p_detector) {
    DETECT_LOG_RET(WARN, common::OB_INVALID_ARGUMENT, "p_detector is nullptr", KP(p_detector));
  } else {
    p_detector->~ObIDeadLockDetector();
    ob_free(p_detector);
    ATOMIC_INC(&release_count_);
  }
}

/* * * * * * definition of ObDeadLockDetectorMgr::DetectorRefGuard * * * * */

// guard should only used on stack, auto-revert pointer when guard destructed
ObDeadLockDetectorMgr::DetectorRefGuard::~DetectorRefGuard()
{
  ObDeadLockDetectorMgr *p_deadlock_detector_mgr = share::g_mp->dead_lock_detector_mgr();
  if (OB_ISNULL(p_deadlock_detector_mgr)) {
    DETECT_LOG_RET(ERROR, OB_ERR_UNEXPECTED, "can not get ObDeadLockDetectorMgr", KP(p_deadlock_detector_mgr));
  } else {
    p_deadlock_detector_mgr->detector_map_.revert(p_detector_);
  }
}

int ObDeadLockDetectorMgr::DetectorRefGuard::set_detector(ObIDeadLockDetector* p_detector)
{
  CHECK_ARGS(p_detector);
  p_detector_ = p_detector;
  return OB_SUCCESS;
}

/* * * * * * define for ObDeadLockDetectorMgr * * * * */

int ObDeadLockDetectorMgr::server_module_init(ObDeadLockDetectorMgr *&p_deadlock_detector_mgr)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(p_deadlock_detector_mgr->init())) {
    DETECT_LOG(ERROR, "init failure detector failed", KR(ret));
  }
  return ret;
}

int ObDeadLockDetectorMgr::init()
{
  #define PRINT_WRAPPER KR(ret)
  int ret = OB_SUCCESS;
  bool time_wheel_inited = false;
  bool detector_map_inited = false;
  
  if (is_inited_) {
    ret = OB_INIT_TWICE;
    DETECT_LOG(WARN, "deadlock detector manager init twice", PRINT_WRAPPER);
  } else if (OB_FAIL(ObDeadLockInnerTableService::init())) {
    DETECT_LOG(WARN, "failed to init deadlock inner table service", K(ret));
  } else {
    ObMemAttr attr(MEMORY_LABEL);
    if (OB_FAIL(time_wheel_.init(TIME_WHEEL_PRECISION_US,
                                 TIMER_THREAD_COUNT,
                                 DETECTOR_TIMER_NAME))) {
      DETECT_LOG(WARN, "time_wheel_ init failed", PRINT_WRAPPER);
    } else if (FALSE_IT(time_wheel_inited = true)) {
    } else if (OB_FAIL(detector_map_.init(attr))) {
      DETECT_LOG(WARN, "detector_map_ init failed", PRINT_WRAPPER);
    } else if (FALSE_IT(detector_map_inited = true)) {
    } else if (OB_FAIL(local_task_queue_.init(this))) {
      DETECT_LOG(WARN, "local deadlock task queue init failed", PRINT_WRAPPER);
    } else {
      ATOMIC_STORE(&stop_ts_, 0);
      is_inited_ = true;
      DETECT_LOG(INFO, "ObDeadLockDetectorMgr init success", PRINT_WRAPPER);
    }
    DETECT_LOG(INFO, "ObDeadLockDetectorMgr init called", PRINT_WRAPPER, K(lbt()));
  }

  if (OB_FAIL(ret)) {
    local_task_queue_.destroy();
    if (detector_map_inited) {
      detector_map_.destroy();
    }
    if (time_wheel_inited) {
      time_wheel_.destroy();
    }
  }

  return ret;
  #undef PRINT_WRAPPER
}

int ObDeadLockDetectorMgr::start()
{
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    DETECT_LOG(WARN, "deadlock detector manager is not initialized", KR(ret));
  } else if (is_stopping_()) {
    ret = OB_IN_STOP_STATE;
  } else if (OB_FAIL(local_task_queue_.start())) {
    DETECT_LOG(WARN, "local deadlock task queue start failed", KR(ret));
  } else if (OB_FAIL(time_wheel_.start())) {
    DETECT_LOG(WARN, "time wheel start failed");
    local_task_queue_.stop();
    local_task_queue_.wait();
  }
  return ret;
}

bool ObDeadLockDetectorMgr::ActivateFn::operator()(const UserBinaryKey &key,
                                                   ObIDeadLockDetector *p_detector)
{
  UNUSED(key);
  p_detector->unregister_timer_task();
  return true;
}

void ObDeadLockDetectorMgr::stop()
{
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    DETECT_LOG(WARN, "deadlock detector manager is not initialized");
  } else if (!is_stopping_()) {
    const int64_t stop_ts = ObClockGenerator::getRealClock();
    ATOMIC_STORE(&stop_ts_, stop_ts > 0 ? stop_ts : 1);
    ActivateFn fn;
    detector_map_.for_each(fn);
    if (time_wheel_.is_running() && OB_FAIL(time_wheel_.stop())) {
      DETECT_LOG(WARN, "ObDeadLockDetectorMgr stop time wheel failed", KR(ret));
    }
    local_task_queue_.stop();
  }
}

void ObDeadLockDetectorMgr::wait()
{
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    DETECT_LOG(WARN, "deadlock detector manager is not initialized");
  } else if (OB_FAIL(time_wheel_.wait())) {
    DETECT_LOG(WARN, "ObDeadLockDetectorMgr wait time wheel failed", KR(ret));
  }
  local_task_queue_.wait();
}

// ObDeadLockDetectorMgr destroy process, all related role should be destroyed within this
void ObDeadLockDetectorMgr::destroy()
{
  int ret = OB_SUCCESS;
  if (false == is_inited_) {
    DETECT_LOG(WARN, "ObDeadLockDetectorMgr not init or has been destroyed");
  } else {
    if (!is_stopping_()) {
      stop();
    }
    wait();
    local_task_queue_.destroy();
    detector_map_.destroy();
    time_wheel_.destroy();
    is_inited_ = false;
    DETECT_LOG(INFO, "ObDeadLockDetectorMgr destroy success");
  }
  DETECT_LOG(INFO, "ObDeadLockDetectorMgr destroy called", K(lbt()));

  return;
}

int ObDeadLockDetectorMgr::get_detector_(const UserBinaryKey &user_key,
                                         DetectorRefGuard &detector_guard)
{
  CHECK_INIT();
  CHECK_ARGS(user_key);
  int ret = OB_SUCCESS;
  ObIDeadLockDetector *p_detector = nullptr;

  if (OB_FAIL(detector_map_.get(user_key, p_detector))) {
    // DETECT_LOG(WARN, "detector_map_ get detector failed", KR(ret), K(user_key), KP(p_detector));
  } else {
    detector_guard.set_detector(p_detector);
  }

  return ret;
}

int ObDeadLockDetectorMgr::unregister_key_(const UserBinaryKey &key)
{
  #define PRINT_WRAPPER KR(ret), K(key)
  int ret = common::OB_SUCCESS;
  DetectorRefGuard ref_guard;
  if (OB_FAIL(get_detector_(key, ref_guard))) {
    // DETECT_LOG(WARN, "get_detector failed", PRINT_WRAPPER);
  } else {
    ref_guard.get_detector()->unregister_timer_task();
    if (OB_FAIL(detector_map_.del(key))) {
      DETECT_LOG(WARN, "detector_map_ erase node failed", PRINT_WRAPPER);
    } else {
      DETECT_LOG(TRACE, "unregister key success", PRINT_WRAPPER);
    }
  }
  return ret;
  #undef PRINT_WRAPPER
}

int ObDeadLockDetectorMgr::post_lcl_state_(const UserBinaryKey &dest_key,
                                           const int64_t lclv,
                                           const ObLCLLabel &label,
                                           const int64_t send_ts)
{
  return is_stopping_()
      ? OB_IN_STOP_STATE
      : local_task_queue_.push_lcl_state(dest_key, lclv, label, send_ts);
}

int ObDeadLockDetectorMgr::post_cycle_info_(const ObDeadLockCycleInfo &cycle_info)
{
  return is_stopping_()
      ? OB_IN_STOP_STATE
      : local_task_queue_.push_cycle_info(cycle_info);
}

int ObDeadLockDetectorMgr::post_parent_notification_(const UserBinaryKey &parent_key,
                                                     const UserBinaryKey &child_key)
{
  return is_stopping_()
      ? OB_IN_STOP_STATE
      : local_task_queue_.push_parent_notification(parent_key, child_key);
}

int ObDeadLockDetectorMgr::process_lcl_state_(const UserBinaryKey &dest_key,
                                              const int64_t lclv,
                                              const ObLCLLabel &label,
                                              const int64_t send_ts)
{
  CHECK_INIT();
  CHECK_ARGS(dest_key, lclv, label, send_ts);
  #define PRINT_WRAPPER KR(ret), K(dest_key), K(lclv), K(label), K(send_ts)
  int ret = OB_SUCCESS;
  DetectorRefGuard ref_guard;

  if (OB_FAIL(get_detector_(dest_key, ref_guard))) {
    if (OB_ENTRY_NOT_EXIST == ret) {
      ret = OB_SUCCESS;
    } else {
      DETECT_LOG(WARN, "fail to get detector", PRINT_WRAPPER);
    }
  } else if (OB_FAIL(ref_guard.get_detector()->process_lcl_state(lclv, label, send_ts))) {
    ObIDeadLockDetector *detector = ref_guard.get_detector();
    DETECT_LOG(WARN, "fail to process lcl state", PRINT_WRAPPER, KP(detector));
  } else {}

  return ret;
  #undef PRINT_WRAPPER
}

int ObDeadLockDetectorMgr::process_cycle_info_(const ObDeadLockCycleInfo &cycle_info)
{
  CHECK_INIT();
  CHECK_ARGS(cycle_info);
  #define PRINT_WRAPPER KR(ret), K(cycle_info)
  int ret = OB_SUCCESS;
  DetectorRefGuard ref_guard;

  (void)check_and_report_cycle_(cycle_info);
  if (OB_FAIL(get_detector_(cycle_info.get_dest_key(), ref_guard))) {
    if (REACH_TIME_INTERVAL(100 * 1000)) {
      // the local resource has been unregistered
      DETECT_LOG(INFO, "dest_resource not in map", PRINT_WRAPPER);
    }
  } else if (OB_FAIL(ref_guard.get_detector()->process_cycle_info(cycle_info))) {
    ObIDeadLockDetector *detector = ref_guard.get_detector();
    DETECT_LOG(WARN, "fail to process cycle info", PRINT_WRAPPER, KP(detector));
  } else {
    // do nothing
  }

  return ret;
  #undef PRINT_WRAPPER
}

int ObDeadLockDetectorMgr::process_parent_notification_(const UserBinaryKey &parent_key,
                                                        const UserBinaryKey &child_key)
{
  CHECK_INIT();
  CHECK_ARGS(parent_key, child_key);
  #define PRINT_WRAPPER KR(ret), KP(p_detector), K(parent_key), K(child_key)
  int ret = OB_SUCCESS;
  ObIDeadLockDetector *p_detector = nullptr;

  const UserBinaryKey &binary_key = parent_key;
  if (common::OB_SUCCESS == (ret = detector_map_.get(binary_key, p_detector))) {
    ret = common::OB_ENTRY_EXIST;
    detector_map_.revert(p_detector);
  } else {
    ObMemAttr attr(MEMORY_LABEL);
    ObDeadLockDetectorMgr *p_deadlock_detector_mgr = share::g_mp->dead_lock_detector_mgr();
    if (OB_ISNULL(p_deadlock_detector_mgr)) {
      ret = OB_ERR_UNEXPECTED;
      DETECT_LOG(ERROR, "can not get ObDeadLockDetectorMgr", KP(p_deadlock_detector_mgr));
    } else if (OB_FAIL(p_deadlock_detector_mgr->inner_alloc_handle_.inner_factory_.create(binary_key,
                                                            [](const common::ObIArray<ObDetectorInnerReportInfo> &,
                                                               const int64_t) -> int { DETECT_LOG_RET(ERROR, common::OB_ERR_UNEXPECTED, "should not kill inner node");
                                                                                       return common::OB_ERR_UNEXPECTED; },
                                                            [binary_key,attr](ObDetectorUserReportInfo& report_info) -> int {
                                                              ObSharedGuard<char> ptr;
                                                              ptr.assign((char*)"detector", [](char*){});
                                                              report_info.set_module_name(ptr);
                                                              char *buffer = (char*)ob_malloc(sizeof(char) * 128, attr);
                                                              if (OB_NOT_NULL(buffer)) {
                                                                binary_key.to_string(buffer, 128);
                                                                ptr.assign(buffer, [](char* p){ ob_free(p); });
                                                              } else {
                                                                ptr.assign((char*)"inner visitor", [](char*){});
                                                              }
                                                              report_info.set_visitor(ptr);
                                                              ptr.assign((char*)"waiting for child execution", [](char*){});
                                                              report_info.set_resource(ptr);
                                                              return common::OB_SUCCESS;
                                                            },
                                                            ObDetectorPriority(PRIORITY_RANGE::EXTREMELY_HIGH, 0),
                                                            0,
                                                            0,
                                                            true,
                                                            p_detector))) {
      DETECT_LOG(WARN, "create new detector instance failed", PRINT_WRAPPER);
    } else if (OB_FAIL(detector_map_.insert_and_get(binary_key, p_detector))) {
      DETECT_LOG(WARN, "detector_map_ insert key and value failed", PRINT_WRAPPER);
      p_deadlock_detector_mgr->inner_alloc_handle_.inner_factory_.release(p_detector);
    } else if (is_stopping_()) {
      ret = OB_IN_STOP_STATE;
      (void)detector_map_.del(binary_key);
      detector_map_.revert(p_detector);
    } else if (FALSE_IT(p_detector->set_timeout(10 * 1000 * 1000))) {
    } else if (OB_FAIL(p_detector->register_timer_task())) {
      if (common::OB_ENTRY_NOT_EXIST == ret) {
        ret = common::OB_EAGAIN;// telling user there is a concurrent problem, need retry
      }
      DETECT_LOG(WARN, "start timer task failed", PRINT_WRAPPER);
      (void)detector_map_.del(binary_key);
      detector_map_.revert(p_detector);
    } else {
      ObDependencyResource resource(child_key);
      if (OB_FAIL(p_detector->block(resource))) {
        DETECT_LOG(WARN, "block child failed", PRINT_WRAPPER);
        p_detector->unregister_timer_task();
        (void)detector_map_.del(binary_key);
      } else {
        DETECT_LOG(INFO, "register parent key success", PRINT_WRAPPER);
      }
      detector_map_.revert(p_detector);
    }
  }

  return ret;
  #undef PRINT_WRAPPER
}

int ObDeadLockDetectorMgr::check_and_report_cycle_(
                           const ObDeadLockCycleInfo &cycle_info)
{
  int ret = OB_SUCCESS;
  if (cycle_info.get_collected_info().empty()) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    const ObDetectorInnerReportInfo &organizer = cycle_info.get_collected_info().at(0);
    if (organizer.get_user_key() == cycle_info.get_dest_key()) {
      uint64_t cycle_hash = calculate_cycle_hash_(cycle_info);
      if (OB_FAIL(check_and_record_cycle_hash_(cycle_hash))) {
        DETECT_LOG(INFO, "this cycle may has been reported",
                         KR(ret), K(cycle_info), K(cycle_hash));
      } else {
        if (OB_FAIL(ObDeadLockInnerTableService::
                    insert_all(cycle_info.get_collected_info()))) {
          DETECT_LOG(INFO, "report inner table success", KR(ret), K(cycle_info));
        } else {
          DETECT_LOG(INFO, "report inner table success", K(cycle_info));
        }
      }
    }
  }
  return ret;
}

uint64_t ObDeadLockDetectorMgr::calculate_cycle_hash_(
                                const ObDeadLockCycleInfo &cycle_info)
{
  uint64_t hash = 0;
  const ObArray<ObDetectorInnerReportInfo> &collected_info = cycle_info.get_collected_info();
  for (int64_t idx = 0; idx < collected_info.count(); ++idx) {
    const uint64_t key_hash = collected_info.at(idx).get_user_key().hash();
    const uint64_t id = collected_info.at(idx).get_detector_id();
    hash = murmurhash(&key_hash, sizeof(key_hash), hash);
    hash = murmurhash(&id, sizeof(id), hash);
  }
  return hash;
}

template<typename T, int POW_OF_2 = 7>
class LimitRecordBuffer
{
  static_assert(POW_OF_2<=20,
                "slots defined more than 2^20=1048576, be sure you want so many slots");
public:
  LimitRecordBuffer() : begin_(0), end_(0) {}
  int check_and_push(const T &element) {
    int ret = OB_SUCCESS;
    ObSpinLockGuard guard(lock_);
    uint64_t idx = begin_;
    for (; idx < end_ && OB_SUCC(ret); ++idx) {
      if (buffer_[real_idx_(idx)] == element) {
        ret = OB_ENTRY_EXIST;
      }
    }
    if (idx == end_) {// not exist
      buffer_[real_idx_(end_++)] = element;
      if (end_ - begin_ > NUM_OF_SLOTS) {
        begin_ = end_ - NUM_OF_SLOTS;
      }
    }
    return ret;
  }
private:
  static constexpr const uint64_t NUM_OF_SLOTS = 1L << POW_OF_2;
  static constexpr const uint64_t MASK = NUM_OF_SLOTS - 1;
  inline uint64_t real_idx_(const uint64_t logic_idx) {
    return (logic_idx & MASK);
  }
  uint64_t begin_;
  uint64_t end_;
  ObSpinLock lock_;
  T buffer_[NUM_OF_SLOTS];
};

int ObDeadLockDetectorMgr::check_and_record_cycle_hash_(const uint64_t hash)
{
  static LimitRecordBuffer<uint64_t> reported_cycle_record;
  return reported_cycle_record.check_and_push(hash);
}

}// detector
}// share
}// oceanbase
