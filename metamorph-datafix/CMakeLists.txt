basalt_module(metamorph-datafix
    SOURCES
        src/DataFixer.cpp
        src/FixSupport.cpp
        src/Componentization.cpp
        src/Fixes1200.cpp
        src/FlatteningFixes.cpp
        src/Fixes1400.cpp
        src/LegacyStates.cpp
        src/Fixes2600.cpp
        src/Fixes3000.cpp
        src/Fixes3400.cpp
        src/Fixes3800.cpp
        src/Fixes4000.cpp
        src/Fixes4300.cpp
        src/EntityFixes.cpp
        src/Fixes4996.cpp
        src/ItemWalk.cpp
        src/LevelData.cpp
        src/ProgressData.cpp
        src/TextFixes.cpp
    DEPENDS basalt::wire-common basalt::wire-nbt
)

basalt_test(metamorph-datafix-test
    SOURCES
        test/DataFixerTest.cpp
    DEPENDS basalt::metamorph-datafix
)
