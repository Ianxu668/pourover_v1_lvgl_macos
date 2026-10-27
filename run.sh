#!/bin/sh
# Incrementally build the simulator and forward all arguments to it.
set -eu
cd "$(dirname "$0")"

if [ ! -f build/CMakeCache.txt ] || ! command -v cmake >/dev/null 2>&1; then
    echo "请先执行 ./setup.sh 初始化编译环境。" >&2
    exit 1
fi
cmake --build build --parallel 8
exec ./bin/main "$@"
