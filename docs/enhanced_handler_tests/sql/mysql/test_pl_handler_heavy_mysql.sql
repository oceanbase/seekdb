-- test_pl_handler_heavy_mysql.sql
-- Heavier MySQL-mode PL handler stress samples.
--
-- Expected on a fixed MySQL-compatible PL handler implementation:
--   every case prints "...: ok ..." and final summary has fail=0.
--
-- Notes:
--   * This file intentionally contains heavier cases than the basic semantic
--     coverage file.
--   * case02 and case06 catch exceptions raised through CALL with a caller
--     CONTINUE handler, then continue executing scalar expressions. Older
--     builds with the exec_ctx restore bug may crash there; that is the point
--     of these stress cases.

CREATE DATABASE IF NOT EXISTS test;
USE test;

DROP PROCEDURE IF EXISTS plh_heavy_run;
DROP PROCEDURE IF EXISTS plh_heavy_case01_loop;
DROP PROCEDURE IF EXISTS plh_heavy_case02_chain;
DROP PROCEDURE IF EXISTS plh_heavy_case03_cursor;
DROP PROCEDURE IF EXISTS plh_heavy_case04_diag_dml;
DROP PROCEDURE IF EXISTS plh_heavy_case05_many_blocks;
DROP PROCEDURE IF EXISTS plh_heavy_case06_cross_proc_burst;
DROP PROCEDURE IF EXISTS plh_heavy_throw_kind;
DROP PROCEDURE IF EXISTS plh_heavy_chain_1;
DROP PROCEDURE IF EXISTS plh_heavy_chain_2;
DROP PROCEDURE IF EXISTS plh_heavy_chain_3;
DROP PROCEDURE IF EXISTS plh_heavy_chain_4;
DROP PROCEDURE IF EXISTS plh_heavy_chain_5;

DROP TABLE IF EXISTS plh_heavy_log;
DROP TABLE IF EXISTS plh_heavy_nums;

CREATE TABLE plh_heavy_nums (
  n INT NOT NULL PRIMARY KEY
);

INSERT INTO plh_heavy_nums(n) VALUES
  (1),  (2),  (3),  (4),  (5),  (6),  (7),  (8),  (9),  (10),
  (11), (12), (13), (14), (15), (16), (17), (18), (19), (20),
  (21), (22), (23), (24), (25), (26), (27), (28), (29), (30),
  (31), (32), (33), (34), (35), (36), (37), (38), (39), (40),
  (41), (42), (43), (44), (45), (46), (47), (48), (49), (50),
  (51), (52), (53), (54), (55), (56), (57), (58), (59), (60);

CREATE TABLE plh_heavy_log (
  case_name VARCHAR(64) NOT NULL,
  state_code CHAR(5) NOT NULL,
  errno INT NOT NULL,
  msg_text VARCHAR(128) NOT NULL
);

DELIMITER //

CREATE PROCEDURE plh_heavy_chain_5()
BEGIN
  SIGNAL SQLSTATE '02000'
    SET MESSAGE_TEXT = 'chain-l5';
END //

CREATE PROCEDURE plh_heavy_chain_4()
BEGIN
  DECLARE EXIT HANDLER FOR NOT FOUND
    RESIGNAL SQLSTATE '45054'
      SET MESSAGE_TEXT = 'chain-l4', MYSQL_ERRNO = 20504;

  CALL plh_heavy_chain_5();
END //

CREATE PROCEDURE plh_heavy_chain_3()
BEGIN
  DECLARE EXIT HANDLER FOR SQLSTATE '45054'
    RESIGNAL SQLSTATE '22012'
      SET MESSAGE_TEXT = 'chain-l3', MYSQL_ERRNO = 20503;

  CALL plh_heavy_chain_4();
END //

CREATE PROCEDURE plh_heavy_chain_2()
BEGIN
  DECLARE EXIT HANDLER FOR SQLSTATE '22012'
    RESIGNAL SQLSTATE '45052'
      SET MESSAGE_TEXT = 'chain-l2', MYSQL_ERRNO = 20502;

  CALL plh_heavy_chain_3();
END //

CREATE PROCEDURE plh_heavy_chain_1()
BEGIN
  DECLARE EXIT HANDLER FOR SQLSTATE '45052'
    RESIGNAL SQLSTATE '45051'
      SET MESSAGE_TEXT = 'chain-l1', MYSQL_ERRNO = 20501;

  CALL plh_heavy_chain_2();
END //

CREATE PROCEDURE plh_heavy_throw_kind(IN p_kind INT)
BEGIN
  IF p_kind = 0 THEN
    SIGNAL SQLSTATE '02000'
      SET MESSAGE_TEXT = 'throw-kind-not-found';
  ELSEIF p_kind = 1 THEN
    SIGNAL SQLSTATE '22012'
      SET MESSAGE_TEXT = 'throw-kind-zdiv';
  ELSE
    SIGNAL SQLSTATE '45606'
      SET MESSAGE_TEXT = 'throw-kind-app', MYSQL_ERRNO = 20606;
  END IF;
END //

-- case01: 1200 mixed exception conditions in one procedure.
CREATE PROCEDURE plh_heavy_case01_loop(OUT p_ok INT, OUT p_msg VARCHAR(256))
BEGIN
  DECLARE v_i INT DEFAULT 1;
  DECLARE v_nf INT DEFAULT 0;
  DECLARE v_zd INT DEFAULT 0;
  DECLARE v_errno INT DEFAULT 0;
  DECLARE v_generic INT DEFAULT 0;
  DECLARE v_state CHAR(5) DEFAULT '00000';
  DECLARE v_sql_errno INT DEFAULT 0;

  DECLARE CONTINUE HANDLER FOR NOT FOUND
  BEGIN
    SET v_nf = v_nf + 1;
    GET DIAGNOSTICS CONDITION 1
      v_state = RETURNED_SQLSTATE,
      v_sql_errno = MYSQL_ERRNO;
  END;

  DECLARE CONTINUE HANDLER FOR SQLSTATE '22012'
  BEGIN
    SET v_zd = v_zd + 1;
    GET DIAGNOSTICS CONDITION 1
      v_state = RETURNED_SQLSTATE,
      v_sql_errno = MYSQL_ERRNO;
  END;

  DECLARE CONTINUE HANDLER FOR 20401
  BEGIN
    SET v_errno = v_errno + 1;
    GET DIAGNOSTICS CONDITION 1
      v_state = RETURNED_SQLSTATE,
      v_sql_errno = MYSQL_ERRNO;
  END;

  DECLARE CONTINUE HANDLER FOR SQLEXCEPTION
  BEGIN
    SET v_generic = v_generic + 1;
    GET DIAGNOSTICS CONDITION 1
      v_state = RETURNED_SQLSTATE,
      v_sql_errno = MYSQL_ERRNO;
  END;

  WHILE v_i <= 1200 DO
    IF MOD(v_i, 4) = 0 THEN
      SIGNAL SQLSTATE '02000'
        SET MESSAGE_TEXT = 'case01 not found';
    ELSEIF MOD(v_i, 4) = 1 THEN
      SIGNAL SQLSTATE '22012'
        SET MESSAGE_TEXT = 'case01 exact data exception';
    ELSEIF MOD(v_i, 4) = 2 THEN
      SIGNAL SQLSTATE '45401'
        SET MESSAGE_TEXT = 'case01 errno exception', MYSQL_ERRNO = 20401;
    ELSE
      SIGNAL SQLSTATE '45402'
        SET MESSAGE_TEXT = 'case01 generic exception', MYSQL_ERRNO = 20402;
    END IF;

    SET v_i = v_i + 1;
  END WHILE;

  IF v_nf = 300
     AND v_zd = 300
     AND v_errno = 300
     AND v_generic = 300
     AND v_i = 1201 THEN
    SET p_ok = 1;
    SET p_msg = CONCAT(
      'case01_mixed_loop_1200: ok nf=',
      v_nf,
      ', zd=',
      v_zd,
      ', errno=',
      v_errno,
      ', generic=',
      v_generic
    );
  ELSE
    SET p_ok = 0;
    SET p_msg = CONCAT(
      'case01_mixed_loop_1200: wrong nf=',
      v_nf,
      ', zd=',
      v_zd,
      ', errno=',
      v_errno,
      ', generic=',
      v_generic,
      ', i=',
      v_i,
      ', last=',
      v_state,
      '/',
      v_sql_errno
    );
  END IF;
END //

-- case02: five-procedure chain, each level catches and retags.
CREATE PROCEDURE plh_heavy_case02_chain(OUT p_ok INT, OUT p_msg VARCHAR(256))
BEGIN
  DECLARE v_hit INT DEFAULT 0;
  DECLARE v_state CHAR(5) DEFAULT '00000';
  DECLARE v_errno INT DEFAULT 0;
  DECLARE v_text VARCHAR(128) DEFAULT '';

  DECLARE CONTINUE HANDLER FOR SQLSTATE '45051'
  BEGIN
    SET v_hit = v_hit + 1;
    GET DIAGNOSTICS CONDITION 1
      v_state = RETURNED_SQLSTATE,
      v_errno = MYSQL_ERRNO,
      v_text = MESSAGE_TEXT;
  END;

  CALL plh_heavy_chain_1();

  IF v_hit = 1
     AND v_state = '45051'
     AND v_errno = 20501
     AND v_text = 'chain-l1' THEN
    SET p_ok = 1;
    SET p_msg = 'case02_chain_resignal_5_levels: ok';
  ELSE
    SET p_ok = 0;
    SET p_msg = CONCAT(
      'case02_chain_resignal_5_levels: wrong hit=',
      v_hit,
      ', state=',
      v_state,
      ', errno=',
      v_errno,
      ', text=',
      v_text
    );
  END IF;
END //


-- case04: handler reads diagnostics and does DML 120 times.
CREATE PROCEDURE plh_heavy_case04_diag_dml(OUT p_ok INT, OUT p_msg VARCHAR(256))
BEGIN
  DECLARE v_i INT DEFAULT 1;
  DECLARE v_rows INT DEFAULT 0;
  DECLARE v_state CHAR(5) DEFAULT '00000';
  DECLARE v_errno INT DEFAULT 0;
  DECLARE v_text VARCHAR(128) DEFAULT '';

  DECLARE CONTINUE HANDLER FOR SQLEXCEPTION
  BEGIN
    GET DIAGNOSTICS CONDITION 1
      v_state = RETURNED_SQLSTATE,
      v_errno = MYSQL_ERRNO,
      v_text = MESSAGE_TEXT;
    INSERT INTO plh_heavy_log(case_name, state_code, errno, msg_text)
      VALUES ('case04_diag_dml', v_state, v_errno, v_text);
  END;

  DELETE FROM plh_heavy_log WHERE case_name = 'case04_diag_dml';

  WHILE v_i <= 120 DO
    SIGNAL SQLSTATE '45404'
      SET MESSAGE_TEXT = 'case04 diag dml', MYSQL_ERRNO = 20404;
    SET v_i = v_i + 1;
  END WHILE;

  SELECT COUNT(*)
    INTO v_rows
    FROM plh_heavy_log
   WHERE case_name = 'case04_diag_dml'
     AND state_code = '45404'
     AND errno = 20404
     AND msg_text = 'case04 diag dml';

  IF v_rows = 120 THEN
    SET p_ok = 1;
    SET p_msg = CONCAT('case04_diagnostics_and_dml_in_handler: ok rows=', v_rows);
  ELSE
    SET p_ok = 0;
    SET p_msg = CONCAT(
      'case04_diagnostics_and_dml_in_handler: wrong rows=',
      v_rows,
      ', i=',
      v_i,
      ', last=',
      v_state,
      '/',
      v_errno
    );
  END IF;
END //

-- case05: many distinct handler blocks in one procedure.
CREATE PROCEDURE plh_heavy_case05_many_blocks(OUT p_ok INT, OUT p_msg VARCHAR(256))
BEGIN
  DECLARE v01 INT DEFAULT 1;
  DECLARE v02 INT DEFAULT 2;
  DECLARE v03 INT DEFAULT 3;
  DECLARE v04 INT DEFAULT 4;
  DECLARE v05 INT DEFAULT 5;
  DECLARE v06 INT DEFAULT 6;
  DECLARE v07 INT DEFAULT 7;
  DECLARE v08 INT DEFAULT 8;
  DECLARE v09 INT DEFAULT 9;
  DECLARE v10 INT DEFAULT 10;
  DECLARE v11 INT DEFAULT 11;
  DECLARE v12 INT DEFAULT 12;
  DECLARE v13 INT DEFAULT 13;
  DECLARE v14 INT DEFAULT 14;
  DECLARE v15 INT DEFAULT 15;
  DECLARE v16 INT DEFAULT 16;
  DECLARE v_hit INT DEFAULT 0;
  DECLARE v_probe INT DEFAULT 0;

  BEGIN
    DECLARE CONTINUE HANDLER FOR SQLSTATE '45501' SET v_hit = v_hit + 1;
    SIGNAL SQLSTATE '45501' SET MESSAGE_TEXT = 'many-blocks-01';
  END;

  BEGIN
    DECLARE CONTINUE HANDLER FOR SQLSTATE '45502' SET v_hit = v_hit + 2;
    SIGNAL SQLSTATE '45502' SET MESSAGE_TEXT = 'many-blocks-02';
  END;

  BEGIN
    DECLARE CONTINUE HANDLER FOR SQLSTATE '45503' SET v_hit = v_hit + 3;
    SIGNAL SQLSTATE '45503' SET MESSAGE_TEXT = 'many-blocks-03';
  END;

  BEGIN
    DECLARE CONTINUE HANDLER FOR SQLSTATE '45504' SET v_hit = v_hit + 4;
    SIGNAL SQLSTATE '45504' SET MESSAGE_TEXT = 'many-blocks-04';
  END;

  BEGIN
    DECLARE CONTINUE HANDLER FOR SQLSTATE '45505' SET v_hit = v_hit + 5;
    SIGNAL SQLSTATE '45505' SET MESSAGE_TEXT = 'many-blocks-05';
  END;

  BEGIN
    DECLARE CONTINUE HANDLER FOR SQLSTATE '45506' SET v_hit = v_hit + 6;
    SIGNAL SQLSTATE '45506' SET MESSAGE_TEXT = 'many-blocks-06';
  END;

  BEGIN
    DECLARE CONTINUE HANDLER FOR SQLSTATE '45507' SET v_hit = v_hit + 7;
    SIGNAL SQLSTATE '45507' SET MESSAGE_TEXT = 'many-blocks-07';
  END;

  BEGIN
    DECLARE CONTINUE HANDLER FOR SQLSTATE '45508' SET v_hit = v_hit + 8;
    SIGNAL SQLSTATE '45508' SET MESSAGE_TEXT = 'many-blocks-08';
  END;

  BEGIN
    DECLARE CONTINUE HANDLER FOR SQLSTATE '45509' SET v_hit = v_hit + 9;
    SIGNAL SQLSTATE '45509' SET MESSAGE_TEXT = 'many-blocks-09';
  END;

  BEGIN
    DECLARE CONTINUE HANDLER FOR SQLSTATE '45510' SET v_hit = v_hit + 10;
    SIGNAL SQLSTATE '45510' SET MESSAGE_TEXT = 'many-blocks-10';
  END;

  BEGIN
    DECLARE CONTINUE HANDLER FOR SQLSTATE '45511' SET v_hit = v_hit + 11;
    SIGNAL SQLSTATE '45511' SET MESSAGE_TEXT = 'many-blocks-11';
  END;

  BEGIN
    DECLARE CONTINUE HANDLER FOR SQLSTATE '45512' SET v_hit = v_hit + 12;
    SIGNAL SQLSTATE '45512' SET MESSAGE_TEXT = 'many-blocks-12';
  END;

  BEGIN
    DECLARE CONTINUE HANDLER FOR SQLSTATE '45513' SET v_hit = v_hit + 13;
    SIGNAL SQLSTATE '45513' SET MESSAGE_TEXT = 'many-blocks-13';
  END;

  BEGIN
    DECLARE CONTINUE HANDLER FOR SQLSTATE '45514' SET v_hit = v_hit + 14;
    SIGNAL SQLSTATE '45514' SET MESSAGE_TEXT = 'many-blocks-14';
  END;

  BEGIN
    DECLARE CONTINUE HANDLER FOR SQLSTATE '45515' SET v_hit = v_hit + 15;
    SIGNAL SQLSTATE '45515' SET MESSAGE_TEXT = 'many-blocks-15';
  END;

  BEGIN
    DECLARE CONTINUE HANDLER FOR SQLSTATE '45516' SET v_hit = v_hit + 16;
    SIGNAL SQLSTATE '45516' SET MESSAGE_TEXT = 'many-blocks-16';
  END;

  BEGIN
    DECLARE CONTINUE HANDLER FOR SQLSTATE '45517' SET v_hit = v_hit + 17;
    SIGNAL SQLSTATE '45517' SET MESSAGE_TEXT = 'many-blocks-17';
  END;

  BEGIN
    DECLARE CONTINUE HANDLER FOR SQLSTATE '45518' SET v_hit = v_hit + 18;
    SIGNAL SQLSTATE '45518' SET MESSAGE_TEXT = 'many-blocks-18';
  END;

  BEGIN
    DECLARE CONTINUE HANDLER FOR SQLSTATE '45519' SET v_hit = v_hit + 19;
    SIGNAL SQLSTATE '45519' SET MESSAGE_TEXT = 'many-blocks-19';
  END;

  BEGIN
    DECLARE CONTINUE HANDLER FOR SQLSTATE '45520' SET v_hit = v_hit + 20;
    SIGNAL SQLSTATE '45520' SET MESSAGE_TEXT = 'many-blocks-20';
  END;

  SET v_probe = v01 + v02 + v03 + v04 + v05 + v06 + v07 + v08
              + v09 + v10 + v11 + v12 + v13 + v14 + v15 + v16;

  IF v_hit = 210 AND v_probe = 136 THEN
    SET p_ok = 1;
    SET p_msg = CONCAT('case05_many_handler_blocks: ok hit=', v_hit, ', probe=', v_probe);
  ELSE
    SET p_ok = 0;
    SET p_msg = CONCAT('case05_many_handler_blocks: wrong hit=', v_hit, ', probe=', v_probe);
  END IF;
END //

-- case06: repeated callee exceptions caught by caller CONTINUE handlers.
CREATE PROCEDURE plh_heavy_case06_cross_proc_burst(OUT p_ok INT, OUT p_msg VARCHAR(256))
BEGIN
  DECLARE v_i INT DEFAULT 1;
  DECLARE v_nf INT DEFAULT 0;
  DECLARE v_zd INT DEFAULT 0;
  DECLARE v_app INT DEFAULT 0;
  DECLARE v_state CHAR(5) DEFAULT '00000';
  DECLARE v_errno INT DEFAULT 0;

  DECLARE CONTINUE HANDLER FOR NOT FOUND
  BEGIN
    SET v_nf = v_nf + 1;
    GET DIAGNOSTICS CONDITION 1
      v_state = RETURNED_SQLSTATE,
      v_errno = MYSQL_ERRNO;
  END;

  DECLARE CONTINUE HANDLER FOR SQLSTATE '22012'
  BEGIN
    SET v_zd = v_zd + 1;
    GET DIAGNOSTICS CONDITION 1
      v_state = RETURNED_SQLSTATE,
      v_errno = MYSQL_ERRNO;
  END;

  DECLARE CONTINUE HANDLER FOR SQLEXCEPTION
  BEGIN
    SET v_app = v_app + 1;
    GET DIAGNOSTICS CONDITION 1
      v_state = RETURNED_SQLSTATE,
      v_errno = MYSQL_ERRNO;
  END;

  WHILE v_i <= 600 DO
    CALL plh_heavy_throw_kind(MOD(v_i, 3));
    SET v_i = v_i + 1;
  END WHILE;

  IF v_nf = 200 AND v_zd = 200 AND v_app = 200 AND v_i = 601 THEN
    SET p_ok = 1;
    SET p_msg = CONCAT(
      'case06_cross_proc_continue_burst: ok nf=',
      v_nf,
      ', zd=',
      v_zd,
      ', app=',
      v_app
    );
  ELSE
    SET p_ok = 0;
    SET p_msg = CONCAT(
      'case06_cross_proc_continue_burst: wrong nf=',
      v_nf,
      ', zd=',
      v_zd,
      ', app=',
      v_app,
      ', i=',
      v_i,
      ', last=',
      v_state,
      '/',
      v_errno
    );
  END IF;
END //

CREATE PROCEDURE plh_heavy_run()
BEGIN
  DECLARE v_ok INT DEFAULT 0;
  DECLARE v_pass INT DEFAULT 0;
  DECLARE v_fail INT DEFAULT 0;
  DECLARE v_msg VARCHAR(256) DEFAULT '';

  CALL plh_heavy_case01_loop(v_ok, v_msg);
  SELECT v_msg AS msg;
  IF v_ok = 1 THEN
    SET v_pass = v_pass + 1;
  ELSE
    SET v_fail = v_fail + 1;
  END IF;

  CALL plh_heavy_case02_chain(v_ok, v_msg);
  SELECT v_msg AS msg;
  IF v_ok = 1 THEN
    SET v_pass = v_pass + 1;
  ELSE
    SET v_fail = v_fail + 1;
  END IF;

  CALL plh_heavy_case03_cursor(v_ok, v_msg);
  SELECT v_msg AS msg;
  IF v_ok = 1 THEN
    SET v_pass = v_pass + 1;
  ELSE
    SET v_fail = v_fail + 1;
  END IF;

  CALL plh_heavy_case04_diag_dml(v_ok, v_msg);
  SELECT v_msg AS msg;
  IF v_ok = 1 THEN
    SET v_pass = v_pass + 1;
  ELSE
    SET v_fail = v_fail + 1;
  END IF;

  CALL plh_heavy_case05_many_blocks(v_ok, v_msg);
  SELECT v_msg AS msg;
  IF v_ok = 1 THEN
    SET v_pass = v_pass + 1;
  ELSE
    SET v_fail = v_fail + 1;
  END IF;

  CALL plh_heavy_case06_cross_proc_burst(v_ok, v_msg);
  SELECT v_msg AS msg;
  IF v_ok = 1 THEN
    SET v_pass = v_pass + 1;
  ELSE
    SET v_fail = v_fail + 1;
  END IF;

  SELECT CONCAT('plh_heavy summary: pass=', v_pass, ', fail=', v_fail) AS msg;
END //

DELIMITER ;

CALL plh_heavy_run();

DROP PROCEDURE plh_heavy_run;
DROP PROCEDURE plh_heavy_case06_cross_proc_burst;
DROP PROCEDURE plh_heavy_case05_many_blocks;
DROP PROCEDURE plh_heavy_case04_diag_dml;
DROP PROCEDURE plh_heavy_case03_cursor;
DROP PROCEDURE plh_heavy_case02_chain;
DROP PROCEDURE plh_heavy_case01_loop;
DROP PROCEDURE plh_heavy_throw_kind;
DROP PROCEDURE plh_heavy_chain_1;
DROP PROCEDURE plh_heavy_chain_2;
DROP PROCEDURE plh_heavy_chain_3;
DROP PROCEDURE plh_heavy_chain_4;
DROP PROCEDURE plh_heavy_chain_5;
DROP TABLE plh_heavy_log;
DROP TABLE plh_heavy_nums;
