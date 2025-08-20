cd %~dp0
mkdir build
cd build
cmake -G "Visual Studio 14 2015" -DCMAKE_GENERATOR_TOOLSET=v140 ../
IF %ERRORLEVEL% NEQ  0 exit /b 1
cd %~dp0
call cmake --build build --target clean
call cmake --build build --config Debug
IF %ERRORLEVEL% NEQ  0 exit /b 1
call cmake --build build --config Release

IF %ERRORLEVEL% NEQ  0 exit /b 1
call copy_includefile.bat
IF %ERRORLEVEL% NEQ  0 exit /b 1

copy build\loki.dir\Release\loki.pdb build\Release\

call create_nuget loki.json