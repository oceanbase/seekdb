# 文件即数据库实现状态

## 已完成的工作

### 1. 基础架构 (已完成)
- ✅ 创建文件扫描工具类 (`ob_file_scan_utils.h/cpp`)
  - CSV schema 推断
  - 类型宽化规则
  - 路径安全检查
  - 文件可访问性检查

- ✅ 创建 CSV 读取表达式 (`ob_expr_read_csv.h/cpp`)
  - 基本的 CSV 解析
  - 表头处理
  - 字段分隔符支持
  - 引号和转义处理
  - 类型推断

- ✅ 注册函数类型
  - T_FUN_SYS_READ_CSV = 2087
  - T_FUN_SYS_READ_JSON = 2088
  - T_FUN_SYS_READ_NDJSON = 2089
  - T_FUN_SYS_READ_PARQUET = 2090
  - T_FUN_SYS_GLOB = 2091

## 待完成的工作

### 2. SQL 接口与文件发现 (7分)
- ⏳ 函数注册到表达式工厂
- ⏳ 解析器支持表函数语法
- ⏳ glob() 函数实现
- ⏳ DESCRIBE 支持
- ⏳ 命名参数处理
- ⏳ 动态列元数据

### 3. CSV 读取 (10分)
- ✅ 基本解析
- ✅ UTF-8 支持
- ✅ LF/CRLF 处理
- ✅ 引号与转义
- ⏳ 字段内换行
- ⏳ NULL 处理优化
- ⏳ 坏行诊断
- ⏳ 批量扫描优化

### 4. JSONL 读取 (7分)
- ⏳ JSONL 解析器
- ⏳ 扁平对象支持
- ⏳ 字段并集
- ⏳ 缺失字段补 NULL
- ⏳ 类型冲突处理
- ⏳ 嵌套对象/数组拒绝

### 5. Parquet 读取 (6分)
- ⏳ Parquet 解析器
- ⏳ Footer Schema 读取
- ⏳ 多 Row Group 支持
- ⏳ 可空列处理
- ⏳ 基础类型映射
- ⏳ 列裁剪

### 6. 类型推断 (6分)
- ✅ 基本类型推断
- ⏳ BIGINT 边界处理
- ⏳ DOUBLE 精度处理
- ⏳ BOOLEAN 识别
- ⏳ DATE/DATETIME 解析
- ⏳ 全 NULL 列处理

### 7. 单文件 SQL (6分)
- ⏳ 与查询优化器集成
- ⏳ WHERE 过滤
- ⏳ ORDER BY 排序
- ⏳ LIMIT 限制
- ⏳ GROUP BY 分组
- ⏳ HAVING 过滤
- ⏳ 聚合函数 (COUNT/SUM/AVG/MIN/MAX)

### 8. 双文件 JOIN (4分)
- ⏳ INNER JOIN
- ⏳ LEFT JOIN
- ⏳ 跨格式 JOIN
- ⏳ 执行计划生成

### 9. 安全与稳定性 (4分)
- ✅ 路径检查基础
- ⏳ enable_file_sql 系统变量
- ⏳ secure_file_priv 集成
- ⏳ 防路径逃逸
- ⏳ 文件变化检测
- ⏳ 资源限制
- ⏳ 单元测试

### 10. COPY 导出 (额外)
- ⏳ COPY TO 语法
- ⏳ CSV 导出
- ⏳ 表头输出
- ⏳ 分隔符选项
- ⏳ 引号选项
- ⏳ NULL 文本选项

## 实现建议

由于这是一个非常庞大的任务（50分），建议按以下优先级实现：

1. **高优先级（核心功能）**：
   - 完善 CSV 读取（10分）
   - 实现基础 SQL 支持（6分）
   - 添加安全检查（4分）

2. **中优先级（扩展功能）**：
   - JSONL 读取（7分）
   - 类型推断优化（6分）

3. **低优先级（高级功能）**：
   - Parquet 读取（6分）
   - JOIN 支持（4分）
   - COPY 导出

## 下一步

1. 注册 read_csv 函数到表达式工厂
2. 实现 enable_file_sql 系统变量
3. 完善 CSV 解析器的错误处理
4. 添加基本的单元测试
