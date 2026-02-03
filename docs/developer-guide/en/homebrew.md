# Homebrew Optimization

This guide helps you optimize Homebrew for faster downloads, especially useful for users in China.

## Set up Homebrew mirrors

If you experience slow downloads with Homebrew, you can configure mirrors to accelerate the process.

Add the following environment variables to your shell configuration file (e.g., `~/.zshrc` or `~/.bash_profile`):

```shell
export HOMEBREW_BREW_GIT_REMOTE="https://mirrors.tuna.tsinghua.edu.cn/git/homebrew/brew.git"
export HOMEBREW_CORE_GIT_REMOTE="https://mirrors.tuna.tsinghua.edu.cn/git/homebrew/homebrew-core.git"
export HOMEBREW_INSTALL_FROM_API=1

export HOMEBREW_API_DOMAIN="https://mirrors.tuna.tsinghua.edu.cn/homebrew-bottles/api"
export HOMEBREW_BOTTLE_DOMAIN="https://mirrors.tuna.tsinghua.edu.cn/homebrew-bottles"

export HOMEBREW_PIP_INDEX_URL="https://mirrors.tuna.tsinghua.edu.cn/pypi/web/simple"
```

After adding these lines, reload your shell configuration:

```shell
source ~/.zshrc  # or source ~/.bash_profile
```

## References

- [Tsinghua Homebrew Mirror Guide](https://mirrors.tuna.tsinghua.edu.cn/help/homebrew/)
