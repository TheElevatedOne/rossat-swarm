# AUR packages

| Directory | Package | Builds from |
|---|---|---|
| `rossat-swarm-bin/` | `rossat-swarm-bin` | the x86_64 tarball attached to the GitHub release (`.github/workflows/release.yml`) |
| `rossat-swarm-git/` | `rossat-swarm-git` | the latest commit of https://github.com/TheElevatedOne/rossat-swarm |

Both depend on `hidapi`, `gtk3`, `glib2`, `cairo`, `gdk-pixbuf2` and
`libayatana-appindicator` (system tray), conflict with each other and
provide `rossat-swarm`. The shared `rossat-swarm.install` reloads the udev
rules and points at the lighting-engine user service.

Releasing a new version:

1. Bump `project(... VERSION x.y.z)` in `CMakeLists.txt` and the
   `ROSSAT_VERSION_*` macros in `include/rossat/rossat.h`, commit, tag `x.y.z`, push.
2. Wait for the release workflow to attach `rossat-swarm-x.y.z-x86_64.tar.gz`.
3. In `rossat-swarm-bin/`: set `pkgver`, run `updpkgsums` (replaces the
   `SKIP` checksum) and `makepkg --printsrcinfo > .SRCINFO`, then push to the AUR.
4. `rossat-swarm-git` picks up new commits by itself; regenerate its
   `.SRCINFO` only when the PKGBUILD changes.
