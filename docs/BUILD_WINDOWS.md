# Windows x64

Use Visual Studio 2022 C++, CMake 3.24+ and Ninja. Run PowerShell: ` $env:CEF_PLATFORM='windows'; ./scripts/bootstrap-cef.sh; cmake -S . -B ../shelter-build-windows -G Ninja -DCMAKE_BUILD_TYPE=Release; cmake --build ../shelter-build-windows; ctest --test-dir ../shelter-build-windows --output-on-failure`.
