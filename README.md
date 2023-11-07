# Flask
A client plugin for TF2 to implement and expose new feature for observation, with Windows and Linux support, always.

## Building
### Windows
- Install Visual Studio 2022
- Install [CMake](https://cmake.org/)
- Download [ninja](https://ninja-build.org/) and make it `$PATH` accessible
- Download [Boost 1.80](https://www.boost.org/users/history/version_1_80_0.html)
  - Choose the Windows ZIP, extract it to `C:/Program Files` -- this path is suggested by Boost developers and is the one used by Flask.
- Clone repository (`git clone --recurse-submodules https://github.com/drunderscore/Flask`)
- `mkdir Build` and `cd Build` in source tree
- `cmake -G Ninja -DTF2_PATH="C:\path\to\Team Fortress 2" ..`
  - You can omit `-G Ninja` if you want to open it in Visual Studio, but I never use it so you're on your own.
- `ninja`
- You now have `Flask.dll`.