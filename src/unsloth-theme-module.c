/*
 * unsloth-theme-sync
 * SPDX-FileCopyrightText: 2026 unsloth-theme-sync contributors
 * SPDX-License-Identifier: AGPL-3.0-only
 *
 * A small GTK3 module that makes Unsloth Desktop follow the XDG desktop
 * portal's color-scheme setting.
 *
 * Unsloth Desktop is a Tauri/WebKitGTK application. Its "System" theme is
 * WebKit's prefers-color-scheme media query, which WebKitGTK derives from the
 * GTK theme (gtk-theme-name / gtk-application-prefer-dark-theme), not from
 * Plasma's color scheme. This module reads org.freedesktop.appearance
 * color-scheme from the portal and applies it to GtkSettings in-process, so
 * the running app switches live when Plasma switches between light and dark.
 *
 * Load it only into Unsloth via GTK_MODULES so other GTK apps are unaffected.
 */

#include <gio/gio.h>
#include <gtk/gtk.h>

#define PORTAL_BUS "org.freedesktop.portal.Desktop"
#define PORTAL_PATH "/org/freedesktop/portal/desktop"
#define PORTAL_IFACE "org.freedesktop.portal.Settings"
#define APPEARANCE_NAMESPACE "org.freedesktop.appearance"
#define COLOR_SCHEME_KEY "color-scheme"

#define DEFAULT_LIGHT_THEME "Breeze"
#define DEFAULT_DARK_THEME "Breeze-Dark"

static const char *theme_name_for_mode(gboolean dark)
{
  const char *override = g_getenv(dark ? "UNSLOTH_THEME_DARK" : "UNSLOTH_THEME_LIGHT");

  if (override != NULL && *override != '\0')
  {
    return override;
  }

  return dark ? DEFAULT_DARK_THEME : DEFAULT_LIGHT_THEME;
}

static void apply_mode(guint32 mode)
{
  GtkSettings *settings = gtk_settings_get_default();

  if (settings == NULL)
  {
    return;
  }

  const gboolean dark = (mode == 1);
  const char *theme = theme_name_for_mode(dark);

  /*
   * Setting the theme name alone is enough for WebKit to update its
   * prefers-color-scheme value. Also set prefer-dark so light themes with a
   * dark variant (and GTK widgets) behave consistently.
   */
  g_object_set(settings,
               "gtk-theme-name", theme,
               "gtk-application-prefer-dark-theme", dark,
               NULL);

  g_printerr("unsloth-theme-sync: color-scheme=%u -> %s\n", mode, theme);
}

/*
 * Takes ownership of value (expected to be a new reference) and unwraps any
 * number of nested variants until it finds a uint32.
 */
static gboolean take_uint32(GVariant *value, guint32 *out)
{
  while (g_variant_is_of_type(value, G_VARIANT_TYPE_VARIANT))
  {
    GVariant *inner = g_variant_get_variant(value);
    g_variant_unref(value);
    value = inner;
  }

  if (g_variant_is_of_type(value, G_VARIANT_TYPE_UINT32))
  {
    *out = g_variant_get_uint32(value);
    g_variant_unref(value);
    return TRUE;
  }

  g_variant_unref(value);
  return FALSE;
}

static void on_setting_changed(GDBusConnection *connection,
                               const gchar *sender_name,
                               const gchar *object_path,
                               const gchar *interface_name,
                               const gchar *signal_name,
                               GVariant *parameters,
                               gpointer user_data)
{
  (void)connection;
  (void)sender_name;
  (void)object_path;
  (void)interface_name;
  (void)signal_name;
  (void)user_data;

  GVariant *namespace_value = g_variant_get_child_value(parameters, 0);
  GVariant *key_value = g_variant_get_child_value(parameters, 1);
  GVariant *packed_value = g_variant_get_child_value(parameters, 2);
  guint32 mode = 0;

  if (g_strcmp0(g_variant_get_string(namespace_value, NULL), APPEARANCE_NAMESPACE) == 0 &&
      g_strcmp0(g_variant_get_string(key_value, NULL), COLOR_SCHEME_KEY) == 0 &&
      take_uint32(packed_value, &mode))
  {
    apply_mode(mode);
  }
  else
  {
    g_variant_unref(packed_value);
  }

  g_variant_unref(namespace_value);
  g_variant_unref(key_value);
}

static GVariant *call_portal_read(GDBusConnection *connection,
                                  const char *method,
                                  GError **error)
{
  return g_dbus_connection_call_sync(
      connection,
      PORTAL_BUS,
      PORTAL_PATH,
      PORTAL_IFACE,
      method,
      g_variant_new("(ss)", APPEARANCE_NAMESPACE, COLOR_SCHEME_KEY),
      G_VARIANT_TYPE("(v)"),
      G_DBUS_CALL_FLAGS_NONE,
      5000,
      NULL,
      error);
}

static void read_initial(GDBusConnection *connection)
{
  GError *error = NULL;

  /* ReadOne exists in portal version 2; fall back to Read on older portals. */
  GVariant *reply = call_portal_read(connection, "ReadOne", &error);

  if (reply == NULL)
  {
    g_clear_error(&error);
    reply = call_portal_read(connection, "Read", &error);
  }

  if (reply == NULL)
  {
    g_printerr("unsloth-theme-sync: portal read failed: %s; using light\n",
               error != NULL ? error->message : "unknown error");
    g_clear_error(&error);
    apply_mode(2);
    return;
  }

  GVariant *value = g_variant_get_child_value(reply, 0);
  guint32 mode = 0;

  if (take_uint32(value, &mode))
  {
    apply_mode(mode);
  }
  else
  {
    apply_mode(2);
  }

  g_variant_unref(reply);
}

G_MODULE_EXPORT void gtk_module_init(gint *argc, gchar ***argv)
{
  (void)argc;
  (void)argv;

  GError *error = NULL;
  GDBusConnection *connection = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);

  if (connection == NULL)
  {
    g_printerr("unsloth-theme-sync: no session bus: %s\n",
               error != NULL ? error->message : "unknown error");
    g_clear_error(&error);
    return;
  }

  read_initial(connection);

  g_dbus_connection_signal_subscribe(connection,
                                     PORTAL_BUS,
                                     PORTAL_IFACE,
                                     "SettingChanged",
                                     PORTAL_PATH,
                                     NULL,
                                     G_DBUS_SIGNAL_FLAGS_NONE,
                                     on_setting_changed,
                                     NULL,
                                     NULL);

  g_printerr("unsloth-theme-sync: watching org.freedesktop.appearance color-scheme\n");
}
