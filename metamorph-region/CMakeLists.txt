basalt_module(metamorph-region
    SOURCES
        src/AnvilRegion.cpp
        src/LinearRegion.cpp
    DEPENDS basalt::wire-common basalt::wire-io
)

basalt_test(metamorph-region-anvil-test
    SOURCES
        test/AnvilRegionTest.cpp
    DEPENDS basalt::metamorph-region
)

basalt_test(metamorph-region-linear-test
    SOURCES
        test/LinearRegionTest.cpp
    DEPENDS basalt::metamorph-region
)

target_compile_definitions(metamorph-region-linear-test
    PRIVATE BASALT_TEST_SCRATCH_DIR="${CMAKE_CURRENT_BINARY_DIR}/test-data"
)
