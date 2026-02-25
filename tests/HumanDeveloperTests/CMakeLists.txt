add_executable(HumanDeveloperTests
    src/main.cpp
)

target_compile_features(HumanDeveloperTests PRIVATE cxx_std_20)

add_test(NAME HumanDeveloper.Scaffold COMMAND HumanDeveloperTests)
