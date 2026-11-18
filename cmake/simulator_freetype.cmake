include("${PRODUCT_UI_SOURCE_DIR}/cmake/PoUiFreeType.cmake")
target_compile_definitions(lvgl PUBLIC LV_USE_FREETYPE=1)
cmake_policy(PUSH)
cmake_policy(SET CMP0079 NEW)
target_link_libraries(lvgl PUBLIC $<BUILD_INTERFACE:freetype>)
cmake_policy(POP)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(font_assets "${PRODUCT_UI_SOURCE_DIR}/assets/freetype")
set(font_generator "${PRODUCT_UI_SOURCE_DIR}/tools/generate_freetype_assets.py")
set(font_roles "${CMAKE_CURRENT_BINARY_DIR}/freetype_roles.inc")
add_custom_command(OUTPUT "${font_roles}"
    COMMAND "${Python3_EXECUTABLE}" "${font_generator}"
            --check "${font_assets}" --write-roles-header "${font_roles}"
    DEPENDS "${font_generator}" "${font_assets}/manifest.json"
            "${font_assets}/fonts/oppo.ttf" "${font_assets}/fonts/geist.ttf"
            "${font_assets}/licenses/oppo.txt" "${font_assets}/licenses/geist.txt"
            "${PRODUCT_UI_SOURCE_DIR}/assets/file_fonts/manifest.json"
            "${PRODUCT_UI_SOURCE_DIR}/assets/texts.json"
    VERBATIM)

add_library(simulator_freetype STATIC src/simulator_fonts.c "${font_roles}")
target_compile_definitions(simulator_freetype PUBLIC SIMULATOR_SHARED_FREETYPE=1
    PRIVATE "SIMULATOR_FREETYPE_ASSETS=\"${font_assets}\"")
target_include_directories(simulator_freetype PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(simulator_freetype PRIVATE lvgl lib-ui)
target_link_libraries(main simulator_freetype)

if(BUILD_TESTING)
    add_executable(simulator_freetype_test tests/simulator_freetype_test.c)
    target_include_directories(simulator_freetype_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
    target_link_libraries(simulator_freetype_test PRIVATE simulator_freetype lvgl lib-ui)
    add_test(NAME simulator_freetype COMMAND simulator_freetype_test)
endif()
