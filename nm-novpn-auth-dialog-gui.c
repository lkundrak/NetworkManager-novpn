/*
 * nm-novpn-auth-dialog-gui - Compatibility GUI authentication handler
 * handler for the NetworkManager mock VPN service
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
#include <unistd.h>
#include <stdio.h>
#include <glib.h>
#include <nma-vpn-password-dialog.h>

typedef void (*VpnDialogCallback) (GKeyFile *keyfile,
                                   gpointer user_data);

typedef struct {
	GKeyFile *keyfile;
	VpnDialogCallback callback;
	gpointer user_data;
	int added_groups[3];
	int passwords;
} VpnDialogData;

static gboolean
add_password (NMAVpnPasswordDialog *dialog,
              int passwords,
              GKeyFile *keyfile,
              const gchar *group,
              GError **error)
{
	g_autofree gchar *label = NULL;
	g_autofree gchar *value = NULL;
	gboolean is_secret;
	gboolean should_ask;

	is_secret = g_key_file_get_boolean (keyfile, group, "IsSecret", NULL);
	should_ask = g_key_file_get_boolean (keyfile, group, "ShouldAsk", NULL);
	if (!should_ask || !is_secret)
		return FALSE;

	label = g_key_file_get_string (keyfile, group, "Label", error);
	if (!label)
		return FALSE;

	value = g_key_file_get_string (keyfile, group, "Value", NULL);

	switch (passwords) {
	case 0:
		nma_vpn_password_dialog_set_show_password (dialog, TRUE);
		nma_vpn_password_dialog_set_password_label (dialog, label);
		if (value)
			nma_vpn_password_dialog_set_password (dialog, value);
		break;
	case 1:
		nma_vpn_password_dialog_set_show_password_secondary (dialog, TRUE);
		nma_vpn_password_dialog_set_password_secondary_label (dialog, label);
		if (value)
			nma_vpn_password_dialog_set_password_secondary (dialog, value);
		break;
	case 2:
		nma_vpn_password_dialog_set_show_password_ternary (dialog, TRUE);
		nma_vpn_password_dialog_set_password_ternary_label (dialog, label);
		if (value)
			nma_vpn_password_dialog_set_password_ternary (dialog, value);
		break;
	default:
		g_set_error_literal (error, -1, -1,
		                     "More than 3 passwords are not supported.");
		return FALSE;
	}

	return TRUE;
}

static const char *
get_password (NMAVpnPasswordDialog *dialog,
              int password_no)
{
	switch (password_no) {
	case 0:
		return nma_vpn_password_dialog_get_password (dialog);
	case 1:
		return nma_vpn_password_dialog_get_password_secondary (dialog);
	case 2:
		return nma_vpn_password_dialog_get_password_ternary (dialog);
	default:
		g_return_val_if_reached ("");
	}
}

static void
free_vpn_dialog_data (gpointer data,
                      GClosure *closure)
{
	VpnDialogData *dialog_data = data;

	g_key_file_unref (dialog_data->keyfile);
	g_slice_free (VpnDialogData, dialog_data);
}

static void
dialog_response (GtkDialog *dialog,
                 gint response_id,
                 gpointer user_data)
{
	VpnDialogData *dialog_data = user_data;
	g_auto(GStrv) groups = g_key_file_get_groups (dialog_data->keyfile, NULL);
	int i;

	for (i = 0; i < dialog_data->passwords; i++) {
		g_key_file_set_string (dialog_data->keyfile, groups[dialog_data->added_groups[i]],
		                       "Value", get_password (NMA_VPN_PASSWORD_DIALOG (dialog), i));
	}

	dialog_data->callback (dialog_data->keyfile, user_data);
}

static GtkWidget *
dialog_from_io_channel (GIOChannel *input,
                        VpnDialogCallback callback,
                        gpointer user_data,
                        GError **error)
{
	GIOStatus status;
	g_autofree gchar *data = NULL;
	gsize len;
	g_autoptr(GKeyFile) keyfile = NULL;
	g_auto(GStrv) groups = NULL;
	GtkWidget *dialog = NULL;
	g_autofree gchar *version = NULL;
	g_autofree gchar *title = NULL;
	g_autofree gchar *message = NULL;
	g_autofree gchar *value = NULL;
	VpnDialogData *dialog_data;
	int i;

	input = g_io_channel_unix_new (STDIN_FILENO);
	g_return_val_if_fail (input, NULL);

	status = g_io_channel_read_to_end (input, &data, &len, error);
	if (status == G_IO_STATUS_ERROR)
		return NULL;
	g_return_val_if_fail (status == G_IO_STATUS_NORMAL, NULL);

	keyfile = g_key_file_new ();
	g_return_val_if_fail (keyfile, NULL);

	if (!g_key_file_load_from_data (keyfile, data, len, G_KEY_FILE_NONE, error))
		return NULL;

	groups = g_key_file_get_groups (keyfile, NULL);
	if (g_strcmp0 (groups[0], "VPN Plugin UI") != 0) {
		g_set_error_literal (error, -1, -1, "Expected [VPN Plugin UI]");
		return NULL;
	}

	version = g_key_file_get_string (keyfile, "VPN Plugin UI", "Version", error);
	if (!version)
		return NULL;
	if (strcmp (version, "2") != 0) {
		g_set_error_literal (error, -1, -1, "Expected Version=2");
		return NULL;
	}

	title = g_key_file_get_string (keyfile, "VPN Plugin UI", "Title", error);
	if (!title)
		return NULL;

	message = g_key_file_get_string (keyfile, "VPN Plugin UI", "Description", error);
	if (!message)
		return NULL;

	dialog_data = g_slice_alloc0 (sizeof (VpnDialogData));
	g_return_val_if_fail (dialog_data, NULL);
	dialog_data->callback = callback;
	dialog_data->user_data = user_data;
	dialog_data->keyfile = g_key_file_ref (keyfile);

	dialog = nma_vpn_password_dialog_new (title, message, NULL);
	nma_vpn_password_dialog_set_show_password (NMA_VPN_PASSWORD_DIALOG (dialog), FALSE);
	nma_vpn_password_dialog_set_show_password_secondary (NMA_VPN_PASSWORD_DIALOG (dialog), FALSE);
	nma_vpn_password_dialog_set_show_password_ternary (NMA_VPN_PASSWORD_DIALOG (dialog), FALSE);

	g_signal_connect_data (dialog, "response", G_CALLBACK (dialog_response), dialog_data, free_vpn_dialog_data, 0);

	for (i = 1; groups[i] != NULL; i++) {
		g_autoptr(GError) local = NULL;

		if (add_password (NMA_VPN_PASSWORD_DIALOG (dialog), dialog_data->passwords, keyfile, groups[i], &local)) {
			dialog_data->added_groups[dialog_data->passwords] = i;
			dialog_data->passwords++;
		} else if (local) {
			g_printerr ("Skipping entry: %s\n", local->message);
		}
	}

	return g_object_ref_sink (dialog);
}

static void
got_secrets (GKeyFile *keyfile, gpointer user_data)
{
	g_auto(GStrv) groups = g_key_file_get_groups (keyfile, NULL);
	g_autofree gchar *value = NULL;
	int i;

	for (i = 1; groups[i] != NULL; i++) {
		value = g_key_file_get_string (keyfile, groups[i], "Value", NULL);
		if (!value)
			continue;

		g_print ("%s\n", groups[i]);
		g_print ("%s\n", value);
		g_free (value);
	}

	g_print ("\n\n");
}

int
main (int argc, char *argv[])
{
	g_autoptr(GIOChannel) input = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GtkWidget) dialog = NULL;

	input = g_io_channel_unix_new (STDIN_FILENO);
	g_return_val_if_fail (input, EXIT_FAILURE);

	gtk_init (&argc, &argv);

	dialog = dialog_from_io_channel (input, got_secrets, NULL, &error);
	if (!dialog) {
		g_printerr ("Error: %s\n", error->message);
		return EXIT_FAILURE;
	}

	if (!nma_vpn_password_dialog_run_and_block (NMA_VPN_PASSWORD_DIALOG (dialog)))
		return EXIT_FAILURE;

	return EXIT_SUCCESS;
}
