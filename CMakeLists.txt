cmake_minimum_required(VERSION 3.12.4)
project(lvgl C CXX)

# Apple ld warns when the LVGL static cycle (lvgl <-> thorvg) is repeated
# on the link line. The link already succeeds; this only drops the noise.
if(APPLE)
    add_link_options(-Wl,-no_warn_duplicate_libraries)
endif()

option(PO_UI_CORE_ONLY "Build shared UI software tests without desktop/vector dependencies" OFF)
if(PO_UI_CORE_ONLY)
    if(USE_FREERTOS OR LVGL_PRO_PROJECT_DIR)
        message(FATAL_ERROR "PO_UI_CORE_ONLY validates the bundled UI without FreeRTOS or replacement projects")
    endif()
    set(LV_BUILD_CONF_PATH "${PROJECT_SOURCE_DIR}/config/lv_conf_core.h" CACHE PATH "Core test config" FORCE)
    set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
    set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(CONFIG_LV_USE_THORVG OFF CACHE BOOL "" FORCE)
    set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
    set(LV_FETCH_DEPENDENCIES OFF CACHE BOOL "" FORCE)
    set(CMAKE_C_STANDARD 99)
    include(CTest)
    add_subdirectory(lvgl)
    add_subdirectory(ui product-ui)
    return()
endif()

# Set the correct FreeRTOS port for your system (e.g., Posix for WSL)
set(FREERTOS_PORT GCC_POSIX CACHE STRING "Port for FreeRTOS on Posix environment")

set(LVGL_PRO_PROJECT_DIR
    ""
    CACHE PATH "Path to LVGL Pro Project folder")

option(USE_FREERTOS "Enable FreeRTOS" OFF) # Turn this on to enable FreeRTOS

if(USE_FREERTOS)
    message(STATUS "FreeRTOS is enabled")

    # FreeRTOS integration when USE_FREERTOS is enabled
    add_library(freertos_config INTERFACE)
    target_include_directories(freertos_config SYSTEM INTERFACE ${PROJECT_SOURCE_DIR}/config)
    target_compile_definitions(freertos_config INTERFACE projCOVERAGE_TEST=0)

    # Add FreeRTOS as a subdirectory
    add_subdirectory(FreeRTOS)

    # FreeRTOS-specific include directories
    include_directories(${PROJECT_SOURCE_DIR}/FreeRTOS/include)
    include_directories(${PROJECT_SOURCE_DIR}/FreeRTOS/portable/ThirdParty/GCC/Posix)
    include_directories(${PROJECT_SOURCE_DIR}/config)

    # Add FreeRTOS sources
    file(GLOB FREERTOS_SOURCES
        "${PROJECT_SOURCE_DIR}/FreeRTOS/*.c"
        "${PROJECT_SOURCE_DIR}/FreeRTOS/portable/MemMang/heap_4.c"
        "${PROJECT_SOURCE_DIR}/FreeRTOS/portable/ThirdParty/GCC/Posix/*.c"
    )
else()
    message(STATUS "FreeRTOS is disabled")
    set(FREERTOS_SOURCES "")  # No FreeRTOS sources if FreeRTOS is disabled
endif()

# Main include files of the project
include_directories(${PROJECT_SOURCE_DIR}/main/inc)

# Define options for LVGL with default values (OFF)
option(LV_USE_DRAW_SDL "Use SDL draw unit" OFF)
option(LV_USE_LIBPNG "Use libpng to decode PNG" OFF)
option(LV_USE_LIBJPEG_TURBO "Use libjpeg turbo to decode JPEG" OFF)
option(LV_USE_FFMPEG "Use libffmpeg to display video using lv_ffmpeg" OFF)
option(LV_USE_FREETYPE "Build with the UI submodule's pinned FreeType backend" ON)
if(USE_FREERTOS AND LV_USE_FREETYPE)
    message(STATUS "Disabling LV_USE_FREETYPE for the FreeRTOS build")
    set(LV_USE_FREETYPE OFF)
endif()
if(LV_USE_FREETYPE)
    # The simulator links the pinned UI archive instead of LVGL's dependency resolver.
    set(CONFIG_LV_USE_FREETYPE OFF)
endif()

# Set C and C++ standards
set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set the output path for the executable
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)
set(WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})

# Find and include SDL2 library
find_package(SDL2 REQUIRED)

# Add compile definitions based on the selected options
add_compile_definitions($<$<BOOL:${LV_USE_DRAW_SDL}>:LV_USE_DRAW_SDL=1>)
add_compile_definitions($<$<BOOL:${LV_USE_LIBPNG}>:LV_USE_LIBPNG=1>)
add_compile_definitions($<$<BOOL:${LV_USE_LIBJPEG_TURBO}>:LV_USE_LIBJPEG_TURBO=1>)
add_compile_definitions($<$<BOOL:${LV_USE_FFMPEG}>:LV_USE_FFMPEG=1>)

# Add LVGL subdirectory
add_subdirectory(lvgl)
target_include_directories(lvgl PUBLIC ${PROJECT_SOURCE_DIR} ${SDL2_INCLUDE_DIRS})

set(MAIN_SOURCES src/mouse_cursor_icon.c src/hal/hal.c src/hal/frame_stats.c
    src/simulator_preferences.c src/simulator_options.c src/simulator_capture.c
    src/simulator_start.c src/simulator_session.c
    src/simulator_product.c src/simulator_product_ready.c src/simulator_product_brew.c
    src/simulator_product_m2.c src/simulator_product_settings.c)
set(MAIN_LIBS lvgl lvgl::examples lvgl::demos lvgl::thorvg ${SDL2_LIBRARIES})

# Create the main executable, depending on the FreeRTOS option
if(USE_FREERTOS)
    list(APPEND MAIN_SOURCES src/freertos_main.c src/freertos/freertos_posix_port.c ${FREERTOS_SOURCES})
    list(APPEND MAIN_LIBS freertos_config freertos_kernel)
else()
    list(APPEND MAIN_SOURCES src/main.c)
endif()


if(NOT MSVC)
    list(APPEND MAIN_LIBS m pthread)
endif()

# Custom target to run the executable
add_custom_target(run COMMAND ${EXECUTABLE_OUTPUT_PATH}/main DEPENDS main)

# Conditionally include and link SDL2_image if LV_USE_DRAW_SDL is enabled
if(LV_USE_DRAW_SDL)
    set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake")
    find_package(SDL2_image REQUIRED)
    target_include_directories(lvgl PUBLIC ${SDL2_IMAGE_INCLUDE_DIRS})
    list(APPEND MAIN_LIBS ${SDL2_IMAGE_LIBRARIES})
endif()

# Conditionally include and link libpng if LV_USE_LIBPNG is enabled
if(LV_USE_LIBPNG)
    find_package(PNG REQUIRED)
    target_include_directories(lvgl PUBLIC ${PNG_INCLUDE_DIRS})
    list(APPEND MAIN_LIBS ${PNG_LIBRARIES})
endif()

# Conditionally include and link libjpeg-turbo if LV_USE_LIBJPEG_TURBO is enabled
if(LV_USE_LIBJPEG_TURBO)
    find_package(JPEG REQUIRED)
    target_include_directories(lvgl PUBLIC ${JPEG_INCLUDE_DIRS})
    list(APPEND MAIN_LIBS ${JPEG_LIBRARIES})
endif()

# Conditionally include and link FFmpeg libraries if LV_USE_FFMPEG is enabled
if(LV_USE_FFMPEG)
    list(APPEND MAIN_LIBS avformat avcodec avutil swscale z)
endif()

include(CTest)

set(PRODUCT_UI_SOURCE_DIR "${PROJECT_SOURCE_DIR}/ui")
if(LVGL_PRO_PROJECT_DIR)
  if(EXISTS "${LVGL_PRO_PROJECT_DIR}/CMakeLists.txt")
    get_filename_component(PRODUCT_UI_SOURCE_DIR "${LVGL_PRO_PROJECT_DIR}" ABSOLUTE)
    message(STATUS "Using LVGL Pro project from: ${PRODUCT_UI_SOURCE_DIR}")
  else()
    message(WARNING
      "LVGL_PRO_PROJECT_DIR specified but no CMakeLists.txt found at: ${LVGL_PRO_PROJECT_DIR}; using ui submodule")
  endif()
endif()

if(NOT EXISTS "${PRODUCT_UI_SOURCE_DIR}/CMakeLists.txt")
  message(FATAL_ERROR "Product UI CMake project not found at ${PRODUCT_UI_SOURCE_DIR}; initialize the ui submodule or set LVGL_PRO_PROJECT_DIR")
endif()

if(LV_USE_FREETYPE)
  if(NOT DEFINED PO_UI_FREETYPE_TRUETYPE_ONLY)
    set(PO_UI_FREETYPE_TRUETYPE_ONLY ON CACHE BOOL "Build FreeType with only the TrueType modules needed by the product UI")
  endif()
  include("${PRODUCT_UI_SOURCE_DIR}/cmake/PoUiFreeType.cmake")
  target_compile_definitions(lvgl PUBLIC LV_USE_FREETYPE=1)
  cmake_policy(PUSH)
  cmake_policy(SET CMP0079 NEW)
  target_link_libraries(lvgl PUBLIC $<BUILD_INTERFACE:freetype>)
  cmake_policy(POP)
  if(NOT DEFINED PO_UI_FREETYPE_READOUTS AND NOT PO_UI_READY_ONLY AND
     NOT PO_UI_BOOT_LANGUAGE_ONLY AND NOT PO_UI_RESOURCE_PROTOTYPE)
    set(PO_UI_FREETYPE_READOUTS ON CACHE BOOL "Use FreeType for text fonts and migrated readouts")
  endif()
  if(PO_UI_FREETYPE_READOUTS AND NOT PO_UI_FREETYPE_TEXT_SOURCE)
    set(default_oppo_font "${PRODUCT_UI_SOURCE_DIR}/assets/fonts/OPPO_Sans_4.0/OPPO Sans 4.0.ttf")
    set(default_oppo_license "${PRODUCT_UI_SOURCE_DIR}/assets/fonts/OPPO_Sans_4.0/OPPO Sans 4.0 License Notice.txt")
    if(EXISTS "${default_oppo_font}" AND EXISTS "${default_oppo_license}")
      set(fonttools_python "")
      if(Python3_EXECUTABLE)
        execute_process(COMMAND "${Python3_EXECUTABLE}" -c "import fontTools"
            RESULT_VARIABLE fonttools_status OUTPUT_QUIET ERROR_QUIET)
        if(fonttools_status EQUAL 0)
          set(fonttools_python "${Python3_EXECUTABLE}")
        endif()
      else()
        foreach(candidate "/usr/bin/python3" "python3")
          execute_process(COMMAND "${candidate}" -c "import fontTools"
              RESULT_VARIABLE fonttools_status OUTPUT_QUIET ERROR_QUIET)
          if(fonttools_status EQUAL 0)
            set(fonttools_python "${candidate}")
            break()
          endif()
        endforeach()
      endif()
      if(fonttools_python)
        set(Python3_EXECUTABLE "${fonttools_python}" CACHE FILEPATH "Python interpreter with fontTools" FORCE)
        set(PO_UI_FREETYPE_TEXT_SOURCE "${default_oppo_font}" CACHE FILEPATH "Source variable text font for the optional product font pack" FORCE)
        set(PO_UI_FREETYPE_TEXT_LICENSE "${default_oppo_license}" CACHE FILEPATH "License for the product text font" FORCE)
        if(NOT PO_UI_FREETYPE_NAME_CORPUS AND EXISTS "${PRODUCT_UI_SOURCE_DIR}/tests/font_corpus_simulator.txt")
          set(PO_UI_FREETYPE_NAME_CORPUS "${PRODUCT_UI_SOURCE_DIR}/tests/font_corpus_simulator.txt" CACHE FILEPATH "Optional UTF-8 names to include in the product font pack" FORCE)
        endif()
      endif()
    endif()
  endif()
endif()

if(PRODUCT_UI_SOURCE_DIR STREQUAL "${PROJECT_SOURCE_DIR}/ui")
  add_subdirectory(ui product-ui)
else()
  add_subdirectory("${PRODUCT_UI_SOURCE_DIR}" product-ui)
endif()
if(NOT TARGET lib-ui)
  message(FATAL_ERROR "The product UI project must define the lib-ui target")
endif()
list(APPEND MAIN_LIBS lib-ui)
target_compile_definitions(lib-ui PUBLIC LV_LVGL_H_INCLUDE_SIMPLE)
target_include_directories(lib-ui PUBLIC
  "${PRODUCT_UI_SOURCE_DIR}"
  "${PRODUCT_UI_SOURCE_DIR}/include"
  "${PROJECT_SOURCE_DIR}/lvgl"
)

if(PO_UI_FREETYPE_READOUTS)
  include(cmake/simulator_freetype.cmake)
  list(APPEND MAIN_LIBS simulator_freetype)
endif()

add_executable(main ${MAIN_SOURCES})
target_compile_definitions(main PRIVATE LV_CONF_INCLUDE_SIMPLE)
target_include_directories(main PRIVATE
  "${PRODUCT_UI_SOURCE_DIR}"
  "${PRODUCT_UI_SOURCE_DIR}/include"
)
target_link_libraries(main ${MAIN_LIBS})
if(TARGET lib-ui-resource-stdio)
  target_link_libraries(main lib-ui-resource-stdio)
endif()

if(BUILD_TESTING AND UNIX)
  add_executable(simulator_session_test tests/simulator_session_test.c src/simulator_session.c)
  target_include_directories(simulator_session_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
  add_test(NAME simulator_session COMMAND simulator_session_test)

  add_executable(simulator_preferences_test src/simulator_preferences.c)
  target_compile_definitions(simulator_preferences_test PRIVATE SIMULATOR_PREFERENCES_TEST)
  target_include_directories(simulator_preferences_test PRIVATE
    "${PROJECT_SOURCE_DIR}/src"
    "${PRODUCT_UI_SOURCE_DIR}/include"
  )
  target_link_libraries(simulator_preferences_test PRIVATE lib-ui)
  add_test(NAME simulator_preferences COMMAND simulator_preferences_test)

  add_executable(simulator_options_test src/simulator_options.c src/simulator_preferences.c)
  target_compile_definitions(simulator_options_test PRIVATE SIMULATOR_OPTIONS_TEST)
  target_include_directories(simulator_options_test PRIVATE "${PRODUCT_UI_SOURCE_DIR}/include")
  target_link_libraries(simulator_options_test PRIVATE lib-ui)
  add_test(NAME simulator_options COMMAND simulator_options_test)

  add_executable(simulator_start_test src/simulator_start.c)
  target_compile_definitions(simulator_start_test PRIVATE LV_CONF_INCLUDE_SIMPLE SIMULATOR_START_TEST)
  target_link_libraries(simulator_start_test PRIVATE lvgl lib-ui ${SDL2_LIBRARIES})
  add_test(NAME simulator_start COMMAND simulator_start_test)
  set_tests_properties(simulator_start PROPERTIES TIMEOUT 5)

  add_executable(simulator_ready_debug_test
    tests/simulator_ready_debug_test.c
    src/simulator_session.c)
  target_compile_definitions(simulator_ready_debug_test PRIVATE LV_CONF_INCLUDE_SIMPLE)
  target_include_directories(simulator_ready_debug_test PRIVATE
    "${PRODUCT_UI_SOURCE_DIR}"
    "${PRODUCT_UI_SOURCE_DIR}/include")
  target_link_libraries(simulator_ready_debug_test PRIVATE ${MAIN_LIBS})
  add_test(NAME simulator_ready_debug COMMAND simulator_ready_debug_test)

  add_executable(simulator_host_startup_test
    tests/simulator_host_startup_test.c
    src/simulator_options.c
    src/simulator_preferences.c
    src/simulator_session.c)
  target_compile_definitions(simulator_host_startup_test PRIVATE
    LV_CONF_INCLUDE_SIMPLE SIMULATOR_HOST_STARTUP_TEST)
  target_include_directories(simulator_host_startup_test PRIVATE
    "${PRODUCT_UI_SOURCE_DIR}"
    "${PRODUCT_UI_SOURCE_DIR}/include")
  target_link_libraries(simulator_host_startup_test PRIVATE ${MAIN_LIBS})
  add_test(NAME simulator_host_startup COMMAND simulator_host_startup_test)
  set_tests_properties(simulator_host_startup PROPERTIES TIMEOUT 5)

  add_executable(simulator_product_test
    tests/simulator_product_test.c
    src/simulator_options.c
    src/simulator_preferences.c
    src/simulator_session.c)
  target_compile_definitions(simulator_product_test PRIVATE
    LV_CONF_INCLUDE_SIMPLE SIMULATOR_PRODUCT_TEST)
  target_include_directories(simulator_product_test PRIVATE
    "${PRODUCT_UI_SOURCE_DIR}"
    "${PRODUCT_UI_SOURCE_DIR}/include")
  target_link_libraries(simulator_product_test PRIVATE ${MAIN_LIBS})
  add_test(NAME simulator_product COMMAND simulator_product_test)
  set_tests_properties(simulator_product PROPERTIES TIMEOUT 10)
endif()

# On Windows, GUI applications do not show a console by default,
# which hides log output. This ensures a console is available for logging.
if (WIN32)
    if (MSVC)
        target_link_options(main PRIVATE "/SUBSYSTEM:CONSOLE")
    else()
        target_link_options(main PRIVATE "-mconsole")
    endif()
endif()

# Apply additional compile options if the build type is Debug
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(STATUS "Debug mode enabled")

    target_compile_options(lvgl PRIVATE
        -pedantic-errors
        -Wall
        -Wclobbered
        -Wdeprecated
        -Wdouble-promotion
        -Wempty-body
        -Wextra
        -Wformat-security
        -Wmaybe-uninitialized
        # -Wmissing-prototypes
        -Wpointer-arith
        -Wmultichar
        -Wno-pedantic # ignored for now, we convert functions to pointers for properties table.
        -Wreturn-type
        -Wshadow
        -Wshift-negative-value
        -Wsizeof-pointer-memaccess
        -Wtype-limits
        -Wundef
        -Wuninitialized
        -Wunreachable-code
        -Wfloat-conversion
        -Wstrict-aliasing
    )

if (ASAN)
    message(STATUS "AddressSanitizer enabled")

    # Add AddressSanitizer flags
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -fno-omit-frame-pointer")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address -fno-omit-frame-pointer")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -fsanitize=address")
else()
    message(STATUS "AddressSanitizer disabled")
endif()
endif()
