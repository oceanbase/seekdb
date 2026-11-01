# 保持原目录、原文件名的单 Notebook 修复

文件名保持 `seekdb_hipvs_batch_demo_executed.ipynb`；用于课程镜像的原目录 `/workspace/seekdb_hipvs_notebook/`，继续加载原 `lib/seekdb_demo.py`，数据库路径保持 `/workspace/run/obs1`。

输入文件实际包含 `Path` 未定义、数据库未启动导致连接 2881 失败、空探针导致 `IndexError`，同时残留原课程 12.7× 输出。原文件 SHA-256、备份名称和修复版哈希见 [repair-manifest.json](repair-manifest.json)。这证明 Notebook 内存在错误，不证明比赛平台的失败原因是修改了目录。

## 修改内容

- 初始化和启动单元各自补齐所需导入，启动失败就停止后续数据和查询步骤。
- SQL 执行失败立即报错，批量会话允许 600 秒，测量后检查返回数量；不把超时或空结果作为速度成绩。
- 数据准备、统计和召回的先后关系显式检查；重新测量时清除旧变量。
- 启动逻辑不调用课程脚本里的递归删除操作，已有同路径实例复用；2881 属于其他实例时明确停止本次执行。
- 增加最后的结果数量、一致性和 JSON 导出检查；不预设 CPU/GPU 或 GPU 单查/批量完全相同。

为只替换这一个 Notebook，修复函数以可见代码保存在初始化单元内，执行时安装到已导入辅助模块的内存命名空间。**磁盘上的原 `lib/seekdb_demo.py` 不会被修改，也不要求新增配套脚本。** GPU 功能仍来自课程预编译 seekdb/bridge，并非在 Notebook 中重写 GPU 算子。

源文件已清空旧输出，须在课程环境中从头执行；不能把仍未执行的源文件写为本轮已执行结果。

## 一步步替换原文件

1. 本机修复文件位于 `E:\seekdb\seekdb_hipvs_batch_demo_executed.ipynb`。同目录已保留 `before-fix` 备份，不用重新创建解压目录。
2. 在 Jupyter 保存并关闭正在打开的同名 Notebook 标签页，避免它的旧内容自动保存覆盖上传结果。
3. 左侧文件栏进入原 `seekdb_hipvs_notebook` 目录，确认旁边仍有原 `lib` 文件夹。
4. 点击上传，选择本机的同名修复文件。提示已有同名文件时选择覆盖，不使用带 `(1)` 的另一个文件名。
5. 双击这个同名文件，选择 **Kernel → Restart Kernel and Run All Cells…**，确认运行。
6. 初始化应打印 `original-directory runtime fixes loaded:`，后面为 `/workspace/seekdb_hipvs_notebook/lib/seekdb_demo.py`。
7. 等待 Idle，最后应产生本次真实 JSON，包含 `completed: true` 和 `sql_results_validated: true`。任何红色错误出现后停止，先查看第一个报错。
8. 保存并下载此文件及 `results/summary.json`，再将真正执行后的版本归档到仓库；加速比以本次输出为准。

当前 Radeon Cloud 已有课程环境，不需要先粘贴 `docker run` 到 Notebook。那条命令用于在具备 Docker 权限和 ROCm 设备的 Linux 主机上准备环境，不会修复 Notebook 的 Python 导入和执行顺序问题。
