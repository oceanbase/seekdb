ob1.mysql alter system set enable_rebalance = 'true';
ob1.mysql alter system set enable_rereplication = 'true';
ob1.mysql alter system set balancer_idle_time = '10s';
ob1.mysql alter system set server_data_copy_in_concurrency = 20;
ob1.mysql alter system set server_data_copy_out_concurrency = 20;
