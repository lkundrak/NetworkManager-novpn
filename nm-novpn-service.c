/*
 * nm-novpn-service - NetworkManager mock VPN service
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

#include <stdlib.h>
#include <locale.h>
#include <NetworkManager.h>
#include <arpa/inet.h>

struct _NMNovpnPlugin {
        NMVpnServicePlugin parent;
};

struct _NMNovpnPluginClass {
        NMVpnServicePluginClass parent;
};

#if !NM_CHECK_VERSION(1,13,0)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (NMVpnServicePlugin, g_object_unref)
#endif

#define NM_TYPE_NOVPN_PLUGIN (nm_novpn_plugin_get_type ())
G_DECLARE_FINAL_TYPE (NMNovpnPlugin, nm_novpn_plugin, NM, NOVPN_PLUGIN, NMVpnServicePlugin)
G_DEFINE_TYPE (NMNovpnPlugin, nm_novpn_plugin, NM_TYPE_VPN_SERVICE_PLUGIN)

static gboolean
_connect (gpointer user_data)
{
	NMVpnServicePlugin *plugin = user_data;
	struct in_addr addr;

	g_message ("Sending Config");

	nm_vpn_service_plugin_set_config (NM_VPN_SERVICE_PLUGIN (plugin),
		g_variant_new_parsed ("[{'banner', <%s>}, {'has-ip4', <%b>}, {'has-ip6', <%b>}]",
		                      "Behold, Mock Net Connected!", TRUE, FALSE));

	inet_pton (AF_INET, "192.0.2.1", &addr);

	nm_vpn_service_plugin_set_ip4_config (NM_VPN_SERVICE_PLUGIN (plugin),
		g_variant_new_parsed ("[{'address', <%u>}, {'prefix', <%u>},"
		                      "{'never-default', <%b>}, {'domain', <%s>}]",
		                      addr.s_addr, 32, TRUE, "example.com"));

	return G_SOURCE_REMOVE;
}

static gboolean
real_connect (NMVpnServicePlugin *plugin,
              NMConnection *connection,
              GError **error)
{
	g_message ("Connect");
	nm_connection_dump (connection);

	g_idle_add (_connect, plugin);

	return TRUE;
}

static gboolean
real_need_secrets (NMVpnServicePlugin *plugin,
                   NMConnection *connection,
                   const char **setting_name,
                   GError **error)
{
	NMSettingVpn *setting_vpn = nm_connection_get_setting_vpn (connection);
	NMSettingSecretFlags flags;

	g_message ("Need Secrets");

	nm_connection_dump (connection);

	*setting_name = NM_SETTING_VPN_SETTING_NAME;

	if (nm_setting_vpn_get_secret (setting_vpn, "password"))
		return FALSE;

	if (!nm_setting_get_secret_flags (NM_SETTING (setting_vpn), "password", &flags, NULL))
		return TRUE;

	if (flags & NM_SETTING_SECRET_FLAG_NOT_REQUIRED)
		return FALSE;

	return TRUE;
}

static gboolean
real_disconnect (NMVpnServicePlugin *plugin,
                 GError **error)
{
	g_message ("Disconnect");
	return TRUE;
}

static void
plugin_state_changed (NMVpnServicePlugin *plugin,
		      NMVpnServiceState state,
		      gpointer user_data)
{
	g_message ("State Changed: %d", state);
}

static void
nm_novpn_plugin_init (NMNovpnPlugin *self)
{
}

static void
nm_novpn_plugin_class_init (NMNovpnPluginClass *novpn_class)
{
	NMVpnServicePluginClass *parent_class = NM_VPN_SERVICE_PLUGIN_CLASS (novpn_class);

	parent_class->connect = real_connect;
	parent_class->need_secrets = real_need_secrets;
	parent_class->disconnect = real_disconnect;
}

NMNovpnPlugin *
nm_novpn_plugin_new (const char *bus_name, gboolean debug)
{
	NMNovpnPlugin *self;
	GError *error = NULL;

	self = (NMNovpnPlugin *) g_initable_new (NM_TYPE_NOVPN_PLUGIN, NULL, &error,
	                                           NM_VPN_SERVICE_PLUGIN_DBUS_SERVICE_NAME, bus_name,
	                                           NM_VPN_SERVICE_PLUGIN_DBUS_WATCH_PEER, !debug,
	                                           NULL);
	if (!self) {
		g_message ("Failed to initialize a plugin instance: %s", error->message);
		g_error_free (error);
	}

	return self;
}

static void
quit_mainloop (NMNovpnPlugin *self, gpointer user_data)
{
	g_main_loop_quit ((GMainLoop *) user_data);
}

int
main (int argc, char *argv[])
{
	g_autoptr(NMNovpnPlugin) self = NULL;
	g_autoptr(GMainLoop) main_loop = NULL;
	g_autoptr(GOptionContext) opt_ctx = NULL;
	g_autofree char *bus_name = g_strdup ("org.freedesktop.NetworkManager.Novpn");;
	gboolean persist = FALSE;
	gboolean debug = FALSE;
	g_autoptr(GError) error = NULL;

	GOptionEntry options[] = {
		{ "bus-name", 0, 0, G_OPTION_ARG_STRING, &bus_name, "D-Bus name to use for this instance", NULL },
		{ "persist", 0, 0, G_OPTION_ARG_NONE, &persist, "Don’t quit when VPN connection terminates", NULL },
		{ "debug", 0, 0, G_OPTION_ARG_NONE, &debug, "Enable verbose debug logging (may expose passwords)", NULL },
		{NULL}
	};

	/* locale will be set according to environment LC_* variables */
	setlocale (LC_ALL, "");

	/* Parse options */
	opt_ctx = g_option_context_new (NULL);
	g_option_context_set_translation_domain (opt_ctx, "NetworkManager-novpn");
	g_option_context_set_ignore_unknown_options (opt_ctx, FALSE);
	g_option_context_set_help_enabled (opt_ctx, TRUE);
	g_option_context_add_main_entries (opt_ctx, options, NULL);

	if (!g_option_context_parse (opt_ctx, &argc, &argv, &error)) {
		g_printerr ("Error parsing the command line options: %s", error->message);
		g_option_context_free (opt_ctx);
		return EXIT_FAILURE;
	}

	self = nm_novpn_plugin_new (bus_name, debug);
	if (!self)
		return EXIT_FAILURE;

	main_loop = g_main_loop_new (NULL, FALSE);

	if (!persist)
		g_signal_connect (self, "quit", G_CALLBACK (quit_mainloop), main_loop);

	g_signal_connect (G_OBJECT (self), "state-changed", G_CALLBACK (plugin_state_changed), NULL);

	g_main_loop_run (main_loop);

	return EXIT_SUCCESS;
}
