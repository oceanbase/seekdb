USE CHARGE00;
let $tenant_id = query_get_value(select effective_tenant_id(), effective_tenant_id(), 1);
--echo ##### test #####
let $outline_name = charge_ol_1;
## eval create outline $outline_name on delete from gp_refund_info_00 where (tx_id = '0000000100');
delete from gp_refund_info_00 where (tx_id = '0000000100');
delete from gp_refund_info_00 where (tx_id = '0000000100');
source t/outline/get_outline_result.sql;

--echo
--echo ## test use FULL
let $outline_name = charge_ol_1_1;
## eval create outline charge_ol_1_1 on delete  /*+FULL(gp_refund_info_00)*/from gp_refund_info_00 where (tx_id = '0000000100');
delete  from gp_refund_info_00 where (tx_id = '0000000100');
delete  from gp_refund_info_00 where (tx_id = '0000000100');
source t/outline/get_outline_result.sql;

--echo
--echo ## test use UK_BILL_NO
let $outline_name = charge_ol_1_2;
## eval create outline $outline_name on delete   /*+INDEX(gp_refund_info_00 UK_BILL_NO)*/from gp_refund_info_00 where (tx_id = '0000000100');
delete   from gp_refund_info_00 where (tx_id = '0000000100');
delete   from gp_refund_info_00 where (tx_id = '0000000100');
source t/outline/get_outline_result.sql;

--echo
--echo ## test use IDX_TXID_SERVICE_TARGET_TYPE
let $outline_name = charge_ol_1_3;
## eval create outline $outline_name on delete    /*+INDEX(gp_refund_info_00 IDX_TXID_SERVICE_TARGET_TYPE)*/from gp_refund_info_00 where (tx_id = '0000000100');
delete    from gp_refund_info_00 where (tx_id = '0000000100');
delete    from gp_refund_info_00 where (tx_id = '0000000100');
source t/outline/get_outline_result.sql;
