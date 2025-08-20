cd %~dp0
mkdir build
cd build
cmake -G "Visual Studio 14 2015" -DCMAKE_GENERATOR_TOOLSET=v140 -DWITH_DEMO=ON %~1 %~2 %~3 %~4 ../
cd %~dp0