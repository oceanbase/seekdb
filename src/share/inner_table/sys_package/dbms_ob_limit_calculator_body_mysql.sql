#package_name:dbms_ob_limit_calculator
#author:cxf262476, yangyifei.yyf

CREATE OR replace PACKAGE BODY dbms_ob_limit_calculator
  PROCEDURE phy_res_calculate_by_logic_res_inner(
    IN  args                                VARCHAR(1024) DEFAULT '',
    OUT res                                 VARCHAR(2048));
  PRAGMA INTERFACE(C, PHY_RES_CALCULATE_BY_LOGIC_RES);

  PROCEDURE calculate_min_phy_res_needed_by_logic_res(
    IN args                                 VARCHAR(1024) DEFAULT '')
  BEGIN
    DECLARE res VARCHAR(2048);
    CALL phy_res_calculate_by_logic_res_inner(args, res);
    SELECT * FROM JSON_TABLE(res, '$[*]' COLUMNS (PHYSICAL_RESOURCE_NAME VARCHAR(64) PATH '$.physical_resource_name',
                                                  MIN_VALUE BIGINT PATH '$.min_value')) t;
  END;

END;
//
