# unsloth-theme-sync

[![build](https://github.com/satrac/unsloth-theme-sync/actions/workflows/build.yml/badge.svg)](https://github.com/satrac/unsloth-theme-sync/actions/workflows/build.yml)

Make [Unsloth Desktop](https://unsloth.ai/) follow your desktop's light/dark
mode — including **live switching** when KDE Plasma changes its color scheme at
sunrise/sunset.

It is a tiny GTK3 module that reads `org.freedesktop.appearance color-scheme`
from the XDG desktop portal and applies it to Unsloth's GTK settings. Loaded
only into Unsloth, it does not affect other GTK applications.

## The problem

Unsloth Desktop is a Tauri application using WebKitGTK. Its
**Appearance → Theme → System** setting maps to WebKit's
`prefers-color-scheme` media query, which WebKitGTK derives from the **GTK
theme** (`gtk-theme-name` / `gtk-application-prefer-dark-theme`). It does not
read Plasma's color scheme or the XDG portal directly.

On KDE Plasma with **Auto** color scheme, Plasma switches between
`BreezeLight` and `BreezeDark` at sunrise/sunset, but Unsloth stays on whatever
GTK theme it saw at startup. `GTK_USE_PORTAL=1` does not help, and setting
`GTK_THEME` pins the app to one mode (no live switching).

## What this does

`unsloth-theme-module.so` is loaded into Unsloth's process via the `GTK_MODULES`
environment variable. It:

1. reads `org.freedesktop.appearance color-scheme` from the portal at startup;
2. subscribes to the portal's `SettingChanged` signal;
3. sets `gtk-theme-name` (default `Breeze` / `Breeze-Dark`) and
   `gtk-application-prefer-dark-theme` on the app's `GtkSettings`.

WebKit then updates `prefers-color-scheme` without restarting the app.

## Requirements

- GTK3 development files
- `pkg-config` and a C compiler
- A desktop portal implementing the `color-scheme` setting (Plasma 6, GNOME, …)
- Breeze GTK themes installed, or configure different theme names (see below)

Install build dependencies:

```sh
# Arch
sudo pacman -S --needed base-devel gtk3

# Debian / Ubuntu
sudo apt install build-essential pkg-config libgtk-3-dev

# Fedora
sudo dnf install gcc make pkgconf-pkg-config gtk3-devel
```

## Build and install

```sh
git clone https://github.com/satrac/unsloth-theme-sync.git
cd unsloth-theme-sync
make
make install          # installs to ~/.local/lib/unsloth-theme-module.so
```

Use a different prefix:

```sh
make install PREFIX=/usr/local
```

Uninstall:

```sh
make uninstall
```

## Configure Unsloth

Edit the Unsloth launcher, for example
`~/.local/share/applications/unsloth-desktop-linux.desktop`, and add
`GTK_MODULES` to the `Exec=` line:

```ini
Exec=env GTK_MODULES=/home/you/.local/lib/unsloth-theme-module.so UNSLOTH_APPIMAGE_WEBKIT=host "/home/you/.local/bin/Unsloth-Desktop-Linux.AppImage" %u
```

The `UNSLOTH_APPIMAGE_WEBKIT=host` part matters for the AppImage: in bundled
WebKit mode the AppImage's `AppRun` sets `GTK_THEME`, which overrides this
module and prevents live switching. Host mode uses the system GTK/WebKit stack.
If your launch method is not an AppImage, or the variable is unsupported, omit
it.

Keep Unsloth's **Appearance → Theme** on **System** (an explicit Light/Dark
choice in the app overrides the media query).

Launch from the terminal to see the module's log output:

```sh
GTK_MODULES=$HOME/.local/lib/unsloth-theme-module.so \
UNSLOTH_APPIMAGE_WEBKIT=host \
/path/to/Unsloth-Desktop-Linux.AppImage
```

Expected log lines:

```
unsloth-theme-sync: color-scheme=1 -> Breeze-Dark
unsloth-theme-sync: watching org.freedesktop.appearance color-scheme
```

For `unsloth://` links, the app may regenerate
`unsloth-studio-handler.desktop`; add the same environment there if you want
link launches to use the module too.

## Verify

Check what the portal currently reports:

```sh
gdbus call --session \
  --dest org.freedesktop.portal.Desktop \
  --object-path /org/freedesktop/portal/desktop \
  --method org.freedesktop.portal.Settings.Read \
  org.freedesktop.appearance color-scheme
```

- `0` = no preference
- `1` = prefer dark
- `2` = prefer light

Switch Plasma's color scheme (or wait for the automatic switch) and Unsloth
should follow immediately.

## Theme names

Defaults are `Breeze` (light) and `Breeze-Dark` (dark). Override them if you
use another GTK theme:

```sh
UNSLOTH_THEME_LIGHT=Adwaita \
UNSLOTH_THEME_DARK=Adwaita-dark \
GTK_MODULES=$HOME/.local/lib/unsloth-theme-module.so \
UNSLOTH_APPIMAGE_WEBKIT=host \
/path/to/Unsloth-Desktop-Linux.AppImage
```

## How it works

1. GTK loads the module because `GTK_MODULES` points to the `.so`.
2. `gtk_module_init()` reads the portal color-scheme and calls
   `gtk_settings_get_default()`.
3. It sets `gtk-theme-name` and `gtk-application-prefer-dark-theme`.
4. It subscribes to the portal's `SettingChanged` signal and repeats on every
   change.
5. WebKit updates `prefers-color-scheme`; Unsloth's frontend switches theme.

The module changes only the process it is loaded into. It does not modify
dconf, GTK `settings.ini`, or other applications.

For a function-by-function walkthrough of the module, see
[docs/code-walkthrough.md](docs/code-walkthrough.md).

## Limitations

- GTK3 only (Unsloth Desktop uses `webkit2gtk-4.1` / GTK3).
- If an Unsloth update rewrites its `.desktop` file, re-add the `GTK_MODULES`
  environment variable.
- The app's own theme setting must remain **System**.
- Native GTK widgets in the app will use the configured GTK theme
  (`Breeze`/`Breeze-Dark` by default), even if the rest of your desktop uses a
  different GTK theme.
- The module is an independent project and is not affiliated with Unsloth AI.

## License

AGPL-3.0-only, matching **Unsloth Studio** (`studio/*` and `unsloth_cli/*` in
the Unsloth repository are AGPLv3). The core Unsloth library (`unsloth/*`) is
Apache-2.0. See [LICENSE](LICENSE).

## Tested with

- KDE Plasma 6.8.2 (Wayland) with Plasma's XDG portal backend
- GTK 3.24.52
- Unsloth Desktop AppImage with WebKitGTK 2.54.2
