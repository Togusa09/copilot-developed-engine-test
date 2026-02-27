add_executable(Sandbox
    src/main.cpp
)

target_link_libraries(Sandbox
    PRIVATE
        Engine
)

target_compile_features(Sandbox PRIVATE cxx_std_20)

add_custom_command(TARGET Sandbox POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/src/Engine/shaders
        $<TARGET_FILE_DIR:Sandbox>/shaders
)
