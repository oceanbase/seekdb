# SeekDB 执行下沉脚本（Native）

与 `seekdb-native.yml` 配套，在 self-hosted Runner 上直接执行 Prepare → Compile → Mysqltest，不经过 Farm2。

**脚本来源**：已从 farm-jenkins 复制到本目录 `scripts/`，无需再配置 FARM2_SCRIPTS_REPO 或 clone。

| 文件 | 说明 |
|------|------|
| `scripts/frame.sh` | 自 farm-jenkins 复制并改造（/etc/hosts、dep_cache 在无权限时跳过） |
| `scripts/farm_compile.sh` | 自 farm-jenkins 复制 |
| `scripts/farm_post_compile.sh` | 自 farm-jenkins 复制 |
| `scripts/mysqltest_for_farm.sh` | 自 farm-jenkins 复制 |
| `scripts/dep_cache.sh` | 本仓 stub，供 frame 在无 dep_create 时使用 |
| `prepare.sh` | 仅生成 jobargs.output、run_jobs.output |
| `compile.sh` | 调用 scripts/farm_compile.sh（frame prepare + build） |
| `mysqltest_slice.sh` | 调用 scripts/mysqltest_for_farm.sh |
| `collect_result.sh` | 汇总 fail_cases 写 seekdb_result.json |

## 仓库变量（可选）

| 变量 | 说明 |
|------|------|
| `FARM2_WORKER_IMAGE` | 编译/测试用 Docker 镜像；不设则在 Runner 本机执行 |
| `FORWARDING_HOST` | mirrors.oceanbase.com 解析到该主机时填写 |
| `RELEASE_MODE` | 非空则 release 编译 |

## 产物位置

- 任务目录：`$GITHUB_WORKSPACE/seekdb_build/$GITHUB_RUN_ID/`
- 编译产出：observer.zst、obproxy.zst、compile.output
- 各 slice：mysqltest.output.$i、collected_log_$i.tar.gz 等
- 小结果由 workflow 上传为 artifact `seekdb-result-native`
