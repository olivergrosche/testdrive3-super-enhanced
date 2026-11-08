# Building the Linux AppImage

`build.sh` runs inside an `ubuntu:20.04` container, so the image needs only glibc 2.31 or newer. It builds SDL 3 and
wxWidgets 3.2 as static libraries, the game and the launcher, and packs them into one AppImage (the static type 2
runtime, so the host needs no libfuse2).

Any x86-64 Linux with podman or docker will do. The input is a folder holding `src/` (this repository) and an
`install/` folder:

* **Public image (the release):** `install/` empty. The image holds no game files; on the first start it asks for
  them (`README-LINUX.md`). `build.sh` refuses to finish if game files ended up inside.
* **Private image:** `install/Game/` with the game files, and optionally `install/settings.ini` (the launcher's) and
  `install/testdrive3-enhanced.args` (for `--play`). Such an image holds the original game and is **not for
  publishing**, which is also why `/release/` is ignored.

```
podman run --rm -v "$PWD:/work" -e VERSION=0.2.0-og10 ubuntu:20.04 bash /work/src/packaging/appimage/build.sh
```

The AppImage lands in `out/`; downloads and the built libraries stay in `cache/` for the next run.

`AppRun` is what the image runs and `README-LINUX.md` is the user's guide; both go into the image.
