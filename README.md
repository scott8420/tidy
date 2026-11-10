# Tidy

A gtkmm-4 app that knows how long each file in your home folder should live,
and where it belongs -- and acts on it only with your say-so, safely and
undoably.

Files arrive without intent: a save lands wherever the app pointed. Each file's
life is set by its purpose, not by how often it is read. Tidy gives every file
a **retention level**, mostly decided when it is born; the undecided wait in
**the Closet** until someone decides. Projects (`<name>-project`) move whole
and keep their tree. Nothing is removed until it is archived, cataloged and
on backup media.

Split from Rouster (the space reclaimer) in Nov 2026; started from Cairn; every
file move goes through **latch**, the one door Rouster also uses.

**Status: the spine.** The window, the look, every page on the road as a hero
that says what it will do and when. Read-only: Tidy writes nothing yet.

## Build

Needs latch installed first (`./install.sh` in latch's folder), then:

```
./build.sh                    # Fedora: gtkmm4.0-devel spdlog-devel cmake gcc-c++
./build/tidy_selftest         # headless: the look, the pages, latch's refusals, the source gate
./build/tidy
TIDY_LOOK=0 ./build/tidy      # the plain shell
```

Without installing latch: `./build.sh -DLATCH_DIR=<latch source>`.

## Install

```
./install.sh                  # ~/.local, no sudo   (--system: /usr/local; --prefix DIR; --build)
./uninstall.sh                # exactly what install.sh listed; --purge: settings + cache,
                              # never Tidy's records (the catalog, the way back, templates)
```

## License

MIT -- see LICENSE.
