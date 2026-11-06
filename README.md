# PourOver Scale macOS LVGL simulator

This repository is the desktop simulator for the PourOver Scale UI. Work on branch `product/macos-v9.6`; clone with submodules because the product UI is in the private `ui/` repository. The simulator integrates the UI's `lib-ui` target and provides boot, language selection, and a mode 1 Ready preview; it does not invent product pages that are not yet integrated.

For setup and safe UI/submodule synchronization, see [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md).

## Build and run

Install Xcode Command Line Tools (`xcode-select --install`) and [Homebrew](https://brew.sh), with `brew` available on `PATH`. Clone with access to the private UI repository, then use the two scripts:

```sh
git clone --recurse-submodules --branch product/macos-v9.6 \
  https://github.com/Ianxu668/pourover_v1_lvgl_macos.git
cd pourover_v1_lvgl_macos
./setup.sh  # Install missing LLVM/CMake/Ninja/SDL2, initialize submodules, configure
./run.sh    # Incremental build with 8 jobs, then launch bin/main

# Arguments are forwarded to the simulator:
./run.sh --screen language --language zh-Hans

# Optional tests (not run on every launch):
ctest --test-dir build --output-on-failure
sh tests/build_scripts_test.sh
```

Both scripts work from any working directory. `setup.sh` configures `build/` as Release with Ninja, Homebrew LLVM, FreeRTOS off, and the bundled `ui/` project; it installs only missing formulae, without requesting upgrades. It initializes missing submodules at pinned revisions and stops if an initialized submodule has a different commit or a conflict; it does not force checkout or update to remote HEAD. `run.sh` requires setup first, stops if the build fails, and does not reinstall dependencies. Existing build caches are not deleted; use a separate build directory manually if changing compiler or generator.

To build against a different LVGL Pro project, set `LVGL_PRO_PROJECT_DIR`; it is used instead of the default `ui/` submodule and must define the `lib-ui` target **and implement the same `pourover_ui.h` startup API** (an arbitrary LVGL Pro export is not sufficient):

```sh
cmake -S . -B build -DLVGL_PRO_PROJECT_DIR=/path/to/ui-project
```

`simulator_preferences` currently stores only the selected language, not window size or position. The simulator reads and saves the selected locale as `language` under SDL's `Mistro/PourOverScale` preference directory. A valid saved locale is loaded before the UI starts and is the language page's initial selection. An interactive launch always plays the boot logo, then opens language selection. With no valid preference the selection starts in English. Confirming the page saves the locale and advances to mode 1 ready. `--screenshot` without `--screen` still captures the boot hold when a valid locale is already saved, and captures the language page when it is not.

Useful preview and capture commands:

```sh
./bin/main --screen boot
./bin/main --screen language
./bin/main --screen language --language zh-Hans
./bin/main --screen ready --language en
./bin/main --screen ready --language zh-Hans
./bin/main --screenshot /tmp/pourover-language.ppm --screen language --language zh-Hans
```

`--screen boot` holds the boot screen. `--screen language` opens the language screen even when a preference exists, without clearing or rewriting it. `--screen ready` opens the mode 1 Ready baseline. The active locale catalog is English (`en`) and Simplified Chinese (`zh-Hans`); `--language` accepts either tag and selects the temporary locale for both the language and built-in Ready previews. Unsupported historical preferences fall back to English and are not overwritten until the user confirms a selection. That page is one snapshot: the simulator keeps a `po_ready_view_t` and calls `po_ui_ready_set()` for later changes. On the Ready screen the keys are `C` link, `H` charge, `[` `]` battery, `T` manual tare, `A` matched container, `N` unmatched tare, `O` overload, `R` reject, `U` relabel, up/down for net weight, left/right for sheet-key dispatch when open (right also tares), otherwise tare, and `1`-`4` for layout scenarios. `U` changes the unit label only; the page does not convert grams to ounces. A note stays about 1.6 seconds, then the simulator clears it. The PC start screen has a theme button. `cmake -DPO_UI_THEME_LIGHT=ON` selects the light ink as the compile default; the button can still switch before the first product page. A confirmed language page advances to Ready on the next loop. `--screenshot` writes a binary PPM (`P6`) image of the rendered screen and exits; it is useful for automated capture where the SDL backend can initialize.

## Simulator and device context

The simulator and firmware are confirmed on LVGL 9.6; this does not validate firmware source consumers or a firmware build for the locale API migration. The simulator uses the SDL software renderer and RGB565 display format. It renders the device's logical landscape canvas at **960 × 376 pixels**, without desktop DPI scaling or canvas rotation. The device display scans at 376 × 960 pixels and is specified at 360 PPI, approximately 67.68 × 26.51 mm in landscape. The display is HD286001C40, the touch controller is CST3240, and the target SoC is BK7258 AP/CP. The firmware uses a fixed Armino SDK revision (`398cab28b938e69ba2071a597daf00845dd080b6`). The device has 8 MB Flash total; PSRAM is expected to be 8–16 MB but remains undecided. Design estimates against the 8 MB PSRAM lower bound; neither capacity is a UI/resource allocation budget.

The design source is `PourOverScale_UI.pen`, accessed through Pencil MCP. Implemented references are **`VDHEl`** (boot), **`ow8fm`** (snapped language choice), and **`s6OG7D`** (dragging); the latter two are states of the same page, not separate navigation destinations.

The current Boot and P02 language screens follow the v0.6 interaction contract and Mistro Ink/Frost visual palette (spec 16 v1.1). The active locale catalog offers English (`en`) and Simplified Chinese (`zh-Hans`); six legacy artwork families are retained but are not selectable locales. Scrolling and choosing a language are previews; the localized panel updates after the selection settles, and releasing a drag does not confirm. Only Continue saves the selection. A 114-pixel touch reference corresponds to about 8 mm on the device, but is not a universal target-size rule.

UI artwork and page implementation belong in the private UI submodule, not this simulator repository. OPPO Sans 4 use is confirmed free for commercial use; font-file processing and independent distribution terms remain a release check. The built-in simulator path uses static A8 phrase assets; a separate opt-in Ready preview accepts only controlled local resource packs and is not the normal product font backend. Firmware storage and on-device rendering/touch behavior still require hardware validation.
