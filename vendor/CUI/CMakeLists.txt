# HS80-only subset of CUI. Not the complete component SDK.
add_library(cui STATIC
 src/cui_core.c src/cui_theme.c src/cui_draw.c src/cui_text.c
 src/cui_layout.c src/cui_widgets.c src/cui_controls.c src/cui_overlay.c src/cui_textedit.c
 src/cui_font_data.c src/cui_inflate.c src/cui_hs80_memory.c
 src/render/cui_render_sw.c
 src/platform/cui_app.c src/platform/cui_app_win32.c)
add_library(cui::cui ALIAS cui)
target_include_directories(cui PUBLIC include)
target_compile_definitions(cui PRIVATE CUI_HS80_SOFTWARE_ONLY CUI_HS80_ESSENTIALS CUI_NO_STB_IMAGE _CRT_SECURE_NO_WARNINGS)
target_compile_options(cui PRIVATE /Gy /Gw /W4 /wd4100 /wd4201 /wd4204 /wd4221 /wd4244 /wd4267 /wd4305 /wd4996 /utf-8)
target_link_libraries(cui PUBLIC user32 gdi32 shell32 imm32 dwmapi)
