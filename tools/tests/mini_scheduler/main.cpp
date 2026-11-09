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

int main(){
  struct Case{const char *name;void(*run)();};
  const Case cases[]={
    {"largest_after_4765_small",test_largest_after_4765},
    {"fifo_progress_under_continual_large_arrivals",test_fifo_progress_with_continual_large_arrivals},
    {"mds_no_running_protection",test_mds_no_running_protection},
    {"mini_no_running_protection",test_mini_no_running_protection},
    {"single_worker_balance_with_diagnostic_resets",test_single_worker_balance_and_diagnostic_resets},
    {"tx_progress_with_continual_mini_mds",test_tx_progress_with_continual_mini_and_mds},
    {"mixed_single_worker_repeated_large_progress",test_mixed_single_worker_keeps_selecting_large},
    {"blocked_largest_list_mutation",test_blocked_largest_list_mutation},
    {"positive_indegree_largest",test_positive_indegree_largest},
    {"preferred_type_all_blocked_fallback",test_preferred_type_all_blocked},
    {"largest_no_ready_task_fallback",test_largest_no_ready_task},
    {"unknown_size_zero_fifo",test_unknown_size_fifo},
    {"emergency_precedence",test_emergency},
    {"emergency_respects_dependency",test_emergency_dependency},
    {"stopped_failed_cleanup_with_actual_delete",test_stopped_failed_cleanup},
    {"node_running_continuation_no_active_task",test_running_continuation_without_active_task},
    {"active_big_excluded_from_size_preference",test_active_big_excluded_from_size_preference},
    {"other_priority_fifo",test_other_priority_fifo},
    {"equal_size_fifo",test_equal_size_fifo},
    {"empty",test_empty},
    {"selection_does_not_consume_dispatch_state",test_pop_does_not_consume_dispatch_state},
    {"other_priority_dispatch_state_unchanged",test_other_priority_dispatch_state_unchanged},
  };
  size_t failures=0;
  for(const auto &test:cases){try{test.run();std::cout<<"PASS "<<test.name<<"\n";}catch(const std::exception &error){++failures;std::cerr<<"FAIL "<<test.name<<": "<<error.what()<<"\n";}}
  std::cout<<"RESULT tests="<<std::size(cases)<<" failed="<<failures<<"\n";
  if(failures)return 1;
  benchmark();return 0;
}
