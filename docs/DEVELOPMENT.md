# 产品 UI 开发与双电脑同步

## 仓库与当前边界

- 模拟器：`Ianxu668/pourover_v1_lvgl_macos`，产品分支 `product/macos-v9.6`。
- LVGL：`lvgl/` 子模块，指向 `Ianxu668/lvgl` 的固定提交，含 ThorVG 内存释放修复。
- 产品 UI：`ui/` 子模块，私有仓库 `Ianxu668/pourover_ui`。
- 固件：独立的 `pour_over_scale_v2`，本次未接入 UI 子模块、未修改源码或 SDK。

模拟器根工程默认通过 `add_subdirectory(ui product-ui)` 构建 UI 子模块的 `lib-ui`，交互启动在 Boot 之后进入语言选择页；页面实现与静态资源只维护在 UI 私有仓库。宿主登记 Boot 退出请求，等待动画完成自身展示和退出后进入语言选择；已保存的有效语言只作为初始选中项，没有有效偏好时用英语。确认后保存偏好并进入 mode 1 ready。不再以 1500 ms 硬切换。

共享 UI 的页面、组件、状态所有权与后续扩展规则见私有 `ui/docs/ARCHITECTURE.md`。A/G/H 都是候选，默认 H 只是当前预览入口，不代表最终产品选择。

产品页面、样式、字体、图片应保存在 UI 私有仓库。SDL、模拟数据留在模拟器；真实传感器、电源、RTOS、显示/触摸驱动留在固件。不要把产品源码、私有设计或资源提交到公开的模拟器/LVGL Fork。

## 另一台 Mac 首次准备

先配置 GitHub 凭据，确认当前账号能访问私有 UI 仓库，再执行：

```sh
git clone --recurse-submodules --branch product/macos-v9.6 \
  https://github.com/Ianxu668/pourover_v1_lvgl_macos.git
cd pourover_v1_lvgl_macos
```

先安装 Xcode Command Line Tools（`xcode-select --install`）或 Xcode，以及 [Homebrew](https://brew.sh)，并按安装提示将 `brew` 加入 PATH。之后只需两个脚本：

```sh
./setup.sh  # 首次准备环境并生成 build/，依赖变化时可重新执行
./run.sh    # 日常增量编译并运行
./run.sh --screen language --language ja  # 参数直接传给模拟器
```

`setup.sh` 检查 macOS 和 Xcode 工具，仅安装缺失的 Homebrew LLVM、CMake、Ninja、SDL2，不主动升级已有依赖；初始化缺失的子模块（需要私有 UI 仓库权限），再使用 Ninja + Homebrew Clang 配置 Release 构建，关闭 FreeRTOS 和核心测试专用模式，使用默认 `ui/`。如果已初始化的子模块偏离父仓库锁定提交或存在冲突，脚本会停止，不强制切换本地版本。它不会清理已有构建缓存；如果旧缓存使用其他编译器或生成器，应手动使用新构建目录。

`run.sh` 使用 8 个并行任务编译，成功后从项目根目录启动 `bin/main`；失败时不会运行旧产物。普通启动和 `--screen boot` 预览会先显示 PC 调试专用的 **Start Boot** 按钮，点击后才创建并播放开机动画；等待期间不计入动画帧率统计。此按钮只在模拟器宿主层实现，不进入共享 UI 或实际固件。`--screen language` 直接进入语言页，`--screenshot` 自动运行原流程，不等待点击。两个脚本都可以从任意工作目录调用，不需要 source。日常运行不重复安装依赖或执行测试，需要时单独运行：

```sh
ctest --test-dir build --output-on-failure
sh tests/build_scripts_test.sh  # 使用假工具检查脚本，不安装依赖、不打开窗口
```

预览及截图示例：

```sh
./bin/main --screen boot --boot-animation A
./bin/main --screen boot --boot-animation G
./bin/main --screen boot --boot-animation H
./bin/main --screen language --language ja
./bin/main --screen language --screenshot /tmp/language.ppm
```

`--screen boot --screenshot <path.ppm>` 等待所选动画稳定后截取 960×376 逻辑画布，不截取起始黑场/狭缝。更细的关键帧由私有 UI 的无 SDL 测试输出。60 Hz 预览使用 16 ms LVGL 调度并检查 SDL VSYNC 请求；启动日志记录显示模式、drawable 尺寸和入场刷新间隔。日志包含设计停顿及 H 的离散解码节奏，不能将整个入场的 P95/FPS 当作连续运动或实板帧率验收。Release 关闭昂贵的对象/样式/内存完整性检查，Debug 保留它们。

隔离的软件兼容性检查使用独立构建目录，不查询 SDL、不链接 ThorVG，也不复制桌面配置到固件：

```sh
cmake -S . -B build-core -G Ninja -DPO_UI_CORE_ONLY=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-core --parallel 8
ctest --test-dir build-core --output-on-failure
```

该构建运行实际 LVGL 的 RGB565/A8、原生线条、动画/Timeline 和页面测试，不等于 BK7258 交叉编译或实板验收。不要在同一构建目录切换普通/核心配置。

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
