static void require(bool condition,const std::string &message){if(!condition)throw std::runtime_error(message);}
static ObITask *pop(ObDagPrioScheduler &s){
  ObITask *task=nullptr;int result=s.pop_task_from_ready_list_(task);
  require(result==OB_SUCCESS && task!=nullptr,"expected runnable task, ret="+std::to_string(result));
  s.dag_list_[0].verify();s.dag_list_[1].verify();return task;
}
static void expect_owner(ObDagPrioScheduler &s,int owner){require(pop(s)->owner==owner,"unexpected task; expected "+std::to_string(owner));}
static void test_largest_after_4765(){
  ObDagPrioScheduler s;std::vector<std::unique_ptr<ObIDag>> small;
  for(int i=0;i<4765;i++){small.emplace_back(new ObIDag(i,ObDagType::DAG_TYPE_MINI_MERGE,4096));s.add(*small.back());}
  ObIDag big(4765,ObDagType::DAG_TYPE_MINI_MERGE,2LL<<30);s.add(big);expect_owner(s,4765);
}
static void test_mds_no_running_protection(){
  ObDagPrioScheduler s;ObIDag big(1,ObDagType::DAG_TYPE_MINI_MERGE,2LL<<30),mds(2,ObDagType::DAG_TYPE_MDS_MINI_MERGE);
  s.add(big);s.add(mds);s.counts.running[ObDagType::DAG_TYPE_MINI_MERGE]=4;expect_owner(s,2);
}
static void test_mini_no_running_protection(){
  ObDagPrioScheduler s;ObIDag mds(1,ObDagType::DAG_TYPE_MDS_MINI_MERGE),mini(2,ObDagType::DAG_TYPE_MINI_MERGE,4);
  s.add(mds);s.add(mini);s.counts.running[ObDagType::DAG_TYPE_MDS_MINI_MERGE]=6;expect_owner(s,2);
}
static void test_blocked_largest_list_mutation(){
  ObDagPrioScheduler s;ObIDag small(1,ObDagType::DAG_TYPE_MINI_MERGE,4),big(2,ObDagType::DAG_TYPE_MINI_MERGE,4000);
  big.blocked=true;s.add(small);s.add(big);expect_owner(s,1);
  require(s.dag_list_[1].contains(big),"blocked largest must move to waiting list");
}
static void test_positive_indegree_largest(){
  ObDagPrioScheduler s;ObIDag small(1,ObDagType::DAG_TYPE_MINI_MERGE,4),big(2,ObDagType::DAG_TYPE_MINI_MERGE,4000);
  big.indegree=1;s.add(big);s.add(small);expect_owner(s,1);
  // Preference may leave the blocked DAG unvisited until a FIFO turn.
  require(big.ready,"indegree blocked task must never execute");
}
static void test_preferred_type_all_blocked(){
  ObDagPrioScheduler s;ObIDag mds(1,ObDagType::DAG_TYPE_MDS_MINI_MERGE),small(2,ObDagType::DAG_TYPE_MINI_MERGE,4),big(3,ObDagType::DAG_TYPE_MINI_MERGE,4000);
  small.blocked=big.blocked=true;s.add(mds);s.add(small);s.add(big);s.counts.running[1]=4;expect_owner(s,1);
  require(s.dag_list_[1].contains(small)&&s.dag_list_[1].contains(big),"all blocked MINI must move safely");
}
static void test_largest_no_ready_task(){
  ObDagPrioScheduler s;ObIDag small(1,ObDagType::DAG_TYPE_MINI_MERGE,4),big(2,ObDagType::DAG_TYPE_MINI_MERGE,4000);
  big.state=ObIDag::DAG_STATUS_NODE_RUNNING;big.ready=false;s.add(small);s.add(big);expect_owner(s,1);
}
static void test_unknown_size_fifo(){
  ObDagPrioScheduler s;ObIDag first(1),second(2);s.add(first);s.add(second);expect_owner(s,1);
}
static void test_emergency(){
  ObDagPrioScheduler s;ObIDag big(1,ObDagType::DAG_TYPE_MINI_MERGE,2LL<<30),urgent(2,ObDagType::DAG_TYPE_MDS_MINI_MERGE);
  urgent.emergency=true;s.add(big);s.add(urgent);s.counts.running[1]=4;expect_owner(s,2);
}
static void test_emergency_dependency(){
  ObDagPrioScheduler s;ObIDag big(1,ObDagType::DAG_TYPE_MINI_MERGE,2LL<<30),urgent(2,ObDagType::DAG_TYPE_MDS_MINI_MERGE);
  urgent.emergency=true;urgent.indegree=1;s.add(big);s.add(urgent);expect_owner(s,1);
  require(s.dag_list_[1].contains(urgent),"emergency must respect dependency");
}
static void test_stopped_failed_cleanup(){
  ObDagPrioScheduler s;
  auto *failed=new ObIDag(1,ObDagType::DAG_TYPE_MINI_MERGE,8000);
  auto *stopped=new ObIDag(2,ObDagType::DAG_TYPE_MINI_MERGE,9000);
  failed->state=ObIDag::DAG_STATUS_NODE_FAILED;failed->delete_on_finish=true;
  stopped->state=ObIDag::DAG_STATUS_NODE_RUNNING;stopped->stop=true;stopped->delete_on_finish=true;
  s.add(*failed);s.add(*stopped);ObITask *task=nullptr;
  require(s.pop_task_from_ready_list_(task)==OB_ENTRY_NOT_EXIST,"only failed/stopped DAGs must not produce task");
  require(s.finished.size()==2&&s.dag_list_[0].is_empty(),"both inactive DAGs must be cleaned");
  s.dag_list_[0].verify();
}
static void test_running_continuation_without_active_task(){
  ObDagPrioScheduler s;ObIDag small(1,ObDagType::DAG_TYPE_MINI_MERGE,4),big(2,ObDagType::DAG_TYPE_MINI_MERGE,4000);
  big.state=ObIDag::DAG_STATUS_NODE_RUNNING;big.running_tasks=0;s.add(small);s.add(big);expect_owner(s,2);
}
static void test_active_big_excluded_from_size_preference(){
  ObDagPrioScheduler s;ObIDag small(1,ObDagType::DAG_TYPE_MINI_MERGE,4),big(2,ObDagType::DAG_TYPE_MINI_MERGE,4000);
  big.state=ObIDag::DAG_STATUS_NODE_RUNNING;big.running_tasks=1;s.add(small);s.add(big);expect_owner(s,1);
}
static void test_other_priority_fifo(){
  ObDagPrioScheduler s;s.priority_=ObDagPrio::OTHER;ObIDag small(1,ObDagType::DAG_TYPE_MINI_MERGE,4),big(2,ObDagType::DAG_TYPE_MINI_MERGE,4000);
  s.add(small);s.add(big);expect_owner(s,1);
}
static void test_equal_size_fifo(){
  ObDagPrioScheduler s;ObIDag first(1,ObDagType::DAG_TYPE_MINI_MERGE,4),second(2,ObDagType::DAG_TYPE_MINI_MERGE,4);
  s.add(first);s.add(second);expect_owner(s,1);
}
static void test_empty(){
  ObDagPrioScheduler s;ObITask *t=nullptr;require(s.pop_task_from_ready_list_(t)==OB_ENTRY_NOT_EXIST&&t==nullptr,"empty queue");
}
static void test_fifo_progress_with_continual_large_arrivals(){
  ObDagPrioScheduler s;std::vector<std::unique_ptr<ObIDag>> dags;
  const int small_count=100;
  for(int i=0;i<small_count;i++){dags.emplace_back(new ObIDag(i,ObDagType::DAG_TYPE_MINI_MERGE,4));s.add(*dags.back());}
  int small_selected=0;
  for(int turn=0;turn<2*small_count;turn++){
    dags.emplace_back(new ObIDag(small_count+turn,ObDagType::DAG_TYPE_MINI_MERGE,(2LL<<30)+turn));s.add(*dags.back());
    auto *task=pop(s);auto &dag=*dags.at(task->owner);
    s.record_ready_task_dispatch_(dag.type);
    if(task->owner<small_count)++small_selected;
    s.dag_list_[0].erase(dag);--s.counts.count[dag.type];
    std::fill(std::begin(s.counts.scheduled),std::end(s.counts.scheduled),0);
  }
  require(small_selected==small_count,"all 100 old small tasks must finish despite 200 large arrivals");
}
static void test_single_worker_balance_and_diagnostic_resets(){
  ObDagPrioScheduler reset,unchanged;
  ObIDag rmini(1,ObDagType::DAG_TYPE_MINI_MERGE,100),rmds(2,ObDagType::DAG_TYPE_MDS_MINI_MERGE);
  ObIDag umini(1,ObDagType::DAG_TYPE_MINI_MERGE,100),umds(2,ObDagType::DAG_TYPE_MDS_MINI_MERGE);
  reset.add(rmini);reset.add(rmds);unchanged.add(umini);unchanged.add(umds);
  int selected[2]={};int same_streak=0,last=0;
  for(int i=0;i<48;i++){
    const int r=pop(reset)->owner,u=pop(unchanged)->owner;
    require(r==u,"diagnostic reset must not change fairness decisions");
    ++selected[r-1];same_streak=r==last?same_streak+1:1;last=r;
    require(same_streak<=2,"single worker must give both compaction types progress");
    auto &rdag=(r==1?rmini:rmds);auto &udag=(u==1?umini:umds);
    reset.record_ready_task_dispatch_(rdag.type);unchanged.record_ready_task_dispatch_(udag.type);
    rdag.ready=udag.ready=true;
    ++unchanged.counts.scheduled[udag.type];
    std::fill(std::begin(reset.counts.scheduled),std::end(reset.counts.scheduled),0);
  }
  require(selected[0]>=16&&selected[1]>=16,"both types must make sustained single-worker progress");
}
static void test_tx_progress_with_continual_mini_and_mds(){
  ObDagPrioScheduler s;
  ObIDag tx(0,ObDagType::DAG_TYPE_TX_TABLE_MERGE),mini(1,ObDagType::DAG_TYPE_MINI_MERGE,100),mds(2,ObDagType::DAG_TYPE_MDS_MINI_MERGE);
  s.add(tx);s.add(mini);s.add(mds);int tx_turn=-1;
  for(int i=0;i<4;i++){
    int owner=pop(s)->owner;auto &dag=owner==0?tx:(owner==1?mini:mds);
    s.record_ready_task_dispatch_(dag.type);dag.ready=true;
    std::fill(std::begin(s.counts.scheduled),std::end(s.counts.scheduled),0);
    if(owner==0){tx_turn=i;break;}
  }
  require(tx_turn>=0&&tx_turn<4,"head TX work must progress within four ready selections");
}
static void test_mixed_single_worker_keeps_selecting_large(){
  ObDagPrioScheduler s;std::vector<std::unique_ptr<ObIDag>> dags;
  for(int i=0;i<100;i++){dags.emplace_back(new ObIDag(i,ObDagType::DAG_TYPE_MINI_MERGE,4));s.add(*dags.back());}
  for(int i=100;i<108;i++){dags.emplace_back(new ObIDag(i,ObDagType::DAG_TYPE_MINI_MERGE,(2LL<<30)+i));s.add(*dags.back());}
  int large_count=0,small_count=0,mds_count=0;
  dags.emplace_back(new ObIDag(int(dags.size()),ObDagType::DAG_TYPE_MDS_MINI_MERGE));s.add(*dags.back());
  for(int i=0;i<24;i++){
    int owner=pop(s)->owner;auto &dag=*dags.at(owner);
    s.record_ready_task_dispatch_(dag.type);
    s.dag_list_[0].erase(dag);--s.counts.count[dag.type];
    if(dag.type==ObDagType::DAG_TYPE_MDS_MINI_MERGE){
      ++mds_count;dags.emplace_back(new ObIDag(int(dags.size()),ObDagType::DAG_TYPE_MDS_MINI_MERGE));s.add(*dags.back());
    }else if(dag.size>=1LL<<30){++large_count;}else{++small_count;}
    std::fill(std::begin(s.counts.scheduled),std::end(s.counts.scheduled),0);
  }
  require(large_count>=4,"mixed single-worker cycle must select second, third and fourth large MINI within 24 turns; actual="+std::to_string(large_count));
  require(small_count>=4&&mds_count>=8,"small MINI and MDS must also continue progressing");
}
static void test_pop_does_not_consume_dispatch_state(){
  ObDagPrioScheduler s;ObIDag big(1,ObDagType::DAG_TYPE_MINI_MERGE,100);s.add(big);
  expect_owner(s,1);
  require(s.high_prio_dispatch_turn_==0&&s.prefer_large_mini_&&s.last_high_compaction_type_==ObDagType::DAG_TYPE_MAX,"selection alone must not consume successful-dispatch state");
  s.record_ready_task_dispatch_(big.type);
  require(s.high_prio_dispatch_turn_==1&&!s.prefer_large_mini_&&s.last_high_compaction_type_==big.type,"real helper must update successful dispatch state");
}
static void test_other_priority_dispatch_state_unchanged(){
  ObDagPrioScheduler s;s.priority_=ObDagPrio::OTHER;s.record_ready_task_dispatch_(ObDagType::DAG_TYPE_MINI_MERGE);
  require(s.high_prio_dispatch_turn_==0&&s.prefer_large_mini_&&s.last_high_compaction_type_==ObDagType::DAG_TYPE_MAX,"other priorities must not advance compaction state");
}
static void benchmark(){
  ObDagPrioScheduler s;std::vector<std::unique_ptr<ObIDag>> dags;
  for(int i=0;i<6000;i++){dags.emplace_back(new ObIDag(i,ObDagType::DAG_TYPE_MINI_MERGE,i==5999?2LL<<30:4096));s.add(*dags.back());}
  const int iterations=3000;const auto start=std::chrono::steady_clock::now();const auto cpu=std::clock();
  for(int i=0;i<iterations;i++){
    ObITask *task=nullptr;
    require(s.pop_task_from_ready_list_(task)==OB_SUCCESS&&task->owner==5999,"benchmark largest selection");
    dags.back()->ready=true;dags.back()->state=ObIDag::DAG_STATUS_READY;
  }
  const double elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
  const double cpu_elapsed=double(std::clock()-cpu)/CLOCKS_PER_SEC;
  std::cout<<"BENCH nodes=6000 iterations="<<iterations<<" wall_s="<<elapsed<<" cpu_s="<<cpu_elapsed<<" wall_us_per_pop="<<elapsed*1e6/iterations<<"\n";
}
