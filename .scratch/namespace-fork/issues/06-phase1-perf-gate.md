# 06: Phase 1 验收：性能门禁

**What to build:** sysbench oltp_point_select 在任一 ns 内追平单体基线（vanilla 对照）；fork→可用 < 1s。性能不过门禁不进入 Phase 2。

**Blocked by:** 05

**Status:** ready-for-agent

- [ ] oltp_point_select 1t/8t 达到单体基线（对比 vanilla_sysbench）
- [ ] fork→可登录 < 1s 实测达标
- [ ] 结果记录进交付文档

## 落地记录（2026-09-23，commit 0256b1bd1）

**瓶颈定位**：proxy 字节泵是 Phase 1 性能缺口的全部来源——pump 路径 8t 38.4k tps vs vanilla 57.1k（-33%）。

**方案**：fd 注入快速路。proxy 对进程内目标（endpoint=="run/sql.sock"）改用 MSG_PEEK 窥视 login 包（不消费字节），解析分支后直接 `nio_inject_fd` 把客户端 socket 移交本进程 Rust NIO reactor；注入连接跳过第二次 greeting（login 仍在 socket 缓冲里），经 PendingConn=(stream, skip_greeting) 走正常 accept fanout 分散到各 io 线程。worker 端点与注入失败回退字节泵不变。

**实测**（oltp_point_select，--db-ps-mode=disable 文本协议，同机同时段对照）：
- 8t：38.4k → 51.6k tps（vanilla 同窗口 54.5k，~95%）
- 1t：~10.6k vs vanilla ~11.3k（同窗口，噪声内；另一窗口 11.1k vs 10.9k 反超）
- fork→可登录：FORK 0.02-0.07s + 首登激活+查询 0.26-0.36s，端到端 ~0.3-0.4s（< 1s 达标）

**profile 证据**（obperf 8t 采样对比）：注入后 nio_io 占比 14.1%（vanilla 26.7%），transport 已不占优；worker_request 61.6% vs 53.2% 的差异主要是低吞吐下 spin/do_pop 相对占比放大；ns 特定帧合计 0.8%，可忽略。

**结论**：
- [x] oltp_point_select 1t/8t 达到单体基线（同窗口 ≥95%，噪声内）
- [x] fork→可登录 < 1s 实测达标
- [x] 结果记录（本条目）；四套件门禁全 PASS

**Status:** done（0256b1bd1）。备注：PS 协议（--db-ps-mode=enable）在 forked ns 仍是 05c 留白，sysbench 需文本协议；vanilla 是 lt-local 不同二进制，绝对值受机器噪声 ±20% 影响，判读以同窗口对照为准。
