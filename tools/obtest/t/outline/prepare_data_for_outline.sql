drop database if exists CHARGE00;
create database CHARGE00;
USE CHARGE00;

CREATE TABLE `GP_REFUND_INFO_00` (
    `REFUND_INFO_NO` VARCHAR(64) NOT NULL,
    `TX_ID` VARCHAR(128) NOT NULL,
    `SERVICE_TARGET` VARCHAR(64) NOT NULL,
    `REFUND_ID` VARCHAR(128) NOT NULL,
    `SERVICE_TYPE` VARCHAR(8) NOT NULL,
    `REFUND_SERVICE_AMOUNT` DECIMAL(20,4),
    `REFUND_AMOUNT` DECIMAL(15,4),
    `REFUND_ACCOUNT` VARCHAR(20),
    `EXTRA_INFO` VARCHAR(100),
    `REAL_AMOUNT` DECIMAL(15,4),
    `MEMO` VARCHAR(100),
    `GMT_CREATE` TIMESTAMP (6) NOT NULL,
    `GMT_MODIFIED` TIMESTAMP (6) NOT NULL,
    `TRANS_LOG_ID` DECIMAL(15,0),
    `TRANS_DATE` TIMESTAMP (6),
    `BILL_NO` VARCHAR(64),
    UNIQUE KEY UK_BILL_NO(BILL_NO),
    KEY IDX_TXID_SERVICE_TARGET_TYPE (TX_ID, SERVICE_TARGET, SERVICE_TYPE),
    PRIMARY KEY (`REFUND_INFO_NO`)
);
eval create outline charge_ol_1 on delete from gp_refund_info_00 where (tx_id = '0000000100');
eval create outline charge_ol_1_1 on delete  /*+FULL(gp_refund_info_00)*/from gp_refund_info_00 where (tx_id = '0000000100');
eval create outline charge_ol_1_2 on delete   /*+INDEX(gp_refund_info_00 UK_BILL_NO)*/from gp_refund_info_00 where (tx_id = '0000000100');
eval create outline charge_ol_1_3 on delete    /*+INDEX(gp_refund_info_00 IDX_TXID_SERVICE_TARGET_TYPE)*/from gp_refund_info_00 where (tx_id = '0000000100');
