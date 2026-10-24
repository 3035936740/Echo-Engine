if(NOT WIN32)
  message(FATAL_ERROR "The desktop backend targets Windows only")
endif()
enable_language(RC)
add_executable(EchoOfKino WIN32 assets/resource.rc src/main.cpp src/input_ui.cpp src/rewind_ui.cpp src/collision_ui.cpp src/snapshot_ui.cpp src/focus_ui.cpp src/renderer.cpp src/audio.cpp src/workspace_ui.cpp src/editor_ui.cpp src/editor_theme.cpp src/dialogue_ui.cpp src/audio_ui.cpp src/map_ui.cpp src/transition_ui.cpp src/barrage_ui.cpp src/animation_ui.cpp src/assets_ui.cpp src/objects_ui.cpp src/authoring_ui.cpp src/health_ui.cpp src/layer_ui.cpp src/event_ui.cpp src/event_authoring_ui.cpp src/choice_ui.cpp src/mobility_ui.cpp src/tile_ui.cpp src/editor_commands.cpp src/verify.cpp src/ui.cpp src/workspace_view.cpp src/inspector_ui.cpp src/ui_tour.cpp)
target_link_libraries(EchoOfKino PRIVATE echo_project echo_audio_decode opengl32 gdi32 user32 winmm comdlg32 shell32 comctl32 uxtheme windowscodecs ole32 uuid)
target_compile_definitions(EchoOfKino PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(EchoOfKino PRIVATE -Wall -Wextra -Wpedantic)
target_link_options(EchoOfKino PRIVATE -static -static-libgcc -static-libstdc++)
set_target_properties(EchoOfKino PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/runtime")

add_custom_command(TARGET EchoOfKino POST_BUILD
  COMMAND ${CMAKE_COMMAND} "-DRUNTIME=$<TARGET_FILE:EchoOfKino>" "-DSOURCE=${CMAKE_SOURCE_DIR}" -P ${CMAKE_SOURCE_DIR}/tools/deploy_runtime.cmake
  VERBATIM
)

target_include_directories(EchoOfKino PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/assets")

# Offscreen render of the drawn editor chrome with mock state (GDI only, also runs under Wine):
#   cmake --build --preset gcc-release --target echo_ui_preview
#   echo_ui_preview <tiles.png> <game-view.png | -> <output-folder>
add_executable(echo_ui_preview EXCLUDE_FROM_ALL ${CMAKE_SOURCE_DIR}/tools/ui_preview.cpp src/ui.cpp src/workspace_view.cpp)
target_include_directories(echo_ui_preview PRIVATE include)
target_compile_definitions(echo_ui_preview PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(echo_ui_preview PRIVATE -Wall -Wextra -municode)
target_link_options(echo_ui_preview PRIVATE -municode -static)
target_link_libraries(echo_ui_preview PRIVATE gdi32 user32 ole32 windowscodecs uuid)
