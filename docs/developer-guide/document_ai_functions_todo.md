# Document AI Functions 实现待办清单

本文档用于梳理本题中两个 AI Function 的实现范围、涉及文件和推荐修改顺序。当前先不改代码，只把“要做什么、改哪些文件、为什么改”讲清楚，方便后续按步骤落地。

## 目标说明

本题包含两个函数：

1. `LOAD_FILE(location_name, file_name)`：从外部存储位置读取本地文件，并转换成数据库函数可返回的 blob 数据。
2. `AI_SPLIT_DOCUMENT(content[, parameters])`：对文本或 Markdown 内容进行切分，输出 chunk 表，每行包含 `CHUNK_ID`、`CHUNK_OFFSET`、`CHUNK_LENGTH`、`CHUNK_TEXT`。

这两个函数的核心不是“把一个算法写出来”这么简单，而是要把函数接口、参数校验、执行链路、返回格式、异常处理、测试用例完整串起来。

## 推荐实施顺序

1. 先确认现有 AI 函数公共框架能否直接复用。
2. 再分别实现 `LOAD_FILE` 和 `AI_SPLIT_DOCUMENT` 的语义。
3. 最后补齐 mysqltest 用例和结果文件。
4. 如果新增或调整了源码编译项，再同步更新 CMake。

## 按文件维度的详细待办

### 1. `src/sql/engine/expr/ob_expr_ai/ob_ai_func.h`

这个文件是 AI Function 的公共接口层，先看它是否已经足够表达新函数需要的能力。

待办：

- 确认 `ObAIFuncBase`、`ObAIFuncIComplete`、`ObAIFuncIEmbed`、`ObAIFuncIRerank` 这些抽象接口是否能支撑新的文档函数。
- 如果 `LOAD_FILE` 和 `AI_SPLIT_DOCUMENT` 不属于“模型请求类函数”，需要补充新的接口抽象，避免把文档处理逻辑硬塞进现有 complete/embed/rerank 接口里。
- 检查 `ObAIFuncExprInfo` 是否还需要新增字段，用来表达新算子的名称、类型或额外配置。

为什么要看这里：

- 这里决定函数能力的“公共形状”。如果接口层不先定好，后面的 resolver 和执行层会反复返工。

### 2. `src/sql/engine/expr/ob_expr_ai/ob_ai_func.cpp`

这个文件负责公共信息的拷贝、初始化和模型信息写入。

待办：

- 检查新的 AI Function 是否需要在这里补充初始化逻辑。
- 如果新增了函数元信息字段，要同步补 `deep_copy`、`init` 和序列化相关处理。
- 确保新函数在拿到 schema/model 信息时能正确报错，而不是返回空结果或未定义行为。

为什么要看这里：

- 这是 AI 函数公共数据的落点，很多函数元数据会先经过这一层。

### 3. `src/sql/engine/expr/ob_expr_ai/ob_ai_func_client.h` 和 `src/sql/engine/expr/ob_expr_ai/ob_ai_func_client.cpp`

这个客户端类是 AI 函数调用外部服务的通道。虽然 `LOAD_FILE` 主要是文件读取，不一定走远端模型调用，但 `AI_SPLIT_DOCUMENT` 如果被设计成“内部本地处理”或“统一走 AI 函数框架”，这里仍然可能需要协同调整。

待办：

- 判断两个新函数是否复用现有 HTTP/AI 客户端框架。
- 如果 `AI_SPLIT_DOCUMENT` 不需要远端调用，就不要强行走这套 client，避免语义不清晰。
- 如果 `LOAD_FILE` 或 `AI_SPLIT_DOCUMENT` 要统一纳入 AI Function 执行框架，就需要明确返回值、错误码和响应解析方式。

为什么要看这里：

- 这里决定函数是“本地执行”还是“远端调用”。对本题来说，这个边界必须先划清楚。

### 4. `src/sql/engine/expr/ob_expr_ai/ob_expr_ai_complete.h` / `ob_expr_ai_complete.cpp`

这是现有 AI complete 类表达式实现。

待办：

- 评估是否要新增一个专门的文档 AI 算子实现，而不是把 `LOAD_FILE`、`AI_SPLIT_DOCUMENT` 混入 complete 类。
- 如果现有表达式注册机制已经可扩展，可以参考这里的写法新增函数类。
- 如果需要在 SQL 层新增算子名映射，也要在这里附近补充相应注册逻辑。

为什么要看这里：

- 它是理解现有 AI 表达式如何落地的最佳参照。

### 5. `src/sql/engine/expr/ob_expr_ai/ob_expr_ai_embed.h` / `ob_expr_ai_embed.cpp`

这是向量 embedding 类表达式实现。

待办：

- 只作为参考，不一定直接修改。
- 如果文档函数要复用参数解析、上下文构造或结果格式化逻辑，可以把这里当作样板。

为什么要看这里：

- 现有 AI 函数的参数组织方式，很多会在这里体现，适合借鉴，不适合盲改。

### 6. `src/sql/engine/expr/ob_expr_ai/ob_expr_ai_rerank.h` / `ob_expr_ai_rerank.cpp`

这是 rerank 类表达式实现。

待办：

- 只做参考，主要用于了解 AI 函数表达式的注册、参数处理和返回值封装方式。
- 如果新函数需要多输入参数、批量响应或特殊输出结构，可以借鉴这里的组织方式。

### 7. `src/sql/engine/expr/ob_expr_ai/ob_expr_ai_prompt.h` / `ob_expr_ai_prompt.cpp`

这是 prompt 类相关实现。

待办：

- 检查是否已有和文档处理相关的参数拼接/文本组织逻辑。
- 如有可复用的文本预处理方法，优先复用，避免新函数重复实现字符串拼装。

### 8. `src/sql/resolver/dml/ob_dml_resolver.cpp`

这是很关键的接入点，负责把 SQL 解析成可执行表达式。

待办：

- 为 `LOAD_FILE` 和 `AI_SPLIT_DOCUMENT` 增加解析与绑定逻辑。
- 明确两个函数在 resolver 里如何识别：函数名、参数个数、参数类型、是否允许默认参数。
- 对 `AI_SPLIT_DOCUMENT` 的 `parameters` JSON 做字段校验，至少覆盖 `type`、`by`、`max`、`overlap` 等关键配置。
- 对 `LOAD_FILE` 的 `location_name` 和 `file_name` 做基础校验，避免空值或非法名称直接进入执行层。

为什么要看这里：

- SQL 层要先认出这两个函数，后面的执行层才有机会真正处理它们。

### 9. 可能新增的源码文件或注册文件

如果现有 AI Function 框架不能直接承载这两个函数，通常还需要新增或补充以下类型的文件：

- 新的表达式实现文件，用来承接 `AI_SPLIT_DOCUMENT` 的具体行为。
- 新的解析辅助文件，用来处理文档切分和参数 JSON。
- 如果函数注册表是分散维护的，还要增加函数名到实现类的映射。

这一类文件是否真的要新增，要以第 1 步的框架检查结果为准，先确认再动手。

### 10. `src/sql/CMakeLists.txt`

这个文件只在新增源码文件或调整编译依赖时修改。

待办：

- 如果新增了 `.cpp` 文件，需要把它加入对应的 `ob_set_subtarget` 或编译列表。
- 确保新文件被正确编进 `ob_sql` 或相关子模块。
- 如果没有新增源码，只改现有文件，就尽量不要动这里。

为什么要看这里：

- 否则代码写完了，编译系统可能根本没把新实现编进去。

### 11. `tools/deploy/mysql_test/test_suite/ai_funcs/t/load_file.test`

这是 `LOAD_FILE` 的 mysqltest 主测试文件。

待办：

- 覆盖基本读取流程：创建 location、写入测试文件、调用 `load_file`、验证返回内容。
- 增加长度或类型校验，确认 blob 输出是完整的，不是截断文本。
- 保持测试自包含，避免依赖外部人工准备的文件。

### 12. `tools/deploy/mysql_test/test_suite/ai_funcs/r/load_file.result`

这是 `LOAD_FILE` 的结果基线文件。

待办：

- 当 `load_file.test` 的输出发生变化时，同步更新这里的期望结果。
- 确保文件内容与测试命令一一对应，避免 CI 比对失败。

### 13. `tools/deploy/mysql_test/test_suite/ai_funcs/t/ai_split_document.test`

这是 `AI_SPLIT_DOCUMENT` 的 mysqltest 主测试文件。

待办：

- 覆盖 sentence 切分场景。
- 覆盖 word 切分与 overlap 场景。
- 覆盖 markdown 场景，并验证标题继承行为。
- 明确输出列至少包含 `chunk_id` 和 `chunk_text`，并尽量验证 `chunk_offset`、`chunk_length`。

### 14. `tools/deploy/mysql_test/test_suite/ai_funcs/r/ai_split_document.result`

这是 `AI_SPLIT_DOCUMENT` 的结果基线文件。

待办：

- 将所有 chunk 结果固化下来，尤其是 offset 和 length 的边界值。
- 如果 chunk 文本里包含换行或标题继承，结果文件要完全对齐真实输出。

### 15. `tools/deploy/mysql_test/test_suite/ai_funcs/t/ai_parse_doc.test` 和 `tools/deploy/mysql_test/test_suite/ai_funcs/r/ai_parse_doc.result`

这两个文件不是本题要求的重点，但可以作为文档 AI 功能的参考样例。

待办：

- 只做对照，不建议无关修改。
- 如果发现 `LOAD_FILE` 影响了 `ai_parse_doc` 的前置依赖，再做最小范围联动检查。

## 重点验证点

1. `LOAD_FILE` 是否能正确读取本地 location 下的文件，并返回完整 blob。
2. `AI_SPLIT_DOCUMENT` 是否按 `by=sentence`、`by=word`、`type=markdown` 三种场景稳定切分。
3. `chunk_offset` 和 `chunk_length` 是否与原文位置一致。
4. 参数 JSON 是否能正确识别默认值和边界值。
5. mysqltest 的 `.test` 和 `.result` 是否严格一致。
6. 如果新增源码文件，`src/sql/CMakeLists.txt` 是否已纳入编译。

## 交付标准

当这件事做完时，应该满足以下条件：

- SQL 层能识别两个函数。
- 执行层能返回稳定结果。
- 错误输入有明确报错。
- mysqltest 可以独立跑通。
- 结果基线已更新，并能用于 CI 回归。

## 建议的后续动作

1. 先确认 `LOAD_FILE` 和 `AI_SPLIT_DOCUMENT` 是复用现有 AI 表达式框架，还是新增独立算子。
2. 再按本文件里的清单逐个改源码文件。
3. 最后统一补测试和结果基线。
