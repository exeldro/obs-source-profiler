# Source Profiler for OBS Studio

Plugin for [OBS Studio](https://github.com/obsproject/obs-studio) to add source profiler to tools menu

# Build
- In-tree build
    - Build OBS Studio: https://obsproject.com/wiki/Install-Instructions
    - Check out this repository to UI/frontend-plugins/source-profiler
    - Add `add_subdirectory(source-profiler)` to UI/frontend-plugins/CMakeLists.txt
    - Rebuild OBS Studio
- Stand-alone build
    - Verify that you have development files for OBS
    - Check out this repository and run `cmake -S . -B build -DBUILD_OUT_OF_TREE=On && cmake --build build`
