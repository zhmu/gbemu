# gbemu

This is a basic Gameboy Emulator written in C++.

# Building

You will need CMake, Ninja and libsdl2:

````
$ mkdir build
$ cd build
$ cmake -GNinja -DCMAKE_BUILD_TYPE=RelWithDebInfo ..
$ ninja
````

# Running
$ src/gbemu <romfile.gb>

