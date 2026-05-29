-- repro_continue_handler_exec_ctx_null.sql（MySQL 模式）
--
-- 独立复现：跨过程 CALL 抛异常 + caller 用 CONTINUE HANDLER 捕 +
-- handler 返回后紧跟 IF 标量比较，导致 ObPLExecCtx::exec_ctx_ 变 NULL，
-- 在 ObPLPartitionHitGuard ctor 炸。
--
-- 与 enhanced_handler / Windows SEH / personality trampoline 无关：
--   * 把 EXIT HANDLER 换 CONTINUE HANDLER 就炸；反过来不炸。
--     异常展开本身（穿过两张 .pdata、trampoline 跳转）是正确的，
--     证明：同样跨过程 CALL，用 EXIT HANDLER + SELECT 固定字符串能跑通。
--   * 把 SIGNAL 从 callee 搬到 caller 内直接抛（删掉 CALL 那一步），
--     这组合就不炸 —— 单过程内 SIGNAL + CONTINUE HANDLER + IF 没事。
--   → 真正触发点是 "跨过程异常恢复后 caller 的 exec_ctx_ 没被正确保持"。
--
-- 崩溃现场（Windows RelWithDebInfo, seekdb 1.3.0.0 @ commit 79ae572a5ef）：
--   [WIN32-TRACE] Vectored exception: code=0xC0000005
--     AccessViolation: READ at 0x0000000000002EC0
--     #00 oceanbase::sql::ObPLPartitionHitGuard::ObPLPartitionHitGuard+0x32
--     #01 oceanbase::sql::ObSPIService::spi_inner_execute+0x24A
--     #02 oceanbase::sql::ObSPIService::spi_query+0x2F0
--     #03 oceanbase::sql::ObSPIService::spi_query_into_expr_idx+0x1AC
--     #04 oceanbase::pl::ObPLSPIWrapper<...>
--
-- 成因定位：
--   地址 0x2EC0 = NULL + offsetof(ObExecContext, pl_stack_ctx_)
--   → ObPLPartitionHitGuard ctor 第一行访问
--     `pl_exec_ctx_.exec_ctx_->get_pl_stack_ctx()`，
--     此时 exec_ctx_ = NULL。
--   从 callee 异常展开回到 caller 的 CONTINUE HANDLER，handler 跑完继续
--   执行 caller 后续语句时，caller 的 ObPLExecCtx.exec_ctx_ 指针丢了
--   （没被恢复到调用 callee 之前的值）。
--
-- 触发最小四要素（缺一不炸）：
--   (1) 两个 PROCEDURE：callee 和 caller
--   (2) callee 里 SIGNAL SQLEXCEPTION（RAISE_APPLICATION_ERROR 等效）
--   (3) caller 用 CONTINUE HANDLER（不是 EXIT HANDLER）捕
--   (4) CALL callee 之后，caller 还有要走 SPI 标量评估的语句
--       （IF v = const THEN / SET v2 = v+1 / SELECT CONCAT(v, ...)）
--
-- 绕开写法（三种任一生效）：
--   A) caller 改用 EXIT HANDLER，handler 里直接 SELECT 固定串不读变量
--   B) caller 不做后续 IF / 表达式比较，handler 返回就是过程结尾
--   C) 把 callee 的异常源搬到 caller 自己的 BEGIN 块里（消灭跨过程 CALL）
--
-- ============================================================
DROP PROCEDURE IF EXISTS repro_ch_callee;
DROP PROCEDURE IF EXISTS repro_ch_caller;

DELIMITER //

-- 要素 (1)(2)：独立 callee，SIGNAL SQLEXCEPTION
CREATE PROCEDURE repro_ch_callee()
BEGIN
  SIGNAL SQLSTATE '45001' SET MESSAGE_TEXT = 'boom from callee';
END //

-- 要素 (1)(3)(4)：caller 用 CONTINUE HANDLER + 跨过程 CALL + 后续 IF
CREATE PROCEDURE repro_ch_caller()
BEGIN
  DECLARE v_state VARCHAR(8) DEFAULT '00000';

  -- 关键点 1：CONTINUE HANDLER。若改成 EXIT HANDLER 则不炸。
  DECLARE CONTINUE HANDLER FOR SQLEXCEPTION
  BEGIN
    GET DIAGNOSTICS CONDITION 1 v_state = RETURNED_SQLSTATE;
  END;

  -- 关键点 2：跨过程 CALL 抛异常。若把 SIGNAL 搬到这里直接抛则不炸。
  CALL repro_ch_callee();

  -- 关键点 3：handler 返回后的 SPI 标量评估。若删掉这段就不炸。
  IF v_state = '45001' THEN
    SELECT 'repro: ok (should not reach this line if bug triggers)' AS msg;
  ELSE
    SELECT CONCAT('repro: state=', v_state) AS msg;
  END IF;
END //

DELIMITER ;

-- 以下 CALL 会崩 observer（READ 0x2EC0）
CALL repro_ch_caller();

DROP PROCEDURE repro_ch_caller;
DROP PROCEDURE repro_ch_callee;
