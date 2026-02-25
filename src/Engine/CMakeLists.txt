add_library(Engine STATIC
    src/Application.cpp
)

target_include_directories(Engine
    PUBLIC
        include
)

target_compile_features(Engine PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(Engine PRIVATE /W4 /permissive-)
else()
    target_compile_options(Engine PRIVATE -Wall -Wextra -Wpedantic)
endif()
