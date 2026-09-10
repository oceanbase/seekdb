# seekdb 图功能 P0：语言与语义契约

本文提及的独立原型仅指历史验证，原型工具已移除；当前可执行测试为 `tools/deploy/mysql_test/test_suite/graph` 下的 mysqltest 用例。

版本：v0.1，2026-09-09。实现起点：`feat/graph` / `71e2b595bdff`。

本契约是 P1/P2 的实现输入；“目标支持”不表示当前 seekdb 已接受对应图语法。P0 实际交付的是测试工具和验证数据，不增加生产 SQL 入口。

## 1. 语言方向和兼容边界

采用 Oracle / DuckPGQ 的 SQL/PGQ 路线：`CREATE PROPERTY GRAPH`、`VERTEX TABLES`、`EDGE TABLES`、`GRAPH_TABLE`、`MATCH`、`COLUMNS`。以两者共同子集为起点；差异默认以 Oracle SQL/PGQ 文档作为语法参照，逐项记录，不引入 `GQL_TABLE` 别名。

普通类型、表达式函数、大小写与标识符规则仍由 seekdb MySQL 模式决定。Oracle 的 `NUMBER`、`VARCHAR2`、权限模型和 DuckDB 的 FROM-first 写法不随图语法一起引入。本文引用 Oracle 数据库 SQL property graph，不是 PGQL 或独立图服务器语言。

语法目标示例（**未在 seekdb 执行**；基表为测试夹具的普通表）：

```sql
CREATE PROPERTY GRAPH p0_graph
  VERTEX TABLES (
    gp_vertex KEY (tenant, id) LABEL person PROPERTIES (name),
    gp_company KEY (tenant, id) LABEL company PROPERTIES (name)
  )
  EDGE TABLES (
    gp_edge KEY (tenant, id)
      SOURCE KEY (src_tenant, src) REFERENCES gp_vertex (tenant, id)
      DESTINATION KEY (dst_tenant, dst) REFERENCES gp_vertex (tenant, id)
      LABEL knows PROPERTIES (weight),
    gp_works KEY (tenant, id)
      SOURCE KEY (src_tenant, src) REFERENCES gp_vertex (tenant, id)
      DESTINATION KEY (dst_tenant, dst) REFERENCES gp_company (tenant, id)
      LABEL works PROPERTIES (tenant, id)
  );

SELECT * FROM GRAPH_TABLE (
  p0_graph
  MATCH (a IS person)-[e IS knows]->(b IS person)
  WHERE e.weight = 1
  COLUMNS (a.name AS source_name, b.name AS target_name)
);
```

首版文档统一使用 `IS label`；`:` 简写只有在兼容矩阵增加对应验证后才纳入产品支持。键不因用于元素身份而自动变为公开属性；需要 `e.id` 投影的示例必须将 id 加入 PROPERTIES。

## 2. 模型、身份和数据契约

1. 图是基表映射，不持有第二份业务数据。写入走普通 SQL 和原有索引维护。
2. P1 首版要求显式声明稳定键，映射到基表主键；键列非 NULL。端点声明引用相应顶点映射的完整键，首版不支持任意表达式键。首版键类型限制为 BIGINT；原型验证两列 BIGINT 复合键。
3. 点身份为 `(图身份, 顶点映射身份, 类型化键元组)`，边身份为 `(图身份, 边映射身份, 类型化键元组)`。Label 不是身份。相同源点与终点的两条边仍是不同边。
4. P1 首版每个元素映射一个 Label，同一图内 Label 唯一；同一基表重复声明为多个映射先拒绝。不同基表的同值主键不相同。P4 再扩展多 Label，不通过复制元素实现。
5. 本实现策略不隐式创建外键，不自动级联删除。基表已有外键仍按 SQL 规则生效。`REFERENCES` 描述映射关联，不等于执行 `ALTER TABLE ADD FOREIGN KEY`。
6. 源或目标端点缺失、端点键含 NULL 的边不参与有效点边匹配；孤立点仍参与点匹配。即使不投影顶点属性也检查存在性。读取失败直接报错，不能当作端点缺失。
7. 第 6 条是 seekdb 的保守读取策略，**不宣称等同 Oracle TRUSTED MODE 的无效数据行为**。Oracle ENFORCED/TRUSTED OPTIONS 首版明确不支持；不得用“兼容 Oracle”隐去这项差异。

## 3. 查询、事务和错误契约

- 默认结果是 bag：平行边、不同匹配和重复外部输入可能产生相同投影行，只有显式 DISTINCT 才做结果去重。
- 固定模式默认不隐式添加点/边不重复约束。不同变量允许绑定同一元素；同一变量再次出现则要求同一身份。原型 `trail` 是显式边身份不等约束的内部验证，不表示已经实现 TRAIL 语法。
- 出向与入向分别绑定源/目标。无向模式的目标语义是两种方向匹配的并集，但同一自环的同一绑定仅计一次，平行边不合并；P0 当前未实现该方向转换。
- 对已映射属性，NULL 沿用 SQL 三值逻辑；未知 Label、未知属性或无效绑定是编译错误，不解释为 NULL。动态属性缺失属于 P4。
- 可选匹配的失败分支做 NULL 扩展，过滤作用域由语法位置决定；不得把可选分支过滤无条件移到外层 WHERE。P0 用 LEFT JOIN 基线验证孤立点与 NULL，图可选语法留 P1 单独实现。
- 无 ORDER BY 不承诺结果顺序。路径内节点/边序列不得因测试规范化而排序。
- P2 有界路径遵守本契约的身份模型，默认允许重复，显式边不重复模式保存路径历史；零跳必须有真实起点。无界枚举、最短路和点不重复不进入 P0。
- 固定模式降为同一条 SQL，沿用语句读视图和读自己的写入。P2 算子必须复用调用方快照、事务和 Schema guard，不在每跳重新取得快照或另开事务。
- P1 建图与查询按调用者权限检查相关基表；基表破坏性 DDL 首版采用阻止策略，允许普通 DML。计划依赖记录图和基表版本，图删除使依赖计划失效。
- 资源不足、超时、取消和实际读错误明确失败。原型 CLI 非零退出时此前输出不可作为完整结果。生产入口复用既有错误框架，不新建软截断成功语义。

## 4. 能力矩阵与验证对应

“拒绝”表示产品首版的目标行为；P0 工具不提供完整 Parser，不能把它对未知命令的拒绝称作 SQL 语法测试。

| 能力 | 产品阶段/状态 | 正例与边界例 | P0 证据 |
| --- | --- | --- | --- |
| 多点表、多边表建图 | P1 目标支持 | person/works/company；同键值不同表不串点 | `works` 转换和 mysqltest；DDL 文档核对 |
| 显式复合身份 | P1 目标支持 | (1,1)、(2,1)；重复 PK 拒绝 | C++ 身份测试、两项 mysqltest |
| 点查询与孤立点 | P1 目标支持 | id=5 孤立点；未知 Label 编译错误 | SQL 基线；Label 错误尚为契约 |
| 一跳/两跳/汇合 | P1 目标支持 | out/chain/converge；平行边保留重数 | 转换执行及独立穷举 |
| 入向匹配 | P1 目标支持 | in；不能把所有边当双向边 | 转换、单跳与随机差分 |
| 无向、自环 | P1 目标支持 | 自环一次、平行边各一次 | 自环有向已测；无向转换未实现 |
| 属性过滤 | P1 目标支持 | weight=1；NULL 不通过等值过滤 | filtered；NULL SQL 基线 |
| SQL 组合 | P1 目标支持 | 外层 JOIN、COUNT、LEFT JOIN；无隐式 DISTINCT | mysqltest 与批量访问候选 |
| 可选图模式 | P1 目标支持 | 缺边输出 NULL；过滤作用域不同 | LEFT JOIN 基线；图语法未实现 |
| 端点更新与回滚 | P1 目标支持 | 删除 id=3；插入 (9,9) 后孤儿边可见 | mysqltest、双连接 RR 验证 |
| 读取失败与取消 | P1/P2 目标支持 | 正常完成；注入错误不作空结果 | C++ 故障测试、真实 SQL 取消 |
| Schema 依赖与权限 | P1 目标支持 | 合法访问；破坏映射/缺权限拒绝 | 本文契约，图对象接入未实现 |
| 有界路径、路径结果 | P2 后续 | 零跳/上界；不可默认无界枚举 | 契约；非本次功能实现 |
| 动态属性、多 Label、图 DML | P4 后续 | 后续另列；首版不悄悄降级 | 首版拒绝契约 |
| GQL_TABLE、Cypher 入口 | 首版拒绝 | 使用 GRAPH_TABLE；不提供自定义别名 | 文档约束 |
| Oracle OPTIONS / DuckDB FROM-first | 首版拒绝 | 完整 SELECT；独有扩展不纳入 | 差异表 |

## 5. 来源与验证级别

核对日期 2026-09-09；Oracle 使用 26 SQL Language Reference，DuckPGQ 使用官方在线文档。在线文档不是稳定二进制版本，文档中有历史内容混杂，因此仅据其核对语法形态，不宣称运行兼容认证。

- [Oracle CREATE PROPERTY GRAPH](https://docs.oracle.com/en/database/oracle/oracle-database/26/sqlrf/create-property-graph.html)：建图、键和端点声明、ENFORCED/TRUSTED 差异。
- [Oracle GRAPH_TABLE](https://docs.oracle.com/en/database/oracle/oracle-database/26/sqlrf/graph_table-operator.html)：SQL 表表达式、MATCH 与 COLUMNS。
- [Oracle Graph Pattern](https://docs.oracle.com/en/database/oracle/oracle-database/26/sqlrf/graph-pattern.html)：模式变量共享、方向与过滤作用域。
- [DuckPGQ Property Graph](https://duckpgq.org/documentation/property_graph/) 与 [SQL/PGQ](https://duckpgq.org/documentation/sql_pgq/)：关系映射及查询示例。

Oracle / DuckPGQ 图语法尚未在本次运行环境执行。实际执行证据仅针对 seekdb 上的等价 SQL、内部原型和测试协议，详见验证报告。
