system sleep 5;
create user if not exists 'admin' IDENTIFIED BY 'admin';
use oceanbase;
create database if not exists test;

use test;
grant all on *.* to 'admin' WITH GRANT OPTION;



alter system set _use_odps_jni_connector = false;
set @@session.ob_query_timeout = 200000000;

set @mysqltest_mode = 'both';

delimiter /
drop procedure if exists exec_sql;/
create procedure exec_sql(v varchar(4000))
begin
  declare continue handler for sqlexception
  begin
    GET DIAGNOSTICS CONDITION 1 @p1 = RETURNED_SQLSTATE, @p2 = MESSAGE_TEXT;
    select v;
  end;
  set @sql_text = v;
  prepare stmt from @sql_text;
  execute stmt;
  deallocate prepare stmt;
end;
/

delimiter ;

/****************************** ATTENTION ******************************/
/* The tenant=all will be deprecated. If you want all tenants to be    */
/* modified, use tenant=sys & tenant=all_user & tenant=all_meta.       */
/***********************************************************************/
set @@session.ob_query_timeout = 10000000;
system sleep 5;
set global recyclebin = 'on';
set global ob_enable_truncate_flashback = 'on';
set global _nlj_batching_enabled = true;
alter system set ob_compaction_schedule_interval = '10s' tenant sys;
alter system set ob_compaction_schedule_interval = '10s' tenant all_user;
alter system set ob_compaction_schedule_interval = '10s' tenant all_meta;
alter system set merger_check_interval = '10s' tenant sys;
alter system set merger_check_interval = '10s' tenant all_user;
alter system set merger_check_interval = '10s' tenant all_meta;
alter system set _ss_major_compaction_prewarm_level = 2 tenant sys;
alter system set _ss_major_compaction_prewarm_level = 2 tenant all_user;
alter system set _ss_major_compaction_prewarm_level = 2 tenant all_meta;
alter system set enable_sql_extension=true tenant sys;
alter system set enable_sql_extension=true tenant all_user;
alter system set enable_sql_extension=true tenant all_meta;
alter system set _enable_adaptive_compaction = false tenant sys;
alter system set _enable_adaptive_compaction = false tenant all_user;
alter system set _enable_adaptive_compaction = false tenant all_meta;
-- alter system set_tp in tools/deploy/set_tp.sql
alter system set_tp tp_no = 509, error_code = 4016, frequency = 1;
alter system set_tp tp_no = 368, error_code = 4016, frequency = 1;
alter system set_tp tp_no = 1200, error_code = 4001, frequency = 1;
alter system set_tp tp_no = 551, error_code = 5434, frequency = 1;
alter system set_tp tp_no = 311, error_code = 4, frequency = 1;
alter system set_tp tp_no = 555, error_code = 4016, frequency = 1;
alter system set_tp tp_no = 558, error_code = 4016, frequency = 1;
alter system set_tp tp_no = 561, error_code = 4016, frequency = 1;
alter system set_tp tp_no = 565, error_code = 4007, frequency = 1;
alter system set_tp tp_no = 408, error_code = 4016, frequency = 1;
alter system set_tp tp_name = ERRSIM_FAST_NLJ_RANGE_GENERATOR_CHECK, error_code = 4016, frequency = 1;
alter system set_tp tp_name = EN_THROW_DS_ERROR, error_code = 4016, frequency = 1;
alter system set _enable_var_assign_use_das = true tenant = sys;
alter system set _enable_var_assign_use_das = true tenant = all_user;
alter system set _enable_var_assign_use_das = true tenant = all_meta;
alter system set _enable_spf_batch_rescan = true tenant = sys;
alter system set _enable_spf_batch_rescan = true tenant = all_user;
alter system set _enable_spf_batch_rescan = true tenant = all_meta;
-- alter system set _use_hash_rollup = "forced" tenant = mysql;
-- alter system set _use_hash_rollup = "forced" tenant = oracle;
-- alter tenant oracle set variables ob_plan_cache_percentage = 20;
-- alter tenant mysql set variables ob_plan_cache_percentage = 20;
alter system set _max_px_workers_per_cpu = 10 tenant = all_user;
alter system set _force_enable_plan_tracing = false tenant sys;
alter system set _force_enable_plan_tracing = false tenant all_user;
alter system set _force_enable_plan_tracing = false tenant all_meta;

delimiter /
drop procedure if exists exec_sql;/
create procedure exec_sql(v varchar(4000))
begin
  declare continue handler for sqlexception
  begin
    GET DIAGNOSTICS CONDITION 1 @p1 = RETURNED_SQLSTATE, @p2 = MESSAGE_TEXT;
    select v;
  end;
  set @sql_text = v;
  prepare stmt from @sql_text;
  execute stmt;
  deallocate prepare stmt;
end;
/

drop procedure if exists check_prepare;/
create procedure check_prepare()
begin
  call exec_sql('alter system set leak_mod_to_check=''''');
  call exec_sql('alter system set leak_mod_to_check=''PlTemp'';');
end;
/
drop procedure if exists check_memory;/
create procedure check_memory()
begin
    declare result int default 0;
    declare result_tmp int default 0;
    begin
    retry:
    while true do
      select sum(alloc_size) from __all_virtual_mem_leak_checker_info into result_tmp;
      if result_tmp is not null and result_tmp != 0 and result_tmp != result then
        set result = result_tmp;
        iterate retry;
      end if;
      if result != 0 and result is not null then
        select * from __all_virtual_mem_leak_checker_info;
        signal sqlstate '45000' SET MESSAGE_TEXT = 'PlTemp Memory May Leak, please check!!';
      end if;
      leave retry;
    end while;
    end;
end;
/
delimiter ;
