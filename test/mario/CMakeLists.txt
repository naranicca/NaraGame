add_executable(mario src/MarioApp.cpp)
target_link_libraries(mario PRIVATE Nara::nara)
set_target_properties(mario PROPERTIES FOLDER "test")

target_compile_definitions(mario PRIVATE
    NARA_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")
