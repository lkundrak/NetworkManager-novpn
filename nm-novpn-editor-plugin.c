/*
 * nm-novpn-editor-plugin - VPN plugin for the NetworkManager mock
 * VPN service
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * (C) Copyright 2018 Lubomir Rintel
 */

#define _GNU_SOURCE
#include <link.h>
#include <dlfcn.h>
#include <glib/gi18n.h>
#include <NetworkManager.h>

struct _NovpnEditorPlugin {
	GObject parent;
};

struct _NovpnEditorPluginClass {
	GObjectClass parent;
};

static void novpn_editor_plugin_interface_init (NMVpnEditorPluginInterface *iface_class);

#define NOVPN_TYPE_EDITOR_PLUGIN (novpn_editor_plugin_get_type ())
G_DECLARE_FINAL_TYPE (NovpnEditorPlugin, novpn_editor_plugin, NM, NOVPN_EDITOR_PLUGIN, GObject)
G_DEFINE_TYPE_EXTENDED (NovpnEditorPlugin, novpn_editor_plugin, G_TYPE_OBJECT, 0,
                        G_IMPLEMENT_INTERFACE (NM_TYPE_VPN_EDITOR_PLUGIN,
                                               novpn_editor_plugin_interface_init))

enum {
	PROP_0,
	PROP_NAME,
	PROP_DESC,
	PROP_SERVICE
};

typedef NMVpnEditor *(*NMVpnEditorFactory) (NMVpnEditorPlugin *editor_plugin,
                                            NMConnection *connection,
                                            GError **error);

static guint32
get_capabilities (NMVpnEditorPlugin *iface)
{
	return NM_VPN_EDITOR_PLUGIN_CAPABILITY_IMPORT |
	       NM_VPN_EDITOR_PLUGIN_CAPABILITY_EXPORT |
	       NM_VPN_EDITOR_PLUGIN_CAPABILITY_IPV6;
}

static int
phdr_cb (struct dl_phdr_info *info,
         size_t size,
         void *data)
{
	char **editor_path = data;
	g_autofree char *dirname = NULL;

	if (!g_str_has_suffix (info->dlpi_name, "libnm-novpn-editor-plugin.so"))
		return 0;

	dirname = g_path_get_dirname (info->dlpi_name);
	*editor_path = g_build_filename (dirname, "libnm-novpn-editor.so", NULL);

	return 1;
}

static NMVpnEditor *
get_editor (NMVpnEditorPlugin *iface,
            NMConnection *connection,
            GError **error)
{
	void *handle;
	g_autofree char *editor_path = NULL;
	NMVpnEditorFactory nm_vpn_editor_factory_novpn;

	dl_iterate_phdr (phdr_cb, &editor_path);
	g_return_val_if_fail (editor_path, NULL);

	handle = dlopen (editor_path, RTLD_LAZY);
	g_return_val_if_fail (handle, NULL);

	nm_vpn_editor_factory_novpn = dlsym (handle, "nm_vpn_editor_factory_novpn");
	g_return_val_if_fail (nm_vpn_editor_factory_novpn, NULL);

	return nm_vpn_editor_factory_novpn (iface, connection, error);
}

static void
get_property (GObject *object,
              guint prop_id,
              GValue *value,
              GParamSpec *pspec)
{
	switch (prop_id) {
	case PROP_NAME:
		g_value_set_string (value, _("Mock VPN Plugin"));
		break;
	case PROP_DESC:
		g_value_set_string (value, _("For testing"));
		break;
	case PROP_SERVICE:
		g_value_set_string (value, "org.freedesktop.NetworkManager.Novpn");
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
novpn_editor_plugin_init (NovpnEditorPlugin *plugin)
{
}

static NMConnection *
import_from_file (NMVpnEditorPlugin *plugin, const char *file_name,
                  GError **error)
{
	g_autoptr(GKeyFile) keyfile = g_key_file_new ();
	g_autoptr(NMConnection) connection = nm_simple_connection_new ();
	NMSetting *setting_vpn = nm_setting_vpn_new ();
	g_auto(GStrv) data_items = NULL;
	g_auto(GStrv) secrets = NULL;
	char *str;
	gsize len;
	gsize i;

	if (!g_key_file_load_from_file (keyfile, file_name, G_KEY_FILE_NONE, error))
		return NULL;

	str = g_key_file_get_string (keyfile, "connection", "id", error);
	if (!str)
		return NULL;
	nm_connection_add_setting (connection,
		g_object_new (NM_TYPE_SETTING_CONNECTION,
		              "id", str,
		              NULL));
	g_free (str);

	g_object_set (setting_vpn,
	              "service-type", "org.freedesktop.NetworkManager.Novpn",
	              NULL);
	nm_connection_add_setting (connection, setting_vpn);

	data_items = g_key_file_get_keys (keyfile, "vpn", &len, NULL);
	if (!data_items)
		len = 0;
	for (i = 0; i < len; i++) {
		str = g_key_file_get_string (keyfile, "vpn", data_items[i], error);
		if (!str)
			return NULL;
		nm_setting_vpn_add_data_item (NM_SETTING_VPN (setting_vpn), data_items[i], str);
		g_free (str);
	}

	secrets = g_key_file_get_keys (keyfile, "vpn-secrets", &len, NULL);
	if (!secrets)
		len = 0;
	for (i = 0; i < len; i++) {
		str = g_key_file_get_string (keyfile, "vpn-secrets", secrets[i], error);
		if (!str)
			return NULL;
		nm_setting_vpn_add_secret (NM_SETTING_VPN (setting_vpn), secrets[i], str);
		g_free (str);
	}

	return g_object_ref (connection);
}

static void
_add_data_item (const char *key, const char *value, gpointer user_data)
{
	GKeyFile *key_file = user_data;

	g_key_file_set_string (key_file, "vpn", key, value);
}

static void
_add_secret (const char *key, const char *value, gpointer user_data)
{
	GKeyFile *key_file = user_data;

	g_key_file_set_string (key_file, "vpn-secrets", key, value);
}

static gboolean
export_to_file (NMVpnEditorPlugin *plugin, const char *file_name,
                NMConnection *connection, GError **error)
{
	g_autoptr(GKeyFile) keyfile = g_key_file_new ();
	NMSettingVpn *setting_vpn = nm_connection_get_setting_vpn (connection);

	g_key_file_set_string (keyfile, "connection", "id", nm_connection_get_id (connection));
	nm_setting_vpn_foreach_data_item (setting_vpn, _add_data_item, keyfile);
	nm_setting_vpn_foreach_secret (setting_vpn, _add_secret, keyfile);

	return g_key_file_save_to_file (keyfile, file_name, error);
}

static void
novpn_editor_plugin_interface_init (NMVpnEditorPluginInterface *iface_class)
{
	iface_class->get_editor = get_editor;
	iface_class->get_capabilities = get_capabilities;
	iface_class->import_from_file = import_from_file;
	iface_class->export_to_file = export_to_file;
	iface_class->get_suggested_filename = NULL;
}

static void
novpn_editor_plugin_class_init (NovpnEditorPluginClass *req_class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (req_class);

	object_class->get_property = get_property;

	g_object_class_override_property (object_class,
	                                  PROP_NAME,
	                                  NM_VPN_EDITOR_PLUGIN_NAME);

	g_object_class_override_property (object_class,
	                                  PROP_DESC,
	                                  NM_VPN_EDITOR_PLUGIN_DESCRIPTION);

	g_object_class_override_property (object_class,
	                                  PROP_SERVICE,
	                                  NM_VPN_EDITOR_PLUGIN_SERVICE);
}

G_MODULE_EXPORT NMVpnEditorPlugin *
nm_vpn_editor_plugin_factory (GError **error)
{
	if (error)
		g_return_val_if_fail (*error == NULL, NULL);

	return g_object_new (NOVPN_TYPE_EDITOR_PLUGIN, NULL);
}
