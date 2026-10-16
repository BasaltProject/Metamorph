basalt_module(metamorph-datafix
    SOURCES
        src/DataFixer.cpp
        src/FixSupport.cpp
        src/Componentization.cpp
        src/Fixes3400.cpp
        src/Fixes3800.cpp
        src/Fixes4000.cpp
        src/Fixes4300.cpp
        src/EntityFixes.cpp
        src/Fixes4996.cpp
        src/ItemWalk.cpp
        src/TextFixes.cpp
    DEPENDS basalt::wire-common basalt::wire-nbt
)

basalt_test(metamorph-datafix-test
    SOURCES
        test/DataFixerTest.cpp
    DEPENDS basalt::metamorph-datafix
)
