if(NOT WIN32)
  message(FATAL_ERROR "The desktop backend targets Windows only")
endif()
enable_language(RC)
add_executable(EchoOfKino WIN32 assets/resource.rc src/main.cpp src/input_ui.cpp src/rewind_ui.cpp src/collision_ui.cpp src/snapshot_ui.cpp src/focus_ui.cpp src/renderer.cpp src/audio.cpp src/workspace_ui.cpp src/editor_ui.cpp src/editor_theme.cpp src/dialogue_ui.cpp src/audio_ui.cpp src/map_ui.cpp src/transition_ui.cpp src/barrage_ui.cpp src/animation_ui.cpp src/assets_ui.cpp src/objects_ui.cpp src/authoring_ui.cpp src/health_ui.cpp src/layer_ui.cpp src/event_ui.cpp src/event_authoring_ui.cpp src/choice_ui.cpp src/mobility_ui.cpp src/tile_ui.cpp src/editor_commands.cpp src/verify.cpp)
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
