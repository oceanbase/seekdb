# 自定义 ARC Runner 镜像 — 预装 Rust 工具链及扩展依赖
#
# 必须基于 ghcr.io/actions/actions-runner，原因：
#   1. 内含 runner agent（run.sh）— 负责向 GitHub 注册、领取 job、上报结果
#   2. 内含 /home/runner/externals/ — JavaScript Action 的 Node.js runtime
#   3. 内含正确的用户/目录结构 — runner agent 依赖 runner 用户和 /home/runner 目录
#
# 基础镜像：固定为基于 Ubuntu 22.04 的 runner 版本，避免使用 latest 后因官方切到 24.04 导致行为变化。
# 更新时可在 https://github.com/actions/runner/pkgs/container/actions-runner 查看可用标签。
#
# 构建：
#   docker build -t your-registry/arc-runner-extended:latest -f docker/runner-extended/Dockerfile_seekdb .
#
# 使用：
#   在 Helm values 中将 runner 容器和 init-dind-externals 的 image 都替换为此镜像

FROM ghcr.io/actions/actions-runner:2.321.0

USER root

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
    m4 \
    libtool-bin \
    libncurses5 \
    make \
    ccache \
    python3 \
    rpm \
    rpm2cpio \
    cpio \
    zstd \
    git \
    file \
    binutils \
    pkg-config \
    libssl-dev \
    libaio1 \
    && rm -rf /var/lib/apt/lists/*

# 切回 runner 用户 — runner agent 必须以此用户运行
USER runner