add_executable(EngineUnitTests
    unit/ModelDataTests.cpp
)

target_link_libraries(EngineUnitTests
    PRIVATE
        Engine
)

target_compile_features(EngineUnitTests PRIVATE cxx_std_20)

add_test(NAME Engine.Unit.ModelData COMMAND EngineUnitTests)

add_executable(EngineRendererBackendSelectionTests
    unit/RendererBackendSelectionTests.cpp
)

target_link_libraries(EngineRendererBackendSelectionTests
    PRIVATE
        Engine
)

target_compile_features(EngineRendererBackendSelectionTests PRIVATE cxx_std_20)

add_test(NAME Engine.Unit.RendererBackendSelection COMMAND EngineRendererBackendSelectionTests)

add_executable(EngineIntegrationTests
    integration/FbxLoaderIntegrationTests.cpp
)

target_link_libraries(EngineIntegrationTests
    PRIVATE
        Engine
)

target_compile_features(EngineIntegrationTests PRIVATE cxx_std_20)

target_compile_definitions(EngineIntegrationTests
    PRIVATE
        ENGINE_TEST_PROJECT_ROOT="${CMAKE_SOURCE_DIR}"
)

add_test(NAME Engine.Integration.FbxLoader COMMAND EngineIntegrationTests)

if(ENGINE_BUILD_HUMAN_DEVELOPER_TESTS)
    add_subdirectory(HumanDeveloperTests)
endif()
