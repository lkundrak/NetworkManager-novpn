/*
 * nm-novpn-auth-dialog - Authentication handler for the NetworkManager
 * mock VPN service
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

#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <glib.h>
#include <glib-unix.h>
#include <NetworkManager.h>

static gboolean
spawn_gui_helper (const char *progname,
                  const char *keyfile_data,
                  gssize length,
                  char * const argv[],
                  GError **error)
{
	g_autofree gchar *gui_helper = g_strdup_printf ("%s-gui", progname);
	GIOChannel *output = NULL;
	GIOStatus status;
	pid_t child_pid;
	gsize written;
	int child_status;
	int fds[2];

	if (pipe (fds) == -1) {
		g_set_error_literal (error, G_UNIX_ERROR, 0, g_strerror (errno));
		return FALSE;
	}

	child_pid = fork ();
	if (child_pid == -1) {
		close (fds[0]);
		close (fds[1]);
		g_set_error_literal (error, G_UNIX_ERROR, 0, g_strerror (errno));
		return FALSE;
	}

	if (child_pid == 0) {
		close (STDIN_FILENO);
		close (fds[1]);
		if (dup2 (fds[0], STDIN_FILENO) == -1) {
			g_printerr ("Error: %s", g_strerror (errno));
			exit (EXIT_FAILURE);
		}
		if (execv (gui_helper, argv) == -1) {
			g_printerr ("%s: %s", gui_helper, g_strerror (errno));
			exit (EXIT_FAILURE);
		}
		g_assert_not_reached ();
	}

	close (fds[0]);
	output = g_io_channel_unix_new (fds[1]);

	status = g_io_channel_write_chars (output, keyfile_data, length, &written, error);
	g_io_channel_unref (output);
	close (fds[1]);
	if (status == G_IO_STATUS_ERROR)
		return FALSE;
	g_return_val_if_fail (status == G_IO_STATUS_NORMAL, FALSE);

	if (waitpid (child_pid, &child_status, 0) == -1) {
		g_set_error_literal (error, G_UNIX_ERROR, 0, g_strerror (errno));
		return FALSE;
	}

	if (child_status != 0) {
		g_set_error (error, -1, -1, "%s exited with status %d", gui_helper, child_status);
		return FALSE;
	}

	return TRUE;
}

static void
_vpn_setting_add_data (gpointer key,
                       gpointer value,
                       gpointer user_data)
{
	const char *key_str = key;
	const char *value_str = value;
	NMSettingVpn *setting_vpn = NM_SETTING_VPN (user_data);

	nm_setting_vpn_add_data_item (setting_vpn, key_str, value_str);
}

static void
_vpn_setting_add_secret (gpointer key,
                         gpointer value,
                         gpointer user_data)
{
	const char *key_str = key;
	const char *value_str = value;
	NMSettingVpn *setting_vpn = NM_SETTING_VPN (user_data);

	nm_setting_vpn_add_secret (setting_vpn, key_str, value_str);
}

int
main (int argc, char *argv[])
{
	gboolean reprompt = FALSE;
	gboolean allow_interaction = FALSE;
	gboolean external_ui_mode = FALSE;
	gboolean should_ask = TRUE;
	g_autofree gchar *vpn_name = NULL;
	g_autofree gchar *vpn_uuid = NULL;
	g_autofree gchar *vpn_service = NULL;
	g_autofree gchar *setting_str = NULL;
	g_autofree gchar *keyfile_data = NULL;
	gsize length;
	g_autoptr(GOptionContext) context = NULL;
	g_autoptr(GHashTable) data = NULL;
	g_autoptr(GHashTable) secrets = NULL;
	g_autoptr(NMSettingVpn) setting_vpn = NULL;
	g_autoptr(GKeyFile) keyfile = NULL;
	g_autoptr(GError) error = NULL;
	const char *password;

	GOptionEntry entries[] = {
		{ "reprompt", 'r', 0, G_OPTION_ARG_NONE, &reprompt, "Reprompt for passwords", NULL},
		{ "uuid", 'u', 0, G_OPTION_ARG_STRING, &vpn_uuid, "UUID of VPN connection", NULL},
		{ "name", 'n', 0, G_OPTION_ARG_STRING, &vpn_name, "Name of VPN connection", NULL},
		{ "service", 's', 0, G_OPTION_ARG_STRING, &vpn_service, "VPN service type", NULL},
		{ "allow-interaction", 'i', 0, G_OPTION_ARG_NONE, &allow_interaction, "Allow user interaction", NULL},
		{ "external-ui-mode", 0, 0, G_OPTION_ARG_NONE, &external_ui_mode, "External UI mode", NULL},
		{ NULL }
	};

	context = g_option_context_new ("- novpn auth dialog");
	g_option_context_add_main_entries (context, entries, "Novpn");
	if (!g_option_context_parse (context, &argc, &argv, &error)) {
		g_printerr ("Bad command line options: %s\n", error->message);
		return EXIT_FAILURE;
	}

	if (!vpn_uuid || !vpn_service || !vpn_name) {
		g_printerr ("A connection UUID, name, and VPN plugin service name are required.\n");
		return EXIT_FAILURE;
	}

	if (!nm_vpn_service_plugin_read_vpn_details (0, &data, &secrets)) {
		g_printerr ("Failed to read '%s' (%s) data and secrets from stdin.\n", vpn_name, vpn_uuid);
		return EXIT_FAILURE;
	}

	setting_vpn = g_object_new (NM_TYPE_SETTING_VPN, "service-type", vpn_service, NULL);
	g_hash_table_foreach (data, _vpn_setting_add_data, setting_vpn);
	g_hash_table_foreach (secrets, _vpn_setting_add_secret, setting_vpn);

	setting_str = nm_setting_to_string (NM_SETTING (setting_vpn));
	g_printerr ("%s", setting_str);

	if (!allow_interaction)
		should_ask = FALSE;

	password = nm_setting_vpn_get_secret (setting_vpn, "password");
	if (password)
		should_ask = FALSE;

	if (reprompt)
		should_ask = TRUE;

	if (!allow_interaction)
		should_ask = FALSE;

	keyfile = g_key_file_new ();

	g_key_file_set_integer (keyfile, "VPN Plugin UI", "Version", 2);
	g_key_file_set_string (keyfile, "VPN Plugin UI", "Description", "Tell me all your secrets");
	g_key_file_set_string (keyfile, "VPN Plugin UI", "Title", "Authenticate VPN");

	g_key_file_set_string (keyfile, "password", "Label", "Password");
	if (password)
		g_key_file_set_string (keyfile, "password", "Value", password);
	g_key_file_set_boolean (keyfile, "password", "IsSecret", TRUE);
	g_key_file_set_boolean (keyfile, "password", "ShouldAsk", should_ask);

	keyfile_data = g_key_file_to_data (keyfile, &length, NULL);

	if (external_ui_mode) {
		g_print ("%s", keyfile_data);
	} else {
		if (!spawn_gui_helper (argv[0], keyfile_data, length, argv, &error)) {
			g_printerr ("Error: %s\n", error->message);
			return EXIT_FAILURE;
		}
	}

	return EXIT_SUCCESS;
}
