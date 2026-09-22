# 上层关系候选：GROUP、WINDOW、DISTINCT 与 ORDERED

日期：2026-09-10。状态：上层接线、受控 kernel 与独立回归通过。
不是通用聚合/窗口替换、PX 或生产成本模型已经完成的声明。

## 阶段不是算子编号

新增四个独立 Server-dev 注册点：

| Rust UpperStage | Hook point | 调用位置 |
| --- | --- | --- |
| Group | optimizer.upper.group.paths.v1 | GROUP BY/ROLLUP 原生候选生成后 |
| Window | optimizer.upper.window.paths.v1 | 窗口函数原生候选生成后 |
| Distinct | optimizer.upper.distinct.paths.v1 | SELECT DISTINCT 原生候选生成后 |
| Ordered | optimizer.upper.ordered.paths.v1 | ORDER BY 候选生成后、后续 LIMIT 等步骤前 |

只有语句经过相应 allocate_plan_top 阶段时才调用。某个原生操作可能
已经被优化消除，或者候选根不是该操作本身；不能把阶段等同于根的
具体 C++ 类型。阶段跟随改写后的语句，而不是原始 SQL 文本：例如
没有聚合函数的 GROUP BY 可以先改写为 DISTINCT，此时进入的是
Distinct 阶段。插件不能根据原 SQL 的关键字预期回调次数。
内部使用显式 phase 枚举路由，未知值拒绝，避免多个
布尔参数的非法组合。原有 relation、JOIN 子问题和选择入口保持独立。

阶段先检查注册可用性；没有 hook 时不复制候选、不创建图、不构建
新树。这不是性能测量结果，也不是整个查询的 registry epoch 快照。
实际回调仍经过 loader 的 Server-dev 构建版本、lease、epoch 和
服务前缀校验，并由既有 Rust hook 运行时组合回调。

## 贡献与后续选择

Rust SDK 使用 Registration::upper_paths_hook(stage, definition)。服务
要求 builders 与 Around，一次 next，不能 select。候选可由前面的
hook 追加，原生与新增实现保留给后续规划。构造错误不可被回调返回
成功吞掉；整条链成功前不发布半组结果，不提交共享输入的父指针。

上层步骤继续使用宿主候选管理和成本选择；插件注册不等于强制采用
它的算法。重新初始化候选时同步 plain-plan 的成本/索引，并保留
当前 is_final_sort 状态，不能沿用追加前的最优候选缓存。

ORDERED 阶段已经满足 SQL 排序，且可能包含 top-N。当前自定义请求
必须声明 preserves_order，否则构造失败且不追加候选；MATERIAL
继续使用原生保序实现。该声明仍需受信任插件正确实现。后续应开放
显式 required properties 与排序恢复，不通过丢失排序悄悄扩大自由度。

## Rust 参考实现与能力边界

v15 增加 upper-policy 服务及四个独立对象，合计五个服务、七个扩展。
策略检查规范化 local_serial 属性，跳过不支持的候选，保留原生路径；
没有合适输入时正常调用 next，不把串行执行器套到 PX 候选上。
它向每个阶段贡献保留原生结果及顺序的 Rust spool 候选，不在贡献
阶段选择赢家，也不伪造更低成本。这个参考实现用于证明阶段接线，
不是新的聚合、窗口、去重或排序算法。

现有 fragment builder 对可移除子图、参数拥有权与输出仍有自己的
限制。开放阶段不会自动提供 aggregate state、窗口 frame、DISTINCT
比较、rollup/grouping sets 等完整语义。下一步需要把这些信息及
属性协议接入图和 builder，再实现独立的 Rust 上层算法；不能将
一个保序 spool 当成通用上层算子替代已经完成。

## 本轮验证计划与当前证据

- SDK 注册测试检查四个点与原有三个点独立，单次事务传递准确名称。
- 独立 loader 用真实 Rust v15 服务检查四个阶段的追加、不选择、错误
  传播，以及未知阶段拒绝；不是仅调用 C++ 测试 callback。
- kernel 增加阶段路由、select 禁止、整组失败及 ORDERED 失序拒绝
  夹具；增加 GROUP BY、窗口函数、DISTINCT、ORDER BY 和 top-N SQL，
  在标量/batch=3 下经过正常优化、codegen、Rust 执行与重扫。
- SQL 夹具的后期选择会显式选择贡献的自定义根，以验证执行链；
  不能据此声称成本模型会在生产场景自然选中 spool。
- 二十一项既有插件单测通过（85460）；生产构建通过（25409），SDK
  全量回归及 27 项文档测试通过（44003），包含七个阶段的注册测试。
  首轮 kernel（71325）在新增 GROUP 阶段计数断言处失败：原 SQL
  只有 GROUP BY ordinal、没有聚合函数，符合现有 group-by 转
  DISTINCT 的改写条件；该 SQL 执行与上层协议夹具已运行到。
  将用例改为按 MOD 分组计算 MAX，并让 DISTINCT 真正合并重复值，
  增加各阶段计数及 EXPLAIN 诊断后，第二轮完整 kernel 通过（8999）：
  十组新增 SQL 在标量与 batch=3 下经过正常规划、codegen、真实
  Rust 候选执行和整体重扫；GROUP/WINDOW/DISTINCT/ORDERED 均实际
  贡献，MAX 分组、ROW_NUMBER、重复值去重、降序及 top-N 结果正确。
  既有三十四组 JOIN SQL 及其他执行矩阵保持通过，不将第一次失败轮
  或旧检查点视为本轮全量成功。
- 生产对应 v15 DSO 重建与二进制审计通过（9646），独立测试目标
  重建通过（71561）；完整 CTest 30/30 通过（28807，108.98 秒），
  包含独立复制的 Rust v15 插件打包、宿主绑定、四阶段真实回调、
  追加/不选择、构造错误及非本地串行候选回退。格式、diff 及十二项
  kernel 门禁检查通过（60651）。
- 生产宿主契约下二十一项参考插件单测及 124 项 runtime 单测重新
  通过（63284），源码边界及最终 diff 检查通过（32262）。本轮没有
  新增 Bazel、跨平台、无插件性能或实库 catalog 验证。
  旧 v14 的通过记录不作为本轮证明。

部分 schema/session 仍为夹具。实库 catalog、权限/保存点/并发可见性、
跨查询块/PX/缓存、类型/索引/恢复、AI 及轻量化目标保持不变。
GIS 继续由 C++ 实现，不因上层 Rust 规划入口而要求语言迁移。
