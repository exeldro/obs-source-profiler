# Source profiler for OBS Studio

Plugin for [OBS Studio](https://github.com/obsproject/obs-studio) to add Source Profiler to tools menu

Based on [source profiling ui](https://github.com/derrod/obs-studio/tree/source-profiling-ui) by @derrod

# Build
- In-tree build
    - Build OBS Studio: https://obsproject.com/wiki/Install-Instructions
    - Check out this repository to UI/frontend-plugins/source-profiler
    - Add `add_subdirectory(source-profiler)` to UI/frontend-plugins/CMakeLists.txt
    - Rebuild OBS Studio
- Stand-alone build
    - Verify that you have development files for OBS
    - Check out this repository and run `cmake -S . -B build -DBUILD_OUT_OF_TREE=On && cmake --build build`
