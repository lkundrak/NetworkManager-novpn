/*
 * nm-novpn-editor - GUI Editor widget for the NetworkManager mock
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

#include <NetworkManager.h>
#include <libnma/nma-cert-chooser.h>
#include <libnma/nma-ui-utils.h>
#include <gtk/gtk.h>

struct _NMNovpnEditor {
	GtkBox parent;

	GtkEntry *password_entry;
	GtkEntry *username_entry;
	GtkEntry *gateway_entry;
	NMACertChooser *ca_cert_chooser;
	GtkSizeGroup *labels;
};

struct _NMNovpnEditorClass {
	GtkBoxClass parent;
};

static void nm_novpn_editor_interface_init (NMVpnEditorInterface *iface_class);

#define NM_TYPE_NOVPN_EDITOR (nm_novpn_editor_get_type ())
G_DECLARE_FINAL_TYPE (NMNovpnEditor, nm_novpn_editor, NM, NOVPN_EDITOR, GtkBox)
G_DEFINE_TYPE_EXTENDED (NMNovpnEditor, nm_novpn_editor, GTK_TYPE_BOX, 0,
                        G_IMPLEMENT_INTERFACE (NM_TYPE_VPN_EDITOR,
                                               nm_novpn_editor_interface_init))

static GObject *
get_widget (NMVpnEditor *iface)
{
	return G_OBJECT (iface);
}

static gboolean
update_connection (NMVpnEditor *editor,
                   NMConnection *connection,
                   GError **error)
{
	NMNovpnEditor *self = NM_NOVPN_EDITOR (editor);
	NMSetting *setting;
	const gchar *gateway;
	const gchar *username;
	const gchar *password;
	g_autofree gchar *ca_cert_chooser = NULL;
	NMSettingSecretFlags password_flags = NM_SETTING_SECRET_FLAG_NONE;

	gateway = gtk_entry_get_text (self->gateway_entry);
	username = gtk_entry_get_text (self->username_entry);
	password = gtk_entry_get_text (self->password_entry);
	password_flags = nma_utils_menu_to_secret_flags (GTK_WIDGET (self->password_entry));
	ca_cert_chooser = nma_cert_chooser_get_cert_uri (self->ca_cert_chooser);

	setting = g_object_new (NM_TYPE_SETTING_VPN,
                                "service-type", "org.freedesktop.NetworkManager.Novpn",
                                NULL);

	if (gateway && gateway[0] != '\0')
		nm_setting_vpn_add_data_item (NM_SETTING_VPN (setting), "gateway", gateway);
	if (username && username[0] != '\0')
		nm_setting_vpn_add_data_item (NM_SETTING_VPN (setting), "username", username);
	if (password && password[0] != '\0')
		nm_setting_vpn_add_secret (NM_SETTING_VPN (setting), "password", password);
	if (!nm_setting_set_secret_flags (setting, "password", password_flags, error))
		return FALSE;
	if (ca_cert_chooser && ca_cert_chooser[0] != '\0')
		nm_setting_vpn_add_data_item (NM_SETTING_VPN (setting), "ca-cert", ca_cert_chooser);

        nm_connection_add_setting (connection, setting);
	return TRUE;
}

static void
nm_novpn_editor_init (NMNovpnEditor *self)
{
	gtk_widget_init_template (GTK_WIDGET (self));
	nma_cert_chooser_add_to_size_group (self->ca_cert_chooser, self->labels);
}

static void
nm_novpn_editor_class_init (NMNovpnEditorClass *klass)
{
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	g_type_ensure (NMA_TYPE_CERT_CHOOSER);

	gtk_widget_class_set_template_from_resource (widget_class,
		"/org/freedesktop/NetworkManager/Novpn/nm-novpn.ui");

	gtk_widget_class_bind_template_child (widget_class, NMNovpnEditor, password_entry);
	gtk_widget_class_bind_template_child (widget_class, NMNovpnEditor, username_entry);
	gtk_widget_class_bind_template_child (widget_class, NMNovpnEditor, gateway_entry);
	gtk_widget_class_bind_template_child (widget_class, NMNovpnEditor, ca_cert_chooser);
	gtk_widget_class_bind_template_child (widget_class, NMNovpnEditor, labels);
}

static void
nm_novpn_editor_interface_init (NMVpnEditorInterface *iface_class)
{
	iface_class->get_widget = get_widget;
	iface_class->update_connection = update_connection;
}

G_MODULE_EXPORT NMVpnEditor *
nm_vpn_editor_factory_novpn (NMVpnEditorPlugin *editor_plugin,
                             NMConnection *connection,
                             GError **error)
{
	NMSettingVpn *setting_vpn;
	NMNovpnEditor *self;
	const gchar *gateway;
	const gchar *username;
	const gchar *password;
	NMSettingSecretFlags password_flags = NM_SETTING_SECRET_FLAG_NONE;
	const gchar *ca_cert;

	g_return_val_if_fail (!error || !*error, NULL);

	self = g_object_new (NM_TYPE_NOVPN_EDITOR, NULL);
	gtk_entry_set_text (self->gateway_entry, "novpn.example.com");
	nm_connection_dump (connection);

	setting_vpn = nm_connection_get_setting_vpn (connection);
	if (!setting_vpn) {
		g_warning ("Connection had no VPN setting. Proceeding with an empty one.\n");
		setting_vpn = NM_SETTING_VPN (nm_setting_vpn_new ());
	}

	gateway = nm_setting_vpn_get_data_item (setting_vpn, "gateway");
	username = nm_setting_vpn_get_data_item (setting_vpn, "username");
	password = nm_setting_vpn_get_secret (setting_vpn, "password");
	nm_setting_get_secret_flags (NM_SETTING (setting_vpn),
	                             "password", &password_flags, NULL);
	ca_cert = nm_setting_vpn_get_data_item (setting_vpn, "ca-cert");

	if (gateway && gateway[0] != '\0')
		gtk_entry_set_text (self->gateway_entry, gateway);
	if (username && username[0] != '\0')
		gtk_entry_set_text (self->username_entry, username);
	if (password && password[0] != '\0')
		gtk_entry_set_text (self->password_entry, password);
	nma_utils_setup_password_storage (GTK_WIDGET (self->password_entry),
	                                  password_flags,
	                                  NULL, NULL, TRUE, FALSE);
	if (ca_cert && ca_cert[0] != '\0')
		nma_cert_chooser_set_cert_uri (self->ca_cert_chooser, ca_cert);

	return NM_VPN_EDITOR (g_object_ref_sink (self));
}
