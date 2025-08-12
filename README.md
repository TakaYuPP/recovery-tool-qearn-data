# recovery-tool-qearn-data

This is the tool to recovery Qearn data. Qearn data is broken in the epoch 172.

How to work this tool

Please make the build file using cmake(you need to install the cmake in your PC. please download in here(https://cmake.org/download/)).
   - you need to make the `build` directory inside `recovery-tool-qearn-data` directory at first.
   - please open the `build` directory.
   - please open the cmd and write `cmake ../` command in cli. then it will be created the build files.
   - please open the file `MigrationTool.sln` using Mocrosoft Visual Studio 2022.
   - please complie with release mode. then `recovery-tool-qearn-data.exe` file would be created in release directory.
   - please paste the contract0009.174 file in the release directory.
   - run the `recovery-tool-qearn-data.exe` file.
   - you need to check the changes(date, size) of contract0010.174 files.