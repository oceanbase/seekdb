# 排序语义：Rust 深度插件的版本化检查接口

日期：2026-09-10。版本化接线、受控 kernel 和独立回归通过。
独立 Rust 排序算法尚未实现，不将元数据检查当作算法替代。

## 为什么需要独立语义

上层 Ordered 阶段不保证候选根就是 SORT，也不保证 SORT 是普通
全排序。它可能包含 prefix、partition、Top-N、Top-K、WITH TIES、
local merge、encoded keys 或运行期过滤。插件要替换算法，必须先
识别这些语义，而不是根据阶段或 C++ 枚举的数值猜测。

`candidate_context_v7`（service minor 6）追加 sort_info/sort_key。
宿主通过 CandidateGraph 读取已有字段，不调用可能修改逻辑计划的
get_op_exprs。旧 Role::Ordering 保留宿主构建绑定的方向值，不改义。

## 接口约定

- sort_info 返回实际 SQL 键数量、prefix/partition 数量，以及编码、
  local merge、ties、runtime filter 标志。
- Top-N、Top-K limit/offset 和 hash 表达式是本次调用内的图身份，
  与其他图入口共享身份；缺失用 UINT32_MAX，不是求值后的数值。
- 非 SORT 返回明确的空信息：无标志、零数量、全部可选表达式缺失。
  缺失能力、坏布局和宿主错误不是“非 SORT”。
- sort_key 读取未编码 SQL 键，独立规范化 descending/nulls_first。
  NULL 位置表示最终输出位置，不是降序翻转前比较器的内部 NULL 排位。
- 查询失败后输出清理、错误保持；错误不能被成功返回或后续读取掩盖。

Rust Hook::SORTS 同时启用参数、语义、query、graph 和 builder 前缀。
Context::sort 返回 Option<Sort>，sort_key 返回 SortKey；表达式身份
受 invocation 生命周期约束，不可以保存到计划或异步任务中。
未知标志、非零保留字段、prefix/partition 越界及非规范空结果拒绝。

Loader 验证完整宿主上下文，按 service minor 0—6 提供精确前缀，
重写本次 continuation；旧插件不会意外看到新后缀。Server-dev 的
构建契约、module lease、epoch 和既有 hook 组合协议继续生效。

## 真实 Rust 消费与边界

v16 参考插件保持五个服务／七个扩展，upper-policy 升级为 minor 6。
对本地串行候选读取实际 SORT 信息、每个排序键及可选表达式描述，
经完整校验后仍贡献保留原生结果和顺序的 spool。非 SORT 正常继续，
非本地串行候选不套用串行执行器，错误停止整组贡献。

这用于验证协议实际贯通，不是插件已经取代原生排序。通用片段
builder 可以保留 SORT 输入、替代其目标结果；参数拥有权检查仍然
适用。下一步是独立 Rust 排序策略和执行器，正确传递隐藏输出，
对不支持的特殊语义不贡献候选。随后扩大比较／collation、成本、
spill、资源预算、缓存与 PX。当前整数 JOIN 示例不是接口能力上限。

## 验证范围

- SDK：C/Rust size/align/offset、四种方向、普通和特殊排序、非 SORT、
  旧前缀及缺失能力、保留字段、非法标志、错误保持和生命周期。
- 宿主图：实际 ObLogSort 原始键和可选表达式共享身份、编码和
  prefix/partition/ties/merge、非 SORT、非法字段／索引／指针，
  读取不修改 output/order，不调用 get_op_exprs。
- 独立 loader：真实 Rust v16 四阶段读取、追加／不选择、错误、
  非 SORT／非串行回退，V7 后缀校验及旧 service 精确前缀。
- 受控 kernel：既有十组上层 SQL 及三十四组 JOIN SQL，正常规划、
  codegen、真实 Rust 动态库执行和重扫；新增图协议检查。

SDK 首轮通过（36876），增加缺失能力测试后全量回归重新通过
（13587，含 28 项文档测试）。生产构建通过（21458），完整 kernel
通过（69372，exit 0）：新增宿主 SORT 协议夹具、十组上层 SQL、
三十四组 JOIN SQL 及既有矩阵完成；真实 Rust v16 在正常上层回调中
读取 SORT 信息和键，结果与重扫保持正确。部分 SQL 由夹具后期选择
自定义根，不能据此声称生产成本模型自然选择 spool。

生产对应 v16 动态库重建与二进制审计通过（90469），独立目标重建
通过（59945），完整 CTest 30/30 通过（72912，105.57 秒），其中
rust_server_dev_package 独立复制／构建／打包真实 Rust 插件，经过
loader 的四阶段语义读取、错误、非 SORT／非串行回退和前缀检查。
生产契约下二十一项参考插件单测通过（44328）；runtime 124 项单测
通过（38742）。十二项构建门禁／格式检查通过（65417），源码边界
和 diff 检查通过（91252）。不借用 v15 的旧结果替代本轮验证。

本轮不将受控 schema/session 夹具当作实库 catalog、权限、保存点、
并发可见性或恢复证明；未新增 Bazel、跨平台和轻量化性能验收。
完整目标继续保持，GIS 继续使用 C++。
