system sleep 5;
create user if not exists 'admin' IDENTIFIED BY 'admin';
use oceanbase;
create database if not exists test;

use test;
grant all on *.* to 'admin' WITH GRANT OPTION;


set @@session.ob_query_timeout = 200000000;

set @mysqltest_mode = 'mysql';

set @@session.ob_query_timeout = 10000000;
system sleep 5;
set global recyclebin = 'on';
set global _nlj_batching_enabled = true;
alter system set ob_compaction_schedule_interval = '10s';
alter system set merger_check_interval = '10s';
alter system set _enable_adaptive_compaction = false;
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
alter system set _enable_var_assign_use_das = true;
alter system set _enable_spf_batch_rescan = true;
alter system set _max_px_workers_per_cpu = 10;

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
