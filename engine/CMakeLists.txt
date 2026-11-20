# The PEDWAR audio engine. Plain C++17, no JUCE, no plugin code.
# Later plugins can reuse this folder (state-variable core, shapers / limiters, oversampling).

add_library(pedwar_engine STATIC
    src/Engine.cpp
    src/Resampling.cpp
)

target_include_directories(pedwar_engine PUBLIC include)
target_compile_features(pedwar_engine PUBLIC cxx_std_17)

if(PEDWAR_BUILD_TESTS)
    add_executable(pedwar_engine_test tests/engine_test.cpp)
    target_link_libraries(pedwar_engine_test PRIVATE pedwar_engine)
    add_test(NAME engine COMMAND pedwar_engine_test)
endif()
