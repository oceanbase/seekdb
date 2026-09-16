# 有界图 WALK 查询

SeekDB 的 `GRAPH_TABLE` 支持有界有向 `WALK`。`WALK` 允许重复访问同一个点或边，并保留平行边、汇合路径和重复检索种子的重数。

```sql
SELECT *
FROM GRAPH_TABLE(
  graph_social
  MATCH (a IS person WHERE a.id = 1)
        -[e IS knows WHERE e.since >= 2022]->{0,3}
        (b IS person WHERE b.age >= 18)
  ONE ROW PER STEP (v1, step_e, v2)
  COLUMNS (
    MATCHNUM() AS match_no,
    ELEMENT_NUMBER(step_e) AS step_no,
    VERTEX_ID(v1) AS from_id,
    EDGE_ID(step_e) AS edge_id,
    VERTEX_ID(v2) AS to_id
  )
) g;
```

## 支持矩阵

| 能力 | P2-WALK |
| --- | --- |
| 有界量词 | `{n}`、`{n,m}`、`{,m}`，且 `0 <= n <= m <= 16` |
| 方向 | OUT、IN |
| 路径模式 | 隐式 `WALK` |
| 模式范围 | 一个有向量化边段 |
| 默认行形态 | `ONE ROW PER MATCH` |
| 逐步行形态 | `ONE ROW PER STEP (from,edge,to)` |
| 边组变量 | `COUNT(edge.property)`、`COUNT(EDGE_ID(edge))`、`JSON_ARRAYAGG(edge.property)`、`JSON_ARRAYAGG(EDGE_ID(edge))` |
| 逐步投影 | 迭代变量属性、`MATCHNUM()`、`ELEMENT_NUMBER()`、`VERTEX_ID()`、`EDGE_ID()` |
| 相关种子 | 显式 `LATERAL` 子查询的起点过滤可引用左侧列 |

`*`、`+`、`{n,}`、无向边、多个量化段、量化段与其他边段组合、命名路径、`TRAIL`/`SIMPLE`/`ACYCLIC` 和 `ONE ROW PER VERTEX` 会被明确拒绝。

## 结果语义

- 起点过滤只执行一次；边过滤每跳执行；终点过滤只决定当前路径是否输出，不阻止从不满足终点条件的中间点继续扩展。
- 零跳要求同一个点同时满足起终点模式。逐步形态输出一行，`from` 是真实起点，边、`to` 和元素序号为 `NULL`。
- `ELEMENT_NUMBER()` 返回点边交替路径中的元素位置：点为 `1、3、5...`，边为 `2、4、6...`；迭代变量不能复用图模式中已经声明的变量名。
- `JSON_ARRAYAGG` 按遍历顺序输出；零跳沿用现有空集合聚合结果 `NULL`。
- 元素身份是包含 `GRAPH_OWNER`、`GRAPH_NAME`、`ELEM_TABLE`、`KEY_VALUE` 的 JSON。复合键取映射键的声明顺序；内部判等使用类型化键值，不比较 JSON 文本。
- 查询保持 bag 语义；只有外层 `DISTINCT` 去重。没有外层 `ORDER BY` 时不承诺行顺序。
- 即使没有投影目标点属性，也始终验证目标点存在。孤儿边不产生路径；扫描、回查、RPC、取消、超时或内存失败会使整条查询失败。

SeekDB 不自动创建邻接索引。以源点或目标点开头的索引可以改变访问计划，但不能改变路径和重数。P2-WALK 不提供 fan-out 截断、部分成功或路径落盘。

GraphRAG 应使用显式 `LATERAL` 将 KNN CTE 的 seed ID/score 关联到起点过滤。确定性测试使用精确 KNN；近似检索只用于验证组合链路，不把候选集合当成稳定断言。
