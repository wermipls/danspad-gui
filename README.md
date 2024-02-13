# danspad-gui

![image](https://github.com/wermipls/danspad-gui/assets/32251376/4024837d-f14c-4f4b-b27d-4c69b9621866)

Proof of concept FSR dancepad configuration UI, drop-in replacement for use with [teejusb's FSR firmware](https://github.com/teejusb/fsr). Simple, single executable, minimal dependencies (SDL2 and libserialport), performant (starts up instantly, hardware accelerated preview), likely cross-platform with little to no changes. No bloated web UI with tens of megabytes of `node_modules` or Python server with impossible to set up dependencies 😉

## Building

MSYS2 MinGW64 is used on Windows for a Unix-like build environment. Aside from standard build tools (C compiler and so), you will need to have Meson, SDL2 and libserialport installed.

```sh
git clone https://github.com/wermipls/danspad-gui
cd danspad-gui

meson setup builddir
cd builddir

meson compile
```

## Usage

Fixed command line arguments are used for configuration. 

```sh
./danspad-gui serial_port profile_path

# for example:
./danspad-gui com5 my_profile
```

Profile argument can be omitted, thresholds stored on the pad will be used. Serial port argument can be omitted as well, application will attempt connecting to the first USB serial device instead.

Profile files are currently in binary format, for simplicity (but lack of portability across architectures).
