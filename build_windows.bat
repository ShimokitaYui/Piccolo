@echo off

:: 在这里加入了强制兼容 3.5 版本的参数
cmake -S . -B build -DCMAKE_POLICY_VERSION_MINIMUM=3.5

cmake --build build --config Release
pause