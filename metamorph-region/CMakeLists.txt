basalt_module(metamorph-region
    SOURCES
        src/AnvilRegion.cpp
        src/BswConvert.cpp
        src/BswDictionary.cpp
        src/BswRegion.cpp
        src/ChunkBlob.cpp
        src/LinearRegion.cpp
    DEPENDS basalt::metamorph-datafix basalt::wire-common basalt::wire-io basalt::wire-nbt
    PRIVATE_DEPENDS basalt::zstd
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

basalt_test(metamorph-region-bsw-test
    SOURCES
        test/BswRegionTest.cpp
    DEPENDS basalt::metamorph-region
)

target_compile_definitions(metamorph-region-bsw-test
    PRIVATE BASALT_TEST_SCRATCH_DIR="${CMAKE_CURRENT_BINARY_DIR}/test-data"
)

if(CMAKE_SYSTEM_NAME MATCHES "Linux|Android")
    basalt_test(metamorph-region-bsw-crash-test
        SOURCES
            test/BswCrashTest.cpp
        DEPENDS basalt::metamorph-region
    )

    target_compile_definitions(metamorph-region-bsw-crash-test
        PRIVATE BASALT_TEST_SCRATCH_DIR="${CMAKE_CURRENT_BINARY_DIR}/test-data"
    )
endif()

basalt_test(metamorph-region-chunk-blob-test
    SOURCES
        test/ChunkBlobTest.cpp
    DEPENDS basalt::metamorph-region
)
