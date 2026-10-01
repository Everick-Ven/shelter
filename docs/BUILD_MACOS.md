# macOS Intel

Use Xcode Command Line Tools, CMake 3.24+ and Ninja. Run: `export CEF_PLATFORM=macos; ./scripts/bootstrap-cef.sh; cmake -S . -B ../shelter-build-macos -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=x86_64; cmake --build ../shelter-build-macos; ctest --test-dir ../shelter-build-macos --output-on-failure`.
