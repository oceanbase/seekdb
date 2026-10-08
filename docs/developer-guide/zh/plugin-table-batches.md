# 插件表函数批量执行

## 执行契约

插件表函数在向量化计划中通过 `ObFunctionTableOp::inner_get_next_batch` 消费。
宿主将调用方请求与算子的最大批次大小取较小值，传给现有 C SPI
`cursor.next(context, maximum_rows, emitted_rows)`。Rust `Cursor::next` 使用
`Rows::remaining()` / `Rows::emit()`，不需要增加另一套插件接口。

一次 native next 可以返回多行，也可以返回不足上限的非空批次；后者不代表 EOF。
EOF 必须单独返回零行。宿主核对实际 emit 次数和插件报告的行数，拒绝超过上限、
成功但零行、EOF 携带行等不一致结果。旧的逐行入口继续请求一行。

每个 emit 写入当前批次自己的 datum 槽位；变长结果使用对应行的宿主结果缓冲区。
这是“一次 native 回调产生多行”的批量接入，仍通过逐行 emit 交换数据。
不是 Arrow/列式 C ABI，不是所有类型的零拷贝，也不是已完成的性能结论。

## 列裁剪

SQL codegen 按插件完整的列声明建立 ordinal 映射；未被 SQL 引用的位置为 nullptr，
不生成该列的结果表达式。SELECT ordinal 不会错误地将第二列当成第一列，COUNT(*)
可仅消费行数。插件仍按完整声明 emit 行；宿主检查完整行的类型/NULL/保留字段，
按投影转换所需列。minor 4 新增下面的可选投影信息，使插件可以减少无用字段计算。

引用整理不再为插件表函数强制引用所有声明列；仅由 SELECT/WHERE 等实际表达式
决定保留项。冻结的 binding 仍保存完整声明，列裁剪不会改变插件协议。内置/PL
表函数继续使用旧的完整列保留规则。WHERE 使用但 SELECT 不输出的列不能被删除。

### 插件可见的投影信息（table SPI minor 4）

`seekdb_plugin_table_execution_context_v4_t` 保留 v3 前缀，追加完整声明列数和
按声明序号排列的 `requested_columns` 借用数组，每项为 0/1。数组只在当前同步
open/next 回调期间有效；不能保存进 cursor，也不能跨线程。宿主每批生成掩码，
不是每行生成。SQL 上层过滤使用的列也必须置 1；不能只看 SELECT 输出列表。

Rust 插件选择 `table::Service::<C>::WITH_PROJECTION`；有估算回调时使用
`table_planning::Service::<C>::WITH_PROJECTION`。open 的 `QueryContext` 和 next
的 `Rows` 都提供 `projection_column_count()`、`column_requested(index)`。
需要 SQL 的插件可选择 `WITH_SQL_AND_PROJECTION`，继续严格要求 SQL 能力；
仅请求投影不要求宿主提供 SQL/poll。

- 旧上下文，或列数为 0 且指针为空：没有投影信息，所有列都按需要处理。
- 非空且全零的掩码：只需要行数，仍必须正确扫描并产生原有数量的行。
- 不需要的列可输出合法的轻量占位值，仍保留完整列数、顺序、类型和 NULL 约束。
  不允许缩短行、省略 cell、用 NULL 代替声明为 NOT NULL 的值，或跳过外部副作用。
- 每次回调重新读取掩码；前一批未请求 ordinal 不代表可以不更新游标序号。
  raw rescan 没有查询上下文，后续 next 再读取投影。
- SDK 拒绝非法指针/列数组合、超过 4096 列、非 0/1 元素及非零保留字段；
  已知列数时，emit 必须具有相同 arity。FFI 调用者仍负责提供有效的内存范围。
- loader 给 minor 0/1、2、3 服务分别裁剪到精确 v1、v2、v3 大小，避免改变旧插件
  严格 struct_size 检查。请求 minor 4 的插件也必须处理旧宿主仅提供短上下文的情况。

Rust text 分词示例在未请求 token 时输出空字节，在未请求 ordinal 时输出整数 0；
但分词扫描和序号推进不变。这验证协议可用于跳过字段产出，不代表分词本身更快，
也不是 filter pushdown、列式传输或已测量的吞吐改进。

## 错误与资源

批次内的 SQL/poll/emit 错误与逐行入口共用首错状态。即使此前已经写入若干内部
datum，失败时也不报告可消费行数；游标立即关闭并释放 module lease。重复读取保持
失败，只有显式 rescan/close 才重置。数据库错误并不撤销已发生的外部副作用。

未读完时 close、重扫、EOF 后重复读取均沿用已有生命周期。非插件的 PL/system
表函数在向量化算子中使用单行回退，保持原始函数调用路径。

## 验证范围

`rust/plugin-runtime/tests/rust_sql_expression_fixture.h` 的批量用例经过真实
parser/resolver、生产引用整理、批次表达式帧、`ObOperatorFactory` 调用的表函数
专有 spec codegen、生产算子、loader 和 Rust 动态库。覆盖完整投影、分别仅第一/第二
列、反转 SELECT 列顺序、仅计数所需的零列物化、WHERE 依赖保留，以及不同请求大小、
Unicode/长字符串、重复 EOF、重扫及第一行 emit 后取消。测试包装器统计 next 次数，
并在 open/next 检查生产路径生成的 v4 掩码；执行仍委托真实 loader。额外验证非法/重复列序号在打开游标前被拒绝，以及重复引用
整理不会重新引入已裁剪列。内置 generator 的 0/3 行及向量化算子单行回退也已通过。

`loader_registration.cpp` 增加真实 Rust DSO 的逐回调掩码切换、合法占位值、全零
掩码、raw rescan 后旧上下文回退、非法掩码/失败保持和 lease 释放断言。SQL-series
仍声明 minor 3，测试用非法 v4 后缀确认 loader 对 open/next 都裁剪未知后缀。
SDK 的 `table_projection.rs` 和 C/Rust layout 对照覆盖可选能力、缺少 SQL、
非法字段及完整行 arity；这些不能替代实库与完整优化器测试。

该用例仍手动装配逻辑算子与通用 output/calc/filter 字段，不等于真实客户端运行
完整优化器所选的计划；专有列映射已经由生产 codegen 生成。COUNT(*) 场景验证无列
物化的行流，不代表该用例执行了聚合算子。完整客户端查询、PL 集合回退矩阵、取消
延迟和吞吐/RSS 指标仍需更高层回归验证。

只读的 opt-in 实库脚本要求预先安装本工作区的 Rust text 插件和已有测试数据库：

```bash
python3 rust/plugin-runtime/tests/table_batches_server.py \
  --port 2881 --database plugin_test --confirm-disposable-server
```

密码通过 `SEEKDB_TEST_PASSWORD` 提供。脚本只连接 loopback，不安装插件、不创建
数据库、不修改数据或服务端配置；验证 3001 行、投影/聚合/过滤/类型消费、NULL、
表函数嵌套 SQL 和 LIMIT 后再次读取。本轮环境禁止网络 socket，尚未运行实库用例；
仅脚本语法/CLI 检查不能证明 SQL 查询通过。

关联：[查询状态](plugin-query-control.md)、[表函数 SQL](plugin-table-sql.md)、
[完整设计目标](plugin-next-design.md)。
