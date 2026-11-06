cmake_minimum_required(VERSION 3.24)
project(HS80Control VERSION 2.0.0 LANGUAGES C RC)
if(NOT WIN32 OR NOT MSVC)
 message(FATAL_ERROR "Este recorte usa Windows x64 e MSVC.")
endif()
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_subdirectory(vendor/CUI)
add_library(hs80_common STATIC src/common.c src/protocol.c)
target_include_directories(hs80_common PUBLIC src)
target_compile_definitions(hs80_common PUBLIC UNICODE _UNICODE WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
target_link_libraries(hs80_common PUBLIC advapi32 shell32 user32 psapi)
add_executable(hs80_controller WIN32 src/controller.c src/hid.c src/audio.c src/worker.rc)
set_target_properties(hs80_controller PROPERTIES OUTPUT_NAME HS80-Leds)
target_link_libraries(hs80_controller PRIVATE hs80_common hid setupapi ole32 uuid)
add_executable(hs80_panel WIN32 src/ui.c src/panel.rc)
set_target_properties(hs80_panel PROPERTIES OUTPUT_NAME HS80-Controle)
target_link_libraries(hs80_panel PRIVATE hs80_common cui::cui)
add_executable(hs80_tests tests/protocol_test.c src/protocol.c)
target_include_directories(hs80_tests PRIVATE src)
enable_testing()
add_test(NAME protocol_and_validation COMMAND hs80_tests)
foreach(t hs80_common hs80_controller hs80_panel hs80_tests)
 target_compile_options(${t} PRIVATE /W4 /utf-8 /Gy /Gw /O1)
endforeach()
foreach(t hs80_controller hs80_panel)
 target_link_options(${t} PRIVATE /OPT:REF /OPT:ICF /INCREMENTAL:NO /DYNAMICBASE /NXCOMPAT /HIGHENTROPYVA /MAP)
endforeach()
target_link_options(hs80_panel PRIVATE /ENTRY:mainCRTStartup)
add_custom_target(dist
 COMMAND "${CMAKE_COMMAND}" -E make_directory "${PROJECT_SOURCE_DIR}/dist/licencas"
 COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:hs80_controller>" "${PROJECT_SOURCE_DIR}/dist/HS80-Leds.exe"
 COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:hs80_panel>" "${PROJECT_SOURCE_DIR}/dist/HS80-Controle.exe"
 COMMAND "${CMAKE_COMMAND}" -DHS80_ROOT=${PROJECT_SOURCE_DIR} -P "${PROJECT_SOURCE_DIR}/cmake/PrepareDist.cmake"
 DEPENDS hs80_controller hs80_panel hs80_tests
 COMMENT "Preparando dist com apenas os arquivos de uso"
 VERBATIM)
