#!/bin/sh
# Prepare the macOS simulator without changing initialized submodule revisions.
set -eu
cd "$(dirname "$0")"

if [ "$(uname -s)" != Darwin ]; then
    echo "此脚本仅支持 macOS。" >&2
    exit 1
fi
if ! xcode-select -p >/dev/null 2>&1; then
    echo "请先执行 xcode-select --install，完成安装后重新运行此脚本。" >&2
    exit 1
fi
if ! command -v brew >/dev/null 2>&1; then
    echo "请先安装 Homebrew（https://brew.sh），并按安装提示将 brew 加入 PATH。" >&2
    exit 1
fi

submodules=$(git submodule status --recursive)
if printf '%s\n' "$submodules" | grep -Eq '^[+U]'; then
    echo "子模块存在版本偏移或冲突，请自行确认并处理后重试；脚本不会覆盖本地版本。" >&2
    printf '%s\n' "$submodules" >&2
    exit 1
fi
if printf '%s\n' "$submodules" | grep -q '^-'; then
    echo "初始化锁定版本的子模块（需有私有 UI 仓库访问权限）…"
    git submodule update --init --recursive
fi

set --
for formula in llvm cmake ninja sdl2; do
    if ! brew list --versions "$formula" >/dev/null 2>&1; then
        set -- "$@" "$formula"
    fi
done
if [ "$#" -gt 0 ]; then
    HOMEBREW_NO_AUTO_UPDATE=1 brew install "$@"
fi
brew_prefix=$(brew --prefix)
llvm_prefix=$(brew --prefix llvm)
export PATH="$brew_prefix/bin:$PATH"
cmake -S . -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER="$llvm_prefix/bin/clang" \
    -DCMAKE_CXX_COMPILER="$llvm_prefix/bin/clang++" \
    -DCMAKE_PREFIX_PATH="$brew_prefix" \
    -DUSE_FREERTOS=OFF \
    -DPO_UI_CORE_ONLY=OFF \
    -DLVGL_PRO_PROJECT_DIR=
echo "环境配置完成。执行 ./run.sh 编译并运行。"
