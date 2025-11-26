# 使用与 GitHub Actions 相同的基础镜像
FROM ubuntu:22.04

# 设置环境变量以避免交互式提示
ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=UTC

# 设置工作目录
WORKDIR /workspace

# 安装构建所需的依赖包（与 GitHub Actions 中相同）
RUN apt-get update -y
RUN apt-get install -y git wget rpm rpm2cpio cpio make build-essential binutils m4 libtool-bin libncurses5 python3
# RUN rm -rf /var/lib/apt/lists/*
# 复制项目文件到容器中
COPY . .

# 创建构建脚本
RUN bash build.sh init

# 将 deps 目录添加到 PATH
ENV PATH="/workspace/deps/3rd/usr/local/oceanbase/devtools/bin:${PATH}"
RUN bash build.sh debug -DOB_USE_CCACHE=ON
RUN ccache -z
RUN cd build_debug && make -j$(nproc)
RUN ccache -s
