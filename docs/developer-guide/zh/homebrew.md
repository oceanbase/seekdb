# Homebrew 优化配置

本文档帮助你优化 Homebrew 的下载速度，特别适用于国内用户。

## 配置镜像加速

如果你在使用 Homebrew 时遇到下载速度慢的问题，可以通过配置国内镜像来加速。

将以下环境变量添加到你的 shell 配置文件中（如 `~/.zshrc` 或 `~/.bash_profile`）：

```shell
export HOMEBREW_BREW_GIT_REMOTE="https://mirrors.tuna.tsinghua.edu.cn/git/homebrew/brew.git"
export HOMEBREW_CORE_GIT_REMOTE="https://mirrors.tuna.tsinghua.edu.cn/git/homebrew/homebrew-core.git"
export HOMEBREW_INSTALL_FROM_API=1

export HOMEBREW_API_DOMAIN="https://mirrors.tuna.tsinghua.edu.cn/homebrew-bottles/api"
export HOMEBREW_BOTTLE_DOMAIN="https://mirrors.tuna.tsinghua.edu.cn/homebrew-bottles"

export HOMEBREW_PIP_INDEX_URL="https://mirrors.tuna.tsinghua.edu.cn/pypi/web/simple"
```

添加完成后，重新加载 shell 配置：

```shell
source ~/.zshrc  # 或 source ~/.bash_profile
```

## 参考链接

- [清华大学 Homebrew 镜像使用帮助](https://mirrors.tuna.tsinghua.edu.cn/help/homebrew/)
