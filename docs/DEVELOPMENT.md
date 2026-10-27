# 产品 UI 开发与双电脑同步

## 仓库与当前边界

- 模拟器：`Ianxu668/pourover_v1_lvgl_macos`，产品分支 `product/macos-v9.6`。
- LVGL：`lvgl/` 子模块，指向 `Ianxu668/lvgl` 的固定提交，含 ThorVG 内存释放修复。
- 产品 UI：`ui/` 子模块，私有仓库 `Ianxu668/pourover_ui`。
- 固件：独立的 `pour_over_scale_v2`，本次未接入 UI 子模块、未修改源码或 SDK。

模拟器根工程默认通过 `add_subdirectory(ui product-ui)` 构建 UI 子模块的 `lib-ui`，入口显示产品 Boot 和首次语言设置页；页面实现与静态资源只维护在 UI 私有仓库。首次运行或偏好无效时，模拟器以英语启动语言设置页（Boot 预览延迟 1500 ms，仅为桌面流程，不是硬件时序）。有效偏好会被读取并跳过语言页；由于下一产品页尚未集成，模拟器继续停留在 Boot，不自行补造 Home 页面。

产品页面、样式、字体、图片应保存在 UI 私有仓库。SDL、模拟数据留在模拟器；真实传感器、电源、RTOS、显示/触摸驱动留在固件。不要把产品源码、私有设计或资源提交到公开的模拟器/LVGL Fork。

## 另一台 Mac 首次准备

先配置 GitHub 凭据，确认当前账号能访问私有 UI 仓库，再执行：

```sh
git clone --recurse-submodules --branch product/macos-v9.6 \
  https://github.com/Ianxu668/pourover_v1_lvgl_macos.git
cd pourover_v1_lvgl_macos
```

需要 Xcode Command Line Tools 或 Xcode，以及 Homebrew 的 LLVM、CMake、Ninja 和 SDL2。缺少依赖时安装：

```sh
brew install llvm cmake ninja sdl2
```

首次配置并构建：

```sh
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER="$(brew --prefix llvm)/bin/clang" \
  -DCMAKE_CXX_COMPILER="$(brew --prefix llvm)/bin/clang++" \
  -DCMAKE_PREFIX_PATH="$(brew --prefix)" \
  -DUSE_FREERTOS=OFF
cmake --build build --parallel 8
ctest --test-dir build --output-on-failure
./bin/main
```

预览及截图示例：

```sh
./bin/main --screen boot
./bin/main --screen language --language ja
./bin/main --screen language --screenshot /tmp/language.ppm
```

模拟器将确认后的语言保存到 SDL 的 `Mistro/PourOverScale` 偏好目录；命令行 `--language` 只提供临时初始选择。默认 UI 由私有 `ui/` 子模块提供。如需用兼容的 LVGL Pro 工程替代该子模块，可设置 `-DLVGL_PRO_PROJECT_DIR=<path>`；指定工程需提供 `lib-ui` target 及相同的 `pourover_ui.h` 启动接口。`build/`、`bin/` 不提交或跨电脑复制。每台电脑独立配置；切换编译器时使用新的构建目录。

## FreeRTOS 可选模式（保留上游说明）

当前产品开机/语言流程只验证了 `USE_FREERTOS=OFF`。工程仍保留上游 FreeRTOS 示例入口；它不是本阶段产品流程，不代表 BK7258 固件的 RTOS 集成。

上游启用条件是将 `lv_conf.h` 的 `LV_USE_OS` 改为 `LV_OS_FREERTOS`，并在独立构建目录配置 `-DUSE_FREERTOS=ON`；关闭时两处都恢复，不能只切一个开关。相关宿主配置位于 `config/FreeRTOSConfig.h`。上游曾建议为 POSIX/SDL 示例配置 **512 MiB** 的 `configTOTAL_HEAP_SIZE`，以避免堆不足或窗口出现缓慢；这是桌面示例的经验配置，不是产品 RAM 要求，也不用于普通 SDL 模式。

此模式未做本次 macOS/LLVM 编译或运行验证；不要把保留的选项当作已支持的产品启动方式。

## 日常同步

开始前先确认父工程和子模块没有未提交改动，存在改动时先自行保存，不使用强制更新覆盖：

```sh
git status --short
git submodule foreach --recursive 'git status --short'
git pull --ff-only
git submodule update --init --recursive
```

普通子模块更新恢复的是父工程锁定的提交，可能处于 detached HEAD。修改 UI 前先进入 `ui/`，切到开发分支；修改已锁定版本时也可用 `git switch -c <功能分支>` 从该提交创建分支。不要直接在 detached HEAD 上长期开发。

提交顺序：

1. 在 `ui/` 中提交并推送相关 UI 文件；需要合并时先完成 UI 分支合并。
2. 返回模拟器根目录，确认 `ui/` 正是已推送、已验证的提交。
3. `git add ui`，提交并推送父工程的版本指针。
4. 另一台电脑拉取父工程，再执行 `git submodule update --init --recursive`。

切换电脑前把子模块和父工程都推送完成。两台电脑并行开发使用不同功能分支；UI 不会因父工程 `git pull` 而自动升级到最新开发版本。

## 官方更新

新克隆只有 `origin`；需要获取官方更新时，先添加官方远端一次：

```sh
git remote add upstream https://github.com/lvgl/lv_port_pc_vscode.git
```

工作区干净时，在维护分支合并、验证：

```sh
git fetch upstream
git switch product/macos-v9.6
git switch -c maintenance/sync-v9.6
git merge upstream/release/v9.6
```

先检查 `.gitmodules` 和 `lvgl` 提交指针，避免官方更新使本地依赖回退或丢失 Fork 修复。确认后执行 `git submodule sync --recursive`、`git submodule update --init --recursive`，重新编译运行，再合入产品分支。

日常不要使用 `git submodule update --remote --recursive` 自动升级所有依赖。LVGL 升级与固件兼容性验证是单独任务；桌面运行成功不代表实板验收通过。
