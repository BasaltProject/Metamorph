add_executable(metamorph src/Main.cpp src/Worlds.cpp src/Commands.cpp)
target_include_directories(metamorph PRIVATE src)
target_link_libraries(metamorph PRIVATE basalt::metamorph-region basalt::metamorph-datafix basalt::wire-nbt
                                        basalt::wire-io basalt::warnings basalt::sanitizers basalt::parity)
