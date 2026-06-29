<h1>Gabro's Entity Component System</h1>
...
<h1>Build</h1>
You'll need CMake.
Open a terminal in the root directory.

Run:
  1) cmake -B build
  2) cmake --build build

Optional: on the first step add -D CMAKE_BUILD_TYPE=Release to build with compiler optimizations, use this for a release build.

There is a convenient "test" file in the test folder.
To build the test run:
  - cmake --build build --target test
