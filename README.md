# FooBillard++

A free OpenGL billiard (pool) game for Linux, macOS and Windows.

FooBillard++ is an advanced version of the original
[foobillard 3.0a](https://en.wikipedia.org/wiki/FooBillard) by Florian Berger,
extended by Holger Schaekel with many fixes, new options, graphics and
features. This repository is a modernized fork: the game has been **ported
from SDL 1.2 to SDL2** and a number of long-standing crashes and gameplay
bugs have been fixed (see [Recent changes](#recent-changes-in-this-fork) and
the [ChangeLog](ChangeLog)).

![opening](screenshots/foobilliards.png)

## Features

- 8-ball, 9-ball, carambol and snooker, with simple rules enforcement
- Tournament mode for all game types
- Simple AI player
- Full 3D view: rotate (left mouse button), zoom (right mouse button),
  FOV (right mouse button + `CTRL`), fully playable bird's-eye view
- Shot strength adjustment (mouse wheel) and eccentric hit / spin
  adjustment (button 2 + `Shift`)
- Jump shots and advanced aiming ("snipping") mode
- Animated cue, reflections on balls, shadows, lens flare
- Red/green (anaglyph) stereo mode
- Sound effects and background music (SDL2_mixer)
- Network play over IPv4 (beta)
- Tron-like game mode and glass balls, if you like
- CLI options plus a config file (`~/.foobillardrc`)
- On-screen HUD and status line

Press `F1` in game for a quick help!

## Dependencies

- OpenGL and GLU (100% OpenGL-compatible drivers required)
- [SDL2](https://www.libsdl.org/) — windowing and input
- SDL2_mixer — sound and music
- SDL2_net — network play
- libpng — texture loading
- FreeType 2 — font rendering

On Debian/Ubuntu:

```sh
sudo apt-get install build-essential pkg-config autoconf automake \
    libglu1-mesa-dev libfreetype6-dev libpng-dev \
    libsdl2-dev libsdl2-net-dev libsdl2-mixer-dev
```

On Fedora:

```sh
sudo dnf install gcc make autoconf automake mesa-libGLU-devel \
    freetype-devel libpng-devel SDL2-devel SDL2_net-devel SDL2_mixer-devel
```

## Building

The project uses GNU Autotools. The quickest way:

```sh
./buildme.sh        # regenerates the build system, runs ./configure && make
sudo make install
```

Or step by step:

```sh
aclocal --force
autoconf -f
autoheader -f
automake -a -c -f
./configure
make
sudo make install
```

The game installs under `/usr/games/foobillardplus` by default; run
`/usr/games/foobillardplus/bin/foobillardplus` (or add it to your `PATH`).

Every push is built by GitHub Actions on Ubuntu with both gcc and clang
(see [.github/workflows/ci.yml](.github/workflows/ci.yml)).

### macOS

Special files for macOS, including an Xcode project, are in the
[osx](osx) directory. The homebrew-provided `freetype` and `sdl2` libraries
are used for the dependencies.

### configure options

| Option | Default | Description |
| ------ | ------- | ----------- |
| `--enable-sound=ARG` | yes | Build with sound support (SDL2_mixer). |
| `--enable-network=ARG` | yes | Build with IP network game support (SDL2_net). |
| `--enable-mathsingle=ARG` | yes | Use single-precision math. **Clients with mixed single/double precision are not compatible in network games.** |
| `--enable-fastmath` | no | Use fast approximate sine/cosine/tangent routines. Less accurate than the standard routines, but enough for the game. Unrelated to SSE. |
| `--enable-sse=ARG` | no | Use SSE intrinsics. Implies single precision (double precision is disabled automatically). x86/x86_64 CPUs only. |
| `--enable-optimization` | no | Build with a high compiler optimization level. May produce unstable code on some systems — see below. |
| `--enable-special` | no | Pass your own custom `CFLAGS` instead of the defaults. |
| `--enable-touch` | no | Build a version for generic touch devices. |
| `--enable-wetab` | no | Build a version for the WeTab tablet PC (and only for that). Don't combine with other optimization flags. |
| `--enable-win` | no | Cross-build for MS Windows (32/64 bit) under a [MinGW/MSYS](http://sourceforge.net/projects/mingw) environment. |

Note: `--enable-nvidia` exists but is for testing only — please don't use it.

#### A note on compiler optimization

On some systems aggressive `gcc` optimization can produce unstable code,
which is why no optimization flags are set by default. To list the
optimizations available on your target architecture:

```sh
gcc -c -Q -O3 --help=optimizers > /tmp/O3-opts
```

Use these wisely via `--enable-special` (custom `CFLAGS`), or use
`--enable-optimization` for a preset higher level. There is no guarantee
that the highest levels produce a stable program.

## Config file (`~/.foobillardrc`)

You can place a config file named `.foobillardrc` in your home directory
(on Windows, in the directory named by the `USERPROFILE` environment
variable). The game also writes its settings there.

The file can contain any CLI argument, without the leading `-` and one per
line. CLI arguments are parsed last, so they override `.foobillardrc`
settings.

## Red/green stereo

One picture is drawn on the red channel only, the other on the green and
blue channels, so you can use a green, blue or cyan filter for the left eye
and a red one for the right eye.

## Network game

IP network play is supported over IPv4 (IPv6 is planned). This feature is
heavily BETA.

## Recent changes in this fork

- Ported from SDL 1.2 to SDL2, including mouse-wheel handling via
  `SDL_MOUSEWHEEL` (the wheel adjusts shot strength)
- Fixed a segfault when choosing "Restart Game" from the in-game menu, and
  made it restart in-process instead of re-executing the binary
- Fixed balls resting in a pocket jaw or against a rail jumping to a new
  position between shots
- Fixed the default language selection
- Cleaned up various compiler warnings and an uninitialized-buffer read in
  the sound code

See the [ChangeLog](ChangeLog) for details.

## Screenshots

![green](screenshots/foobilliards1.png)
![snooker](screenshots/foobilliards-snooker.png)
![snooker1](screenshots/foobilliards-snooker1.png)

## Known bugs

- On some Intel integrated graphics chips (GMA) the game is not playable
  under Linux. You really need 100% OpenGL-compatible graphics drivers, and
  50–80 MB of video RAM — with less, the game graphics come out ugly and
  corrupt.

## Why "foo"?

Florian Berger had this logo (F.B. — Florian Berger) and "foo" sounds a bit
like "pool" (somehow he wasn't quite attracted by the name "FoolBillard").

If you are a billiard pro and you're missing some physics, please tell us —
it was implemented the way the authors think it should work, which might
differ from reality.

## Credits & thanks

Many thanks to the band `Zentriert ins Antlitz`, especially Marc Friedrich,
for the music in the game: http://www.zentriertinsantlitz.de

The project is powered by, among others: OpenGL, GCC, GNU Autotools, SDL2,
GIMP, Blender, Audacity, and free 3D graphics from www.terminal26.de,
www.scopia.es and www.blendswap.com. See [AUTHORS](AUTHORS) for the full
list of contributors.

## License

Copyright (C) 2001 Florian Berger (foobillard)

Copyright (C) 2010/2011 Holger Schaekel (foobillard++) —
email: foobillardplus@go4more.de

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License Version 2 as published by
the Free Software Foundation.

This program is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
more details ([COPYING](COPYING)).

The bundled fonts are licensed under the SIL Open Font License
([OFL.txt](OFL.txt)).
