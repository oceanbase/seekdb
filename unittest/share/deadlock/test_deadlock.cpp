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

#define UNITTEST_DEBUG
#include <gmock/gmock.h>
#define private public
#define protected public
#include "observer/ob_srv_network_frame.h"
#include "share/deadlock/ob_lcl_scheme/ob_lcl_node.h"
#include "share/deadlock/test/test_key.h"
#include "share/deadlock/mock_deadlock_rpc.h"

using namespace oceanbase::obrpc;
using namespace std;

namespace oceanbase {
namespace unittest {

using namespace common;
using namespace share::detector;
using namespace share;
using namespace std;
static ObDetectorUserReportInfo user_report_info;
// User-defined operation operation
class TestOperation {
public:
  TestOperation(uint64_t hash) : hash_(hash) {
    ATOMIC_AAF(&alive_count, 1);
    ObSharedGuard<char> ptr;
    ptr.assign((char*)"test", [](char*){});
    user_report_info.set_module_name(ptr);
    ptr.assign((char*)"a", [](char*){});
    user_report_info.set_visitor(ptr);
    ptr.assign((char*)"b", [](char*){});
    user_report_info.set_resource(ptr);
  }
  TestOperation(const TestOperation &rhs) : hash_(rhs.hash_), key_(rhs.key_) {
    ATOMIC_AAF(&alive_count, 1);
    ObSharedGuard<char> ptr;
    ptr.assign((char*)"test", [](char*){});
    user_report_info.set_module_name(ptr);
    ptr.assign((char*)"a", [](char*){});
    user_report_info.set_visitor(ptr);
    ptr.assign((char*)"b", [](char*){});
    user_report_info.set_resource(ptr);
  }
  TestOperation(const ObDeadLockTestIntKey &key) :
  key_(key.get_value()) {
    ATOMIC_AAF(&alive_count, 1);
    ObSharedGuard<char> ptr;
    ptr.assign((char*)"test", [](char*){});
    user_report_info.set_module_name(ptr);
    ptr.assign((char*)"a", [](char*){});
    user_report_info.set_visitor(ptr);
    ptr.assign((char*)"a", [](char*){});
    user_report_info.set_resource(ptr);
  }
  ~TestOperation() { ATOMIC_AAF(&alive_count, -1); }
  TO_STRING_KV(K(key_));
  uint64_t hash_;
public:
  int operator()(const common::ObIArray<ObDetectorInnerReportInfo> &collected_info, const int64_t self_index) {
    UNUSED(collected_info);
    int64_t interval = chrono::duration_cast<chrono::milliseconds>(chrono::high_resolution_clock::now() - loop_occurd_time).count();
    DETECT_LOG(INFO, "detect delay time", K(interval));
    v_record.push_back(interval);
    ObDetectorPriority lowest_priority(PRIORITY_RANGE::EXTREMELY_HIGH, UINT64_MAX);
    int64_t lowest_priority_idx = -1;
    for (int64_t i = 0; i < collected_info.count(); ++i) {
      if (collected_info.at(i).get_priority() < lowest_priority) {
        lowest_priority_idx = i;
        lowest_priority = collected_info.at(i).get_priority();
      }
    }
    v_record_is_lowest_priority.push_back(self_index == lowest_priority_idx);
    v_killed_node.push_back(key_.get_value());
    if (key_.get_value() != -1) {
      MTL(ObDeadLockDetectorMgr*)->unregister_key(key_);
    }
    std::cout << "my hash is:" << hash_ << std::endl;
    return OB_SUCCESS;
  }
  ObDeadLockTestIntKey key_;
  static vector<int64_t> v_record;
  static vector<bool> v_record_is_lowest_priority;
  static int64_t alive_count;
  static vector<int> v_killed_node;
  static decltype(chrono::high_resolution_clock::now()) loop_occurd_time;
};
// The timing of registration and setting up dependencies during actual use is random, through random sleep to simulate the usage scenario
auto collect_callback = [](ObDetectorUserReportInfo& arg){ return arg.assign(user_report_info); };

#define JUDGE_RECORDER(v1,v2,v3,v4,v5,v6,v7,v8)\
ASSERT_EQ(recorder.function_default_construct_time, v1);\
ASSERT_EQ(recorder.function_copy_construct_time, v2);\
ASSERT_EQ(recorder.function_move_construct_time, v3);\
ASSERT_EQ(recorder.function_general_construct_time, v4);\
ASSERT_EQ(recorder.function_copy_assign_time, v5);\
ASSERT_EQ(recorder.function_move_assign_time, v6);\
ASSERT_EQ(recorder.function_general_assign_time, v7);\
ASSERT_EQ(recorder.derived_construct_time, v8);\
recorder.reset();

function::DebugRecorder &recorder = function::DebugRecorder::get_instance();

ObDeadLockDetectorMgr *mgr;

class TestObDeadLockDetector : public ::testing::Test {
public:
  TestObDeadLockDetector() :
    case_num_(0), ctx(1) {}
  ~TestObDeadLockDetector() {}
  virtual void SetUp() {
    share::ObTenantEnv::get_tenant_local()->id_ = 1;
    auto &gctx = GCTX;
    gctx.net_frame_ = new observer::ObSrvNetworkFrame(gctx);
    gctx.net_frame_->rpc_transport_ = reinterpret_cast<rpc::frame::ObReqTransport*>(0x123456);
    gctx.self_addr_seq_.server_addr_ = ObAddr(ObAddr::VER::IPV4, "127.0.0.1", 1234);
    mgr = new ObDeadLockDetectorMgr();
    DETECT_LOG(INFO, "print mgr", KP(mgr), KP(&mgr->inner_alloc_handle_.inner_factory_.release_count_));
    ctx.set(mgr);
    ObTenantEnv::set_tenant(&ctx);
    mgr->init();
    static int case_num = 0;
    ++case_num;
    cout << "\n<<<<<<<<<<<<<<<<<<<<" << "start case" << case_num << ">>>>>>>>>>>>>>>>>>>>" << endl;
    // do not use rpc, will core dump
    // ASSERT_EQ(OB_SUCCESS, MTL(ObDeadLockDetectorMgr*)->init());
    oceancase::unittest::MockDeadLockRpc *rpc = (oceancase::unittest::MockDeadLockRpc *)ob_malloc(sizeof(oceancase::unittest::MockDeadLockRpc),
                                                                                                  ObNewModIds::TEST);
    rpc = new (rpc) oceancase::unittest::MockDeadLockRpc();
    MTL(ObDeadLockDetectorMgr*)->rpc_ = rpc;

    ASSERT_EQ(OB_SUCCESS, MTL(ObDeadLockDetectorMgr*)->start());
    ObServerConfig::get_instance()._lcl_op_interval = 10000;
    std::this_thread::sleep_for(chrono::seconds(3));
    TestOperation::v_killed_node.clear();
  }
  virtual void TearDown() {
    MTL(ObDeadLockDetectorMgr*)->stop();
    MTL(ObDeadLockDetectorMgr*)->wait();
    MTL(ObDeadLockDetectorMgr*)->proxy_ = nullptr;
    MTL(ObDeadLockDetectorMgr*)->rpc_ = nullptr;
    MTL(ObDeadLockDetectorMgr*)->destroy();
    DETECT_LOG(INFO, "print mgr", KP(mgr), KP(&mgr->inner_alloc_handle_.inner_factory_.release_count_));
    int64_t release_count_from_mtl = MTL(ObDeadLockDetectorMgr*)->get_detector_release_count();
    int64_t release_count_from_obj = mgr->get_detector_release_count();
    DETECT_LOG(INFO, "print count", K(release_count_from_mtl), K(release_count_from_obj));
    ASSERT_EQ(MTL(ObDeadLockDetectorMgr*)->get_detector_create_count(), MTL(ObDeadLockDetectorMgr*)->get_detector_release_count());
    ASSERT_EQ(0, TestOperation::alive_count);// Expected user-created operation objects to be destroyed
    delete MTL(ObDeadLockDetectorMgr*);
  }
  template <typename T>
  ObIDeadLockDetector *get_detector_ptr(const T &key) {
    ObDeadLockDetectorMgr::DetectorRefGuard guard;
    UserBinaryKey binary_key;
    binary_key.set_user_key(key);
    MTL(ObDeadLockDetectorMgr*)->get_detector_(binary_key, guard);
    return guard.get_detector();
  }
private:
  int case_num_ = 0;
  share::ObTenantBase ctx;
};

vector<int64_t> TestOperation::v_record;
vector<bool> TestOperation::v_record_is_lowest_priority;
int64_t TestOperation::alive_count = 0;
vector<int> TestOperation::v_killed_node;
decltype(chrono::high_resolution_clock::now()) TestOperation::loop_occurd_time = chrono::high_resolution_clock::now();

TEST_F(TestObDeadLockDetector, test_ObDetectorUserReportInfo) {
  char a[5] = {'1', '2', '3', '4', '\0'};
  {
    ObDetectorUserReportInfo info;
    int ret = OB_SUCCESS;
    char *buffer = nullptr;
    if (OB_UNLIKELY(nullptr == (buffer = (char*)ob_malloc(128, "deadlockCB")))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      transaction::ObTransID self_trans_id;
      ObSharedGuard<char> temp_uniqe_guard;
      int step = 0;
      (void) self_trans_id.to_string(buffer, 128);
      char *node_key_buffer = a;
      if (++step && OB_FAIL(temp_uniqe_guard.assign((char*)"transaction", [](char*){}))) {
      } else if (++step && OB_FAIL(info.set_module_name(temp_uniqe_guard))) {
      } else if (++step && OB_FAIL(temp_uniqe_guard.assign(buffer, [](char* buffer){ ob_free(buffer); DETECT_LOG(INFO, "visitor str destory", K(lbt()));}))) {
      } else if (++step && OB_FAIL(info.set_visitor(temp_uniqe_guard))) {
      } else if (++step && OB_FAIL(temp_uniqe_guard.assign(node_key_buffer, [](char*ptr){ ptr[0] = '2'; DETECT_LOG(INFO, "resource str destory", K(lbt())); }))) {
      } else if (++step && OB_FAIL(info.set_resource(temp_uniqe_guard))) {
      } else if (++step && OB_FAIL(info.set_extra_info("current sql", "1"))) {
      } else {}
      if (OB_FAIL(ret)) {
        DETECT_LOG(WARN, "get string failed in deadlock", KR(ret), K(step));
      }
    }
    ASSERT_EQ(info.get_extra_columns_names().size(), 1);
    ASSERT_EQ(info.get_extra_columns_values().size(), 1);
    DETECT_LOG(INFO, "print info", K(info));
  }
  ASSERT_EQ(a[0], '2');
}
// Normal registration key and unregistration key process
// Test focus:
// 1, after the user registrer_key, the corresponding detector node will be created, and you can get the corresponding resource_id.
// 2, after the user unregisters the key, the corresponding detector node is expected to be destroyed.
// 3, after detector node is destroyed, expected user-created operation object will also be destroyed.
TEST_F(TestObDeadLockDetector, register_ungister_key) {
  // Construct your own operation object from the outer layer
  TestOperation* op = new TestOperation(7710038271457258879UL);
  ASSERT_EQ(1, TestOperation::alive_count);// A user created an operation object
  ASSERT_EQ(OB_SUCCESS, MTL(ObDeadLockDetectorMgr*)->register_key(ObDeadLockTestIntKey(1), *op, collect_callback, 1));// register key
  //JUDGE_RECORDER()
  auto call_back = get_detector_ptr(ObDeadLockTestIntKey(1));
  ASSERT_EQ(1, MTL(ObDeadLockDetectorMgr*)->get_detector_create_count());// Expected to synchronously create one detector object
  ObIDeadLockDetector *ptr = get_detector_ptr(ObDeadLockTestIntKey(1));
  ASSERT_EQ(OB_SUCCESS, MTL(ObDeadLockDetectorMgr*)->unregister_key(ObDeadLockTestIntKey(1))); // unregister key
  delete op;
}
// Handling registration and deregistration of key during exceptions
// Test focus:
// 1,
// 2, the registration of duplicate keys of the same type should fail.
// 3, a key should be able to register again after registration and cancellation
// 3, a key should be able to register again after registration and deregistration
// 4, a key should fail to unregister if it has not been registered
// 5, a registered key that has already been deregistered should fail on a second deregistration attempt
// 6, When ObDeadLockDetectorMgr is destroyed, there are still detectors that have not been destroyed. It is expected that all created detectors will be destroyed after Mgr is destroyed.
TEST_F(TestObDeadLockDetector, register_ungister_in_wrong_way) {
  // do not use rpc, will core dump
  TestOperation *op = new TestOperation(7710038271457258879UL);
  ASSERT_EQ(OB_SUCCESS, MTL(ObDeadLockDetectorMgr*)->register_key(ObDeadLockTestIntKey(12), *op, collect_callback, 1));
  //auto &call_back = ((ObLCLNode*)(get_detector_ptr(ObDeadLockTestIntKey(12))))->get_detect_callback_();
  //TestOperation &op_cp = static_cast<ObFunction<int(const common::ObIArray<ObDetectorInnerReportInfo> &, const int64_t)>::Derived<TestOperation>*>(call_back.get_func_ptr())->func_;
  DETECT_LOG(INFO, "1");
  ASSERT_NE(OB_SUCCESS, MTL(ObDeadLockDetectorMgr*)->register_key(ObDeadLockTestIntKey(12), *op, collect_callback, 1));// 2, the registration of duplicate keys of the same type should fail
  DETECT_LOG(INFO, "2");
  ASSERT_EQ(OB_SUCCESS, MTL(ObDeadLockDetectorMgr*)->unregister_key(ObDeadLockTestIntKey(12)));
  DETECT_LOG(INFO, "3");
  ASSERT_EQ(OB_SUCCESS, MTL(ObDeadLockDetectorMgr*)->register_key(ObDeadLockTestIntKey(12), *op, collect_callback, 1));// 3, a key should be able to be re-registered after registration and deregistration
  DETECT_LOG(INFO, "4");
  ASSERT_NE(OB_SUCCESS, MTL(ObDeadLockDetectorMgr*)->unregister_key(ObDeadLockTestIntKey(13))); // 4, an unregistered key should fail to unregister
  DETECT_LOG(INFO, "5");
  cout << MTL(ObDeadLockDetectorMgr*)->get_detector_create_count() << " " << MTL(ObDeadLockDetectorMgr*)->get_detector_release_count();
  std::this_thread::sleep_for(chrono::milliseconds(100));
  ASSERT_EQ(OB_SUCCESS, MTL(ObDeadLockDetectorMgr*)->unregister_key(ObDeadLockTestIntKey(12)));
  DETECT_LOG(INFO, "6");
  ASSERT_NE(OB_SUCCESS, MTL(ObDeadLockDetectorMgr*)->unregister_key(ObDeadLockTestIntKey(12))); // 5, a key that has already been registered should fail when unregistered again after being unregistered
  ASSERT_EQ(OB_SUCCESS, MTL(ObDeadLockDetectorMgr*)->register_key(ObDeadLockTestIntKey(12), *op, collect_callback, 1));// 6, When ObDeadLockDetectorMgr is destroyed, there are still detectors that have not been destroyed. It is expected that all created detectors will be destroyed after Mgr is destroyed
  std::this_thread::sleep_for(chrono::microseconds(PERIOD * 2));
  delete op;
}
// block and activate process flow
// Test focus:
// 1, block an existing key can succeed
// 2, block a key that has already been blocked returns failure
// 3, can block multiple different existing keys
// 4, block a non-existent key returns failure
// 5, activate a blocked key can succeed
// 6, repeat activate a key returns failure
// 7, activate an unregistered key returns failure
// 8, activate a key that is not in the block list but has already been registered returns failure
TEST_F(TestObDeadLockDetector, block_and_activate) {
  TestOperation *op = new TestOperation(7710038271457258879UL);
  // Register the first key
  ASSERT_EQ(OB_SUCCESS, MTL(ObDeadLockDetectorMgr*)->register_key(ObDeadLockTestIntKey(1), *op, collect_callback, 1));
  // Register the second key
  ASSERT_EQ(OB_SUCCESS, MTL(ObDeadLockDetectorMgr*)->register_key(ObDeadLockTestIntKey(2), *op, collect_callback, 1));
  // Register the third key
  ASSERT_EQ(OB_SUCCESS, MTL(ObDeadLockDetectorMgr*)->register_key(ObDeadLockTestDoubleKey(3.0), *op, collect_callback, 1));
  // Register the fourth key
  ASSERT_EQ(OB_SUCCESS, MTL(ObDeadLockDetectorMgr*)->register_key(ObDeadLockTestDoubleKey(4.0), *op, collect_callback, 1));
  // Set up dependency relationship for two keys
  ASSERT_EQ(OB_SUCCESS, MTL(ObDeadLockDetectorMgr*)->block(ObDeadLockTestIntKey(1), ObDeadLockTestIntKey(2)));// 1, block an already existing key can succeed
  ASSERT_NE(OB_SUCCESS, MTL(ObDeadLockDetectorMgr*)->block(ObDeadLockTestIntKey(1), ObDeadLockTestIntKey(2)));// 2, block an already blocked key returns failure
  ASSERT_EQ(OB_SUCCESS, MTL(ObDeadLockDetectorMgr*)->block(ObDeadLockTestIntKey(1), ObDeadLockTestDoubleKey(3.0)));// 3, can block multiple different existing keys
  // ASSERT_NE(OB_SUCCESS, MTL(ObDeadLockDetectorMgr*)->block(ObDeadLockTestIntKey(1), ObDeadLockTestDoubleKey(5.0))); // 4, block a non-existent key returns failure
  ASSERT_EQ(OB_SUCCESS, MTL(ObDeadLockDetectorMgr*)->activate(ObDeadLockTestIntKey(1), ObDeadLockTestIntKey(2)));// 5, activate a blocked key can succeed
  ASSERT_NE(OB_SUCCESS, MTL(ObDeadLockDetectorMgr*)->activate(ObDeadLockTestIntKey(1), ObDeadLockTestIntKey(2)));// 6, duplicate activate a key returns failure
  ASSERT_NE(OB_SUCCESS, MTL(ObDeadLockDetectorMgr*)->activate(ObDeadLockTestIntKey(1), ObDeadLockTestDoubleKey(5.0))); // 7, activate an unregistered key returns failure
  ASSERT_NE(OB_SUCCESS, MTL(ObDeadLockDetectorMgr*)->activate(ObDeadLockTestIntKey(1), ObDeadLockTestDoubleKey(4.0))); // 8, activate a key that is not in the block list but has already been registered returns failure
  delete op;
}
// Test the deadlock detection function
// Test focus:
// 1, deadlock will not be detected when deadlock has not formed
// 2, After a deadlock is formed, it can be detected within a certain period of time, the detection time does not exceed (number of nodes + 1) * Transmit interval
TEST_F(TestObDeadLockDetector, dead_lock) {
  int loop_times = 5;
  std::srand(std::time(nullptr));
  for (int i = 0; i < loop_times; ++i) {// perform multiple tests to see the maximum, minimum, and average values of detection latency
    int index = i * 5;
    std::this_thread::sleep_for(chrono::milliseconds(std::rand() % 300));
    // register the first key
    // TestOperation *op1 = new TestOperation(ObDeadLockTestIntKey(index + 1));
    // ASSERT_EQ(OB_SUCCESS, MTL(ObDeadLockDetectorMgr*)->register_key(ObDeadLockTestIntKey(index + 1), *op1, collect_callback, 1, std::rand() % 100));
    // register the second key
    TestOperation *op2 = new TestOperation(ObDeadLockTestIntKey(index + 2));
    ASSERT_EQ(OB_SUCCESS, MTL(ObDeadLockDetectorMgr*)->register_key(ObDeadLockTestIntKey(index + 2), *op2, collect_callback, 1, std::rand() % 100));
    // Register the third key
    TestOperation *op3 = new TestOperation(ObDeadLockTestIntKey(index + 3));
    ASSERT_EQ(OB_SUCCESS, MTL(ObDeadLockDetectorMgr*)->register_key(ObDeadLockTestIntKey(index + 3), *op3, collect_callback, 1, std::rand() % 100));
    // Register the fourth key
    TestOperation *op4 = new TestOperation(ObDeadLockTestIntKey(index + 4));
    ASSERT_EQ(OB_SUCCESS, MTL(ObDeadLockDetectorMgr*)->register_key(ObDeadLockTestIntKey(index + 4), *op4, collect_callback, 1, std::rand() % 100));
    // Register the fifth key
    TestOperation *op5 = new TestOperation(ObDeadLockTestIntKey(index + 5));
    ASSERT_EQ(OB_SUCCESS, MTL(ObDeadLockDetectorMgr*)->register_key(ObDeadLockTestIntKey(index + 5), *op5, collect_callback, 1, std::rand() % 100));
    // Set up dependency relationship for two keys
    // ASSERT_EQ(OB_SUCCESS, MTL(ObDeadLockDetectorMgr*)->block(ObDeadLockTestIntKey(index + 1), ObDeadLockTestIntKey(index + 2)));
    ASSERT_EQ(OB_SUCCESS, MTL(ObDeadLockDetectorMgr*)->add_parent(ObDeadLockTestIntKey(index + 2), GCTX.self_addr(), ObDeadLockTestIntKey(index + 1)));
    ASSERT_EQ(OB_SUCCESS, MTL(ObDeadLockDetectorMgr*)->block(ObDeadLockTestIntKey(index + 2), ObDeadLockTestIntKey(index + 3)));
    ASSERT_EQ(OB_SUCCESS, MTL(ObDeadLockDetectorMgr*)->block(ObDeadLockTestIntKey(index + 3), ObDeadLockTestIntKey(index + 4)));
    ASSERT_EQ(OB_SUCCESS, MTL(ObDeadLockDetectorMgr*)->block(ObDeadLockTestIntKey(index + 4), ObDeadLockTestIntKey(index + 5)));
    ASSERT_EQ(OB_SUCCESS, MTL(ObDeadLockDetectorMgr*)->block(ObDeadLockTestIntKey(index + 5), ObDeadLockTestIntKey(index + 1)));
    TestOperation::loop_occurd_time = chrono::high_resolution_clock::now();
    std::this_thread::sleep_for(chrono::microseconds(PERIOD * 2));

    //delete op1;
    delete op2;
    delete op3;
    delete op4;
    delete op5;
    ASSERT_EQ(i + 1, TestOperation::v_killed_node.size());// Only one node can detect deadlock in a cycle
  }
  ASSERT_EQ(loop_times, TestOperation::v_killed_node.size());// Only one node can detect deadlock in a loop
  ASSERT_EQ(loop_times, TestOperation::v_record_is_lowest_priority.size());
  for (int i = 0; i < TestOperation::v_record_is_lowest_priority.size(); ++i)
    ASSERT_EQ(true, TestOperation::v_record_is_lowest_priority[i]);
  cout << "max delay:" << *max_element(TestOperation::v_record.begin(), TestOperation::v_record.end()) << "ms" << endl;
  cout << "min delay:" << *min_element(TestOperation::v_record.begin(), TestOperation::v_record.end()) << "ms" << endl;
  cout << "average delay:" << accumulate(TestOperation::v_record.begin(), TestOperation::v_record.end(), 0) / TestOperation::v_record.size() << "ms" << endl;
}

class DeadLockBlockCallBack {
public:
  DeadLockBlockCallBack(uint64_t hash) : hash_(hash) {
    TRANS_LOG(INFO, "hash value when created", K(hash), K(hash_));
  }
  int operator()(ObDependencyResource &resource, bool &need_remove) {
    UNUSED(resource);
    UNUSED(need_remove);
    std::cout<<"hash_:"<<hash_<<std::endl;
    int ret = OB_SUCCESS;
    return ret;
  }
private:
  uint64_t hash_;
};

TEST_F(TestObDeadLockDetector, block_call_back) {
  DeadLockBlockCallBack deadlock_block_call_back(7710038271457258879UL);
  ObFunction<int(ObDependencyResource &,bool &)> func = deadlock_block_call_back;
  ObDependencyResource re;
  bool n;
  func(re, n);
}

TEST_F(TestObDeadLockDetector, test_timeout) {
  TestOperation *op2 = new TestOperation(ObDeadLockTestIntKey(1));
  ASSERT_EQ(OB_SUCCESS, MTL(ObDeadLockDetectorMgr*)->register_key(ObDeadLockTestIntKey(1), *op2, collect_callback, 1, std::rand() % 100));
  ASSERT_EQ(OB_SUCCESS, MTL(ObDeadLockDetectorMgr*)->register_key(ObDeadLockTestIntKey(2), *op2, collect_callback, 1, std::rand() % 100));
  MTL(ObDeadLockDetectorMgr*)->set_timeout(ObDeadLockTestIntKey(1), 1000);
  std::this_thread::sleep_for(chrono::seconds(1));
  delete op2;
  ASSERT_EQ(MTL(ObDeadLockDetectorMgr*)->get_detector_create_count(), MTL(ObDeadLockDetectorMgr*)->get_detector_release_count() + 1);
}

// 1 -> 5 -> 3 -> 4 -> 1
// 1 -> 2 - > 3 -> 4 -> 1
// 2 has global minimum priority
TEST_F(TestObDeadLockDetector, small_cycle_in_big_cycle_bad_case) {
  int loop_times = 5;
  // Register the first key
  TestOperation *op1 = new TestOperation(ObDeadLockTestIntKey(1));
  ASSERT_EQ(OB_SUCCESS, MTL(ObDeadLockDetectorMgr*)->register_key(ObDeadLockTestIntKey(1), *op1, collect_callback, 9));
  // Register the second key
  TestOperation *op2 = new TestOperation(ObDeadLockTestIntKey(2));
  ASSERT_EQ(OB_SUCCESS, MTL(ObDeadLockDetectorMgr*)->register_key(ObDeadLockTestIntKey(2), *op2, collect_callback, 0));// should kill this node
  // Register the third key
  TestOperation *op3 = new TestOperation(ObDeadLockTestIntKey(3));
  ASSERT_EQ(OB_SUCCESS, MTL(ObDeadLockDetectorMgr*)->register_key(ObDeadLockTestIntKey(3), *op3, collect_callback, 9));
  // Register the fourth key
  TestOperation *op4 = new TestOperation(ObDeadLockTestIntKey(4));
  ASSERT_EQ(OB_SUCCESS, MTL(ObDeadLockDetectorMgr*)->register_key(ObDeadLockTestIntKey(4), *op4, collect_callback, 9));
  // Register the fifth key
  TestOperation *op5 = new TestOperation(ObDeadLockTestIntKey(5));
  ASSERT_EQ(OB_SUCCESS, MTL(ObDeadLockDetectorMgr*)->register_key(ObDeadLockTestIntKey(5), *op5, collect_callback, 1));
  // Set up dependency relationship for two keys
  // ASSERT_EQ(OB_SUCCESS, MTL(ObDeadLockDetectorMgr*)->block(ObDeadLockTestIntKey(index + 1), ObDeadLockTestIntKey(index + 2)));
  ASSERT_EQ(OB_SUCCESS, MTL(ObDeadLockDetectorMgr*)->block(ObDeadLockTestIntKey(1), ObDeadLockTestIntKey(5)));
  ASSERT_EQ(OB_SUCCESS, MTL(ObDeadLockDetectorMgr*)->block(ObDeadLockTestIntKey(5), ObDeadLockTestIntKey(3)));
  ASSERT_EQ(OB_SUCCESS, MTL(ObDeadLockDetectorMgr*)->block(ObDeadLockTestIntKey(3), ObDeadLockTestIntKey(4)));
  ASSERT_EQ(OB_SUCCESS, MTL(ObDeadLockDetectorMgr*)->block(ObDeadLockTestIntKey(4), ObDeadLockTestIntKey(1)));
  ASSERT_EQ(OB_SUCCESS, MTL(ObDeadLockDetectorMgr*)->block(ObDeadLockTestIntKey(1), ObDeadLockTestIntKey(2)));
  ASSERT_EQ(OB_SUCCESS, MTL(ObDeadLockDetectorMgr*)->block(ObDeadLockTestIntKey(2), ObDeadLockTestIntKey(3)));
  TestOperation::loop_occurd_time = chrono::high_resolution_clock::now();
  std::this_thread::sleep_for(chrono::microseconds(PERIOD * 4));
  delete op1;
  delete op2;
  delete op3;
  delete op4;
  delete op5;
  for (auto v : TestOperation::v_killed_node) {
    DETECT_LOG(INFO, "kill node", K(v));
  }
  ASSERT_EQ(2, TestOperation::v_killed_node.size());
  ASSERT_EQ(true, TestOperation::v_killed_node[0] == 2 || TestOperation::v_killed_node[0] == 5);
  ASSERT_EQ(true, TestOperation::v_killed_node[1] == 2 || TestOperation::v_killed_node[1] == 5);
}

// TEST_F(TestObDeadLockDetector, test_lock_conflict_print) {
//   memtable::RetryInfo retry_info;
//   while ((ObClockGenerator::getClock() % 1000000) < 100_ms);
//   DETECT_LOG(INFO, "DEBUG1", K(ObClockGenerator::getClock()));
//   ASSERT_EQ(retry_info.need_print(), true);
//   retry_info.on_conflict();
//   ASSERT_EQ(retry_info.need_print(), false);
//   this_thread::sleep_for(std::chrono::milliseconds(100));
//   DETECT_LOG(INFO, "DEBUG2", K(ObClockGenerator::getClock()));
//   ASSERT_EQ(retry_info.need_print(), false);
//   this_thread::sleep_for(std::chrono::milliseconds(1000));
//   DETECT_LOG(INFO, "DEBUG", K(retry_info));
//   ASSERT_EQ(retry_info.need_print(), true);
//   for (int i = 0; i < 9; ++i) {
//     retry_info.on_conflict(); 
//   }
//   ASSERT_EQ(retry_info.need_print(), true);
// }

// TEST_F(TestObDeadLockDetector, print_timestamp) {
//   int64_t ts_ = 1623827288705600;
//   DETECT_LOG(INFO, "test ts", KTIME(ts_));
//   DETECT_LOG(INFO, "test ts", KTIME_(ts));
//   DETECT_LOG(INFO, "test ts", KTIMERANGE(ts_, DAY, MSECOND));
//   DETECT_LOG(INFO, "test ts", KTIMERANGE_(ts, DAY, MSECOND));
//   ObLCLMessage msg;
//   UserBinaryKey key;
//   key.set_user_key(ObDeadLockTestIntKey(1));
//   msg.set_args(GCTX.self_addr(),
//                key,
//                GCTX.self_addr(),
//                key,
//                2,
//                ObLCLLabel(1, ObDetectorPriority(1)),
//                ObClockGenerator::getRealClock());
//   char buffer[4000];
//   msg.to_string(buffer, 4000);
//   std::cout << "test ts" << buffer << std::endl;
//   DETECT_LOG(INFO, "test ts", K(msg));
// }

}// namespace unittest
}// namespace oceanbase

int main(int argc, char **argv)
{
  system("rm -rf test_deadlock.log");
  oceanbase::common::ObClockGenerator::init();
  oceanbase::common::ObLogger &logger = oceanbase::common::ObLogger::get_logger();
  logger.set_file_name("test_deadlock.log", false);
  logger.set_log_level(OB_LOG_LEVEL_INFO);
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
