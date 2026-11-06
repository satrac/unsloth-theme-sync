# Code walkthrough

This document explains `src/unsloth-theme-module.c`: what kind of code it is,
the entry point GTK calls, and the startup and live-update workflows.

## What kind of code this is

It is not a standalone program. It is a **GTK3 loadable module** — a shared
object (`.so`) with no `main()`. GTK loads it at runtime, calls one exported
function, and keeps it loaded in the process.

That is why the Makefile uses `-fPIC -shared` and why the Unsloth launcher sets
`GTK_MODULES`.

## Entry point: `gtk_module_init`

```c
G_MODULE_EXPORT void gtk_module_init(gint *argc, gchar ***argv)
```

This is the GTK3 module ABI entry point. The sequence is:

1. The app initializes GTK (`gtk_init()`).
2. GTK reads the `GTK_MODULES` environment variable.
3. For each module, GTK `dlopen()`s the `.so` and looks up `gtk_module_init`.
4. GTK calls it with the app's `argc`/`argv`.

`G_MODULE_EXPORT` makes the symbol visible to `dlsym()` even when the module is
built with hidden symbol visibility. The module itself does not use
`argc`/`argv`.

Because this runs inside Unsloth's GTK/WebKit **UI process**, it can change that
process's `GtkSettings` live. An external helper process cannot do that.

## Startup workflow

### 1. Get the session bus

```c
GDBusConnection *connection = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);
```

The module needs the user session bus to talk to
`org.freedesktop.portal.Desktop`. If there is no bus, it logs and returns; the
app continues with its normal theme.

### 2. Read the current portal value

`read_initial()` calls `call_portal_read()`, which performs:

```text
org.freedesktop.portal.Settings.ReadOne(
    "org.freedesktop.appearance",
    "color-scheme"
) -> (v)
```

on:

- bus: `org.freedesktop.portal.Desktop`
- path: `/org/freedesktop/portal/desktop`
- interface: `org.freedesktop.portal.Settings`
- timeout: 5 seconds

`ReadOne` is the modern method. If the portal is older, the code falls back to
the deprecated `Read`.

Returned values:

- `0` = no preference
- `1` = prefer dark
- `2` = prefer light

### 3. Unwrap the variant

D-Bus returns a variant containing a `uint32`. The `Read` fallback historically
double-wraps it, so `take_uint32()` loops:

```c
while (g_variant_is_of_type(value, G_VARIANT_TYPE_VARIANT)) {
    GVariant *inner = g_variant_get_variant(value);
    g_variant_unref(value);
    value = inner;
}
```

Then it extracts the `guint32`. This function **takes ownership** of the
reference passed in and unrefs it, which keeps the ref-counting simple.

### 4. Apply the mode

`apply_mode()` does the actual work:

```c
const gboolean dark = (mode == 1);
const char *theme = theme_name_for_mode(dark);

g_object_set(settings,
             "gtk-theme-name", theme,
             "gtk-application-prefer-dark-theme", dark,
             NULL);
```

`theme_name_for_mode()` checks:

- `UNSLOTH_THEME_DARK`
- `UNSLOTH_THEME_LIGHT`

Otherwise it defaults to `Breeze-Dark` / `Breeze`.

Setting `gtk-theme-name` triggers GTK to reload the theme. Setting
`gtk-application-prefer-dark-theme` keeps GTK's own widgets and dark-variant
handling consistent.

The `g_printerr()` line is only diagnostics:

```text
unsloth-theme-sync: color-scheme=1 -> Breeze-Dark
```

### 5. Subscribe to changes

```c
g_dbus_connection_signal_subscribe(
    connection,
    PORTAL_BUS,
    PORTAL_IFACE,
    "SettingChanged",
    PORTAL_PATH,
    NULL,
    G_DBUS_SIGNAL_FLAGS_NONE,
    on_setting_changed,
    NULL,
    NULL);
```

This is what makes switching live. The subscription stays for the process
lifetime; GIO keeps the bus connection cached.

## Live-update workflow

When Plasma switches at sunset/sunrise, `xdg-desktop-portal` emits:

```text
SettingChanged(
    "org.freedesktop.appearance",
    "color-scheme",
    <uint32 1 or 2>
)
```

`on_setting_changed()`:

1. Extracts child 0 (namespace), child 1 (key), child 2 (packed value).
2. Ignores signals that are not `org.freedesktop.appearance` / `color-scheme`.
3. Unwraps the value via `take_uint32()`.
4. Calls `apply_mode()`.

That sets the two GTK properties again. GTK emits `notify::gtk-theme-name` and
`notify::gtk-application-prefer-dark-theme`; WebKit listens to those and
updates `prefers-color-scheme` in the webview. Unsloth's frontend then
re-renders light/dark **without a restart**.

Sequence:

```text
Plasma color scheme
        │
        ▼
XDG portal (color-scheme = 1/2)
        │  SettingChanged
        ▼
unsloth-theme-module
        │  g_object_set()
        ▼
GtkSettings (theme name + prefer-dark)
        │  notify::...
        ▼
WebKitGTK prefers-color-scheme
        │
        ▼
Unsloth web UI
```

## Ref-counting details

- `g_variant_get_child_value()` returns a new reference.
- `take_uint32()` consumes and unrefs its argument, whether it succeeds or
  fails.
- In `on_setting_changed()`, if the namespace/key filter fails, the code unrefs
  `packed_value` itself, because `take_uint32()` was not called.
- `reply`, `namespace_value`, and `key_value` are explicitly unref'd.
- The `GDBusConnection` is not unref'd; it is a cached shared connection owned
  by GIO for the process lifetime.

## Why the launcher does what it does

```ini
Exec=env GTK_MODULES=/home/you/.local/lib/unsloth-theme-module.so \
  UNSLOTH_APPIMAGE_WEBKIT=host \
  "/path/Unsloth-Desktop-Linux.AppImage" %u
```

- `GTK_MODULES=...` loads the module **only into Unsloth's process**. Other GTK
  apps are untouched.
- `UNSLOTH_APPIMAGE_WEBKIT=host` forces the AppImage to use the host
  GTK/WebKit stack. In bundled mode, the AppImage's `AppRun` sets `GTK_THEME`,
  which would override the module and prevent live switching.
- Unsloth's own theme setting must stay **System**; an explicit Light/Dark
  setting in the app bypasses the media query.

## Build side

The `Makefile` compiles a plugin, not an executable:

```sh
cc -O2 -Wall -Wextra $(pkg-config --cflags gtk+-3.0 gio-2.0) \
   -fPIC -shared -o unsloth-theme-module.so src/unsloth-theme-module.c \
   $(pkg-config --libs gtk+-3.0 gio-2.0)
```

`make install` copies it to `$(PREFIX)/lib`, which is why the launcher points at
`~/.local/lib/unsloth-theme-module.so`.

## Function map

| Function | Responsibility |
| --- | --- |
| `theme_name_for_mode()` | Reads `UNSLOTH_THEME_LIGHT` / `UNSLOTH_THEME_DARK`, otherwise returns `Breeze` / `Breeze-Dark`. |
| `apply_mode()` | Maps the portal mode to a theme and writes `gtk-theme-name` + `gtk-application-prefer-dark-theme`. |
| `take_uint32()` | Consumes a GVariant, unwraps nested variants, extracts a `uint32`. |
| `on_setting_changed()` | D-Bus `SettingChanged` callback; filters and applies the new mode. |
| `call_portal_read()` | Synchronous D-Bus helper for portal `Read` / `ReadOne`. |
| `read_initial()` | Reads the portal at startup, falls back to `Read`, defaults to light. |
| `gtk_module_init()` | GTK3 module entry point; connects to the bus, applies the initial mode, subscribes to changes. |

The short version: **GTK loads the module at startup → it reads the portal once
→ it subscribes for changes → every change updates `GtkSettings` in-process →
WebKit updates `prefers-color-scheme` live.**
