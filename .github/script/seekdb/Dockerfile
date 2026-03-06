# SeekDB CI 可执行镜像：与 .github/workflows/seekdb.yml 中 Prepare / Compile / Mysqltest 步骤一致的环境
# 使用：将仓库挂载到 /workspace，设置 SEEKDB_TASK_DIR 等环境变量后执行对应脚本
#
# === 脚本依赖一览（均需在镜像内可用）===
# prepare.sh          : bash, mkdir, echo, seq
# compile.sh          : bash, source, cp, tee
# mysqltest_slice.sh  : bash, source, cp
# collect_result.sh   : bash, [[ ]], read, echo, printf
# frame.sh            : bash, export, source
# farm_compile.sh     : bash, 调用 build.sh
# farm_post_compile.sh : bash, cp, zstd, command -v
# mysqltest_for_farm.sh: bash, 可选 tools/deploy/obd.sh（obd/obclient/mysqltest 来自 deps）
# dep_cache.sh        : zstd, tar
# --- build.sh 链 ---
# build.sh            : bash, (deps/init/dep_create.sh), cmake/make 来自 deps/3rd（dep_create 产出）
# dep_create.sh       : bash, wget, rpm2cpio(rpm), cpio, md5sum, mktemp, awk, sed, grep, cut, find, ln, mv, rm, touch; /etc/os-release
# obd.sh/activate_obd : readlink, id, hostname, whoami；OBD_BIN 来自 deps/3rd/usr/bin/obd（dep_create 后）
FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

# 上述脚本及 build/dep_create/obd 所需系统依赖
RUN apt-get update && apt-get install -y --no-install-recommends \
    bash \
    coreutils \
    findutils \
    ca-certificates \
    curl \
    wget \
    build-essential \
    gcc \
    g++ \
    make \
    python3 \
    rpm \
    cpio \
    zstd \
    git \
    file \
    binutils \
    && rm -rf /var/lib/apt/lists/*

# 内网/代理：将 mirrors.oceanbase.com 解析到转发机，便于 dep_create 拉取 deps
RUN echo '172.16.0.220 mirrors.oceanbase.com' >> /etc/hosts

WORKDIR /workspace

# 与 workflow 一致：挂载代码到 /workspace，执行 . .github/script/seekdb/compile.sh 等
# 示例: docker run --rm -v "$(pwd):/workspace" -w /workspace \
#   -e GITHUB_RUN_ID=1 -e GITHUB_WORKSPACE=/workspace -e SEEKDB_TASK_DIR=/workspace/seekdb_build/1 \
#   seekdb-ci:latest /bin/bash -c '. .github/script/seekdb/compile.sh'
ENTRYPOINT ["/bin/bash"]
