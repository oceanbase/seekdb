alter system set balancer_idle_time='10s' ;
alter system set server_data_copy_in_concurrency=30;
alter system set server_data_copy_out_concurrency=50;
alter system set server_temporary_offline_time ='15s';
alter system set data_copy_concurrency=600;


