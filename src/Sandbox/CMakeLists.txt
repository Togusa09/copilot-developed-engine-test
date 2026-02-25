add_executable(Sandbox
    src/main.cpp
)

target_link_libraries(Sandbox
    PRIVATE
        Engine
)

target_compile_features(Sandbox PRIVATE cxx_std_20)
