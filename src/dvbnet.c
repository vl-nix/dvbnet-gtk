/*
* Copyright 2020 Stepan Perun
* This program is free software.
*
* License: Gnu General Public License GPL-3
* file:///usr/share/common-licenses/GPL-3
* http://www.gnu.org/licenses/gpl-3.0.html
*/

#include "dvbnet.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#include <linux/dvb/net.h>

#define MAX_IF 10

enum mode 
{
	SET_IP,
	SET_MAC,
	DEL_IF
};

struct _Dvbnet
{
	GtkApplication  parent_instance;

	GtkWindow *window;

	GtkLabel *label_if;
	GtkEntry *entry_ip;
	GtkEntry *entry_mac;

	int net_fd;
	uint8_t  dvb_adapter, dvb_net, net_ens;
	uint16_t net_pid, if_num;
};

G_DEFINE_TYPE (Dvbnet, dvbnet, GTK_TYPE_APPLICATION)

static void dvbnet_about ( Dvbnet *dvbnet )
{
	GtkAboutDialog *dialog = (GtkAboutDialog *)gtk_about_dialog_new ();
	gtk_window_set_transient_for ( GTK_WINDOW ( dialog ), dvbnet->window );

	gtk_window_set_icon_name ( GTK_WINDOW ( dialog ), "applications-internet" );
	gtk_about_dialog_set_logo_icon_name ( dialog, "applications-internet" );

	const char *authors[] = { "Stepan Perun", " ", NULL };

	gtk_about_dialog_set_program_name ( dialog, "DvbNet-Gtk" );
	gtk_about_dialog_set_version ( dialog, "1.1.1" );
	gtk_about_dialog_set_license_type ( dialog, GTK_LICENSE_LGPL_3_0 );
	gtk_about_dialog_set_authors ( dialog, authors );
	gtk_about_dialog_set_website ( dialog,   "https://github.com/vl-nix/dvbnet-gtk" );
	gtk_about_dialog_set_copyright ( dialog, "Copyright 2020 DvbNet-Gtk" );
	gtk_about_dialog_set_comments  ( dialog, "Control digital data network interfaces" );

	gtk_dialog_run ( GTK_DIALOG (dialog) );
	gtk_widget_destroy ( GTK_WIDGET (dialog) );
}

static void dvbnet_message_dialog ( const char *error, const char *file_or_info, GtkMessageType mesg_type, GtkWindow *window )
{
	GtkMessageDialog *dialog = ( GtkMessageDialog *)gtk_message_dialog_new (
		window, GTK_DIALOG_MODAL, mesg_type, GTK_BUTTONS_CLOSE, "%s\n%s", error, file_or_info );

	gtk_dialog_run     ( GTK_DIALOG ( dialog ) );
	gtk_widget_destroy ( GTK_WIDGET ( dialog ) );
}

static int dvb_net_open ( Dvbnet *dvbnet )
{
	char file[80];
	sprintf ( file, "/dev/dvb/adapter%i/net%i", dvbnet->dvb_adapter, dvbnet->dvb_net );

	int fd = open ( file, O_RDWR );

	if ( fd == -1 )
	{
		perror ( "Open net device failed" );
		dvbnet_message_dialog ( file, g_strerror ( errno ), GTK_MESSAGE_ERROR, dvbnet->window );
	}

	return fd;
}

static int dvb_net_get_if_info ( int fd, uint16_t ifnum, uint16_t *pid, uint8_t *encaps )
{
	struct dvb_net_if info;

	memset ( &info, 0, sizeof(struct dvb_net_if) );
	info.if_num = ifnum;

	int ret = ioctl ( fd, NET_GET_IF, &info );

	if ( ret == -1 ) return ret;

	*pid = info.pid;
	*encaps = info.feedtype;

	return 0;
}

static void dvb_net_set_if_info ( Dvbnet *dvbnet )
{
	dvbnet->net_fd = dvb_net_open ( dvbnet );

	if ( dvbnet->net_fd == -1 ) return;

	/* 250 * MAX_IF */
	char text[2500] = {}, *p = NULL;

	uint16_t ifs = 0; for ( ifs = 0; ifs < MAX_IF; ifs++ )
	{
		uint16_t pid = 0;
		uint8_t encaps = 0;

		int ret = dvb_net_get_if_info ( dvbnet->net_fd, ifs, &pid, &encaps );

		if ( ret == -1 ) continue;

		if ( ifs == 0 )
		{
			sprintf ( text, "IF:  dvb%d_%d;  Pid:  %d;  Encapsulation:  %s\n",
				dvbnet->dvb_adapter, ifs, pid, ( encaps ) ? "Ule" : "Mpe" );
		}
		else
		{
			p = text;
			sprintf ( text, "%sIF:  dvb%d_%d;  Pid:  %d;  Encapsulation:  %s\n", p,
				dvbnet->dvb_adapter, ifs, pid, ( encaps ) ? "Ule" : "Mpe" );
		}
	}

	if ( text[0] )
		gtk_label_set_text ( dvbnet->label_if, text );
	else
		gtk_label_set_text ( dvbnet->label_if, "Status" );

	close ( dvbnet->net_fd );
}

static void dvb_net_del_if ( Dvbnet *dvbnet )
{
	char cmd[250];
	sprintf ( cmd, "ip link set dvb%u_%u down", dvbnet->dvb_adapter, dvbnet->if_num );	

	system ( cmd );
	sleep  ( 1 );

	int ret = ioctl ( dvbnet->net_fd, NET_REMOVE_IF, dvbnet->if_num );

	if ( ret == -1 )
	{
		perror ( "NET_REMOVE_IF" );
		dvbnet_message_dialog ( "NET_REMOVE_IF", g_strerror ( errno ), GTK_MESSAGE_ERROR, dvbnet->window );
	}
}

static int dvb_net_add_if ( Dvbnet *dvbnet )
{
	struct dvb_net_if params;

	memset ( &params, 0, sizeof(params) );
	params.pid = dvbnet->net_pid;
	params.feedtype = ( dvbnet->net_ens ) ? DVB_NET_FEEDTYPE_ULE : DVB_NET_FEEDTYPE_MPE;

	int ret = ioctl ( dvbnet->net_fd, NET_ADD_IF, &params );

	if ( ret == -1 )
	{
		perror ( "NET_ADD_IF" );
		dvbnet_message_dialog ( "NET_ADD_IF", g_strerror ( errno ), GTK_MESSAGE_ERROR, dvbnet->window );
	}

	return ret;
}

static void dvb_net_add ( Dvbnet *dvbnet )
{
	dvbnet->net_fd = dvb_net_open ( dvbnet );

	if ( dvbnet->net_fd == -1 ) return;

	dvb_net_add_if ( dvbnet );

	close ( dvbnet->net_fd );
}

static void dvb_net_set_ip ( G_GNUC_UNUSED GtkButton *button, Dvbnet *dvbnet )
{
	char cmd[250];
	sprintf ( cmd, "ifconfig dvb%u_%u %s", dvbnet->dvb_adapter, dvbnet->if_num, gtk_entry_get_text ( dvbnet->entry_ip ) );	

	system ( cmd );
}

static void dvb_net_set_mac ( G_GNUC_UNUSED GtkButton *button, Dvbnet *dvbnet )
{
	char cmd[250];
	sprintf ( cmd, "ifconfig dvb%u_%u hw ether %s", dvbnet->dvb_adapter, dvbnet->if_num, gtk_entry_get_text ( dvbnet->entry_mac ) );	

	system ( cmd );
}

static void dvb_net_del_if_num_run ( G_GNUC_UNUSED GtkButton *button, Dvbnet *dvbnet )
{
	dvbnet->net_fd = dvb_net_open ( dvbnet );

	if ( dvbnet->net_fd == -1 ) return;

	dvb_net_del_if ( dvbnet );

	close ( dvbnet->net_fd );

	dvb_net_set_if_info ( dvbnet );
}

static void dvb_net_del_changed_if_num ( GtkSpinButton *button, Dvbnet *dvbnet )
{
	dvbnet->if_num = (uint16_t)gtk_spin_button_get_value_as_int ( button );
}

static void dvb_net_act_if_num ( enum mode act, Dvbnet *dvbnet )
{
	GtkWindow *window = (GtkWindow *)gtk_window_new ( GTK_WINDOW_TOPLEVEL );
	gtk_window_set_title ( window, "DvbNet interface" );
	gtk_window_set_modal ( window, TRUE );
	gtk_window_set_default_size ( window, 300, 100 );
	gtk_window_set_icon_name ( window, "applications-internet" );

	GtkBox *m_box = (GtkBox *)gtk_box_new ( GTK_ORIENTATION_VERTICAL, 0 );
	gtk_box_set_spacing ( m_box, 5 );

	GtkSpinButton *spinbutton = (GtkSpinButton *)gtk_spin_button_new_with_range ( 0, MAX_IF-1, 1 );
	gtk_spin_button_set_value ( spinbutton, dvbnet->if_num );
	g_signal_connect ( spinbutton, "changed", G_CALLBACK ( dvb_net_del_changed_if_num ), dvbnet );

	gtk_box_pack_start ( m_box, GTK_WIDGET ( spinbutton ), FALSE, FALSE, 0 );

	GtkBox *h_box = (GtkBox *)gtk_box_new ( GTK_ORIENTATION_HORIZONTAL, 0 );
	gtk_box_set_spacing ( h_box, 5 );

	GtkButton *button = (GtkButton *)gtk_button_new_with_label ( "⏻" );
	g_signal_connect_swapped ( button, "clicked", G_CALLBACK ( gtk_widget_destroy ), window );
	gtk_box_pack_start ( h_box, GTK_WIDGET ( button ), TRUE, TRUE, 0 );

	if ( act == DEL_IF )
	{
		button = (GtkButton *)gtk_button_new_with_label ( "➖" );
		g_signal_connect ( button, "clicked", G_CALLBACK ( dvb_net_del_if_num_run ), dvbnet );
	}

	if ( act == SET_IP )
	{
		button = (GtkButton *)gtk_button_new_with_label ( "Set IP" );
		g_signal_connect ( button, "clicked", G_CALLBACK ( dvb_net_set_ip ), dvbnet );
	}

	if ( act == SET_MAC )
	{
		button = (GtkButton *)gtk_button_new_with_label ( "Set Mac" );
		g_signal_connect ( button, "clicked", G_CALLBACK ( dvb_net_set_mac ), dvbnet );
	}

	g_signal_connect_swapped ( button, "clicked", G_CALLBACK ( gtk_widget_destroy ), window );
	gtk_box_pack_end ( h_box, GTK_WIDGET ( button ), TRUE, TRUE, 0 );

	gtk_box_pack_end ( m_box, GTK_WIDGET ( h_box ), FALSE, FALSE, 0 );

	gtk_container_set_border_width ( GTK_CONTAINER ( m_box ), 10 );
	gtk_container_add ( GTK_CONTAINER ( window ), GTK_WIDGET ( m_box ) );

	gtk_widget_show_all ( GTK_WIDGET ( window ) );
}

static void dvbnet_clicked_button_net_ip ( G_GNUC_UNUSED GtkButton *button, Dvbnet *dvbnet )
{
	dvb_net_act_if_num ( SET_IP, dvbnet );
}

static void dvbnet_clicked_button_net_mac ( G_GNUC_UNUSED GtkButton *button, Dvbnet *dvbnet )
{
	dvb_net_act_if_num ( SET_MAC, dvbnet );
}

static void dvb_net_del ( Dvbnet *dvbnet )
{
	dvb_net_act_if_num ( DEL_IF, dvbnet );
}

static void dvbnet_clicked_button_net_add ( G_GNUC_UNUSED GtkButton *button, Dvbnet *dvbnet )
{
	dvb_net_add ( dvbnet );
	dvb_net_set_if_info ( dvbnet );
}

static void dvbnet_clicked_button_net_rld ( G_GNUC_UNUSED GtkButton *button, Dvbnet *dvbnet )
{
	dvb_net_set_if_info ( dvbnet );
}

static void dvbnet_clicked_button_net_del ( G_GNUC_UNUSED GtkButton *button, Dvbnet *dvbnet )
{
	dvb_net_del ( dvbnet );
}

static void dvbnet_clicked_button_net_inf ( G_GNUC_UNUSED GtkButton *button, Dvbnet *dvbnet )
{
	dvbnet_about ( dvbnet );
}

static void dvbnet_spinbutton_changed_dvb_adapter ( GtkSpinButton *button, Dvbnet *dvbnet )
{
	gtk_spin_button_update ( button );

	dvbnet->dvb_adapter = (uint8_t)gtk_spin_button_get_value_as_int ( button );
}
static void dvbnet_spinbutton_changed_dvb_net ( GtkSpinButton *button, Dvbnet *dvbnet )
{
	gtk_spin_button_update ( button );

	dvbnet->dvb_net = (uint8_t)gtk_spin_button_get_value_as_int ( button );
}

static void dvbnet_spinbutton_changed_dvb_pid ( GtkSpinButton *button, Dvbnet *dvbnet )
{
	gtk_spin_button_update ( button );

	dvbnet->net_pid = (uint16_t)gtk_spin_button_get_value_as_int ( button );
}

static void dvbnet_combo_changed_dvb_ens ( GtkComboBoxText *combo_box, Dvbnet *dvbnet )
{
	dvbnet->net_ens = (uint8_t)gtk_combo_box_get_active ( GTK_COMBO_BOX ( combo_box ) );
}

static GtkBox * dvbnet_create_net_box_props ( Dvbnet *dvbnet )
{
	GtkBox *v_box = (GtkBox *)gtk_box_new ( GTK_ORIENTATION_VERTICAL, 0 );
	gtk_box_set_spacing ( v_box, 5 );

	GtkGrid *grid = (GtkGrid *)gtk_grid_new ();
	// gtk_grid_set_row_homogeneous    ( GTK_GRID ( grid ), TRUE );
	// gtk_grid_set_column_homogeneous ( GTK_GRID ( grid ), TRUE );
	gtk_grid_set_row_spacing ( grid, 5 );
	gtk_grid_set_column_spacing ( grid, 10 );

	gtk_widget_set_margin_top    ( GTK_WIDGET ( grid ), 10 );
	gtk_widget_set_margin_bottom ( GTK_WIDGET ( grid ), 10 );
	gtk_widget_set_margin_start  ( GTK_WIDGET ( grid ), 10 );
	gtk_widget_set_margin_end    ( GTK_WIDGET ( grid ), 10 );

	gtk_box_pack_start ( v_box, GTK_WIDGET ( grid ), FALSE, FALSE, 0 );

	GtkLabel *label = (GtkLabel *)gtk_label_new ( "Adapter" );
	gtk_widget_set_halign ( GTK_WIDGET ( label ), GTK_ALIGN_START );

	GtkSpinButton *spinbutton = (GtkSpinButton *)gtk_spin_button_new_with_range ( 0, 16, 1 );
	gtk_spin_button_set_value ( spinbutton, 0 );
	g_signal_connect ( spinbutton, "changed", G_CALLBACK ( dvbnet_spinbutton_changed_dvb_adapter ), dvbnet );

	gtk_grid_attach ( GTK_GRID ( grid ), GTK_WIDGET ( label      ), 0, 0, 1, 1 );
	gtk_grid_attach ( GTK_GRID ( grid ), GTK_WIDGET ( spinbutton ), 1, 0, 1, 1 );

	label = (GtkLabel *)gtk_label_new ( "Net" );
	gtk_widget_set_halign ( GTK_WIDGET ( label ), GTK_ALIGN_START );

	spinbutton = (GtkSpinButton *)gtk_spin_button_new_with_range ( 0, 16, 1 );
	gtk_spin_button_set_value ( spinbutton, 0 );
	g_signal_connect ( spinbutton, "changed", G_CALLBACK ( dvbnet_spinbutton_changed_dvb_net ), dvbnet );

	gtk_grid_attach ( GTK_GRID ( grid ), GTK_WIDGET ( label      ), 2, 0, 1, 1 );
	gtk_grid_attach ( GTK_GRID ( grid ), GTK_WIDGET ( spinbutton ), 3, 0, 1, 1 );

	label = (GtkLabel *)gtk_label_new ( "Pid" );
	gtk_widget_set_halign ( GTK_WIDGET ( label ), GTK_ALIGN_START );

	spinbutton = (GtkSpinButton *)gtk_spin_button_new_with_range ( 0, UINT16_MAX, 1 );
	gtk_spin_button_set_value ( spinbutton, dvbnet->net_pid );
	g_signal_connect ( spinbutton, "changed", G_CALLBACK ( dvbnet_spinbutton_changed_dvb_pid ), dvbnet );

	gtk_grid_attach ( GTK_GRID ( grid ), GTK_WIDGET ( label      ), 0, 1, 1, 1 );
	gtk_grid_attach ( GTK_GRID ( grid ), GTK_WIDGET ( spinbutton ), 1, 1, 1, 1 );

	label = (GtkLabel *)gtk_label_new ( "Encaps" ); // Encapsulation
	gtk_widget_set_halign ( GTK_WIDGET ( label ), GTK_ALIGN_START );

	GtkComboBoxText *combo = (GtkComboBoxText *) gtk_combo_box_text_new ();
	gtk_combo_box_text_append ( combo, "MPE", "Mpe - multi" );
	gtk_combo_box_text_append ( combo, "ULE", "Ule - ultra" );
	gtk_combo_box_set_active ( GTK_COMBO_BOX ( combo ), 0 );
	g_signal_connect ( combo,  "changed", G_CALLBACK ( dvbnet_combo_changed_dvb_ens ), dvbnet );

	gtk_grid_attach ( GTK_GRID ( grid ), GTK_WIDGET ( label ), 2, 1, 1, 1 );
	gtk_grid_attach ( GTK_GRID ( grid ), GTK_WIDGET ( combo ), 3, 1, 1, 1 );

	GtkButton *button_ip = (GtkButton *)gtk_button_new_with_label ( "Set IP" );
	g_signal_connect ( button_ip, "clicked", G_CALLBACK ( dvbnet_clicked_button_net_ip ), dvbnet );

	dvbnet->entry_ip = (GtkEntry *)gtk_entry_new ();
	gtk_entry_set_text ( dvbnet->entry_ip, "10.1.1.1" );

	gtk_grid_attach ( GTK_GRID ( grid ), GTK_WIDGET ( button_ip ), 0, 2, 1, 1 );
	gtk_grid_attach ( GTK_GRID ( grid ), GTK_WIDGET ( dvbnet->entry_ip ), 1, 2, 1, 1 );

	GtkButton *button_mac = (GtkButton *)gtk_button_new_with_label ( "Set MAC" );
	g_signal_connect ( button_mac, "clicked", G_CALLBACK ( dvbnet_clicked_button_net_mac ), dvbnet );

	dvbnet->entry_mac = (GtkEntry *)gtk_entry_new ();
	gtk_entry_set_text ( dvbnet->entry_mac, "00:01:02:03:04:05" );

	gtk_grid_attach ( GTK_GRID ( grid ), GTK_WIDGET ( button_mac ), 2, 2, 1, 1 );
	gtk_grid_attach ( GTK_GRID ( grid ), GTK_WIDGET ( dvbnet->entry_mac ), 3, 2, 1, 1 );

	return v_box;
}

static GtkBox * dvbnet_create_net_box_status ( Dvbnet *dvbnet )
{
	GtkBox *v_box = (GtkBox *)gtk_box_new ( GTK_ORIENTATION_VERTICAL, 0 );
	gtk_widget_set_margin_top    ( GTK_WIDGET ( v_box ), 10 );
	gtk_widget_set_margin_bottom ( GTK_WIDGET ( v_box ), 10 );

	dvbnet->label_if = (GtkLabel *)gtk_label_new ( "Status" );
	gtk_box_pack_start ( v_box, GTK_WIDGET ( dvbnet->label_if ), FALSE, FALSE, 0 );

	return v_box;
}

static GtkBox * dvbnet_create_net_box_control ( Dvbnet *dvbnet )
{
	GtkBox *v_box = (GtkBox *)gtk_box_new ( GTK_ORIENTATION_VERTICAL, 0 );
	gtk_box_set_spacing ( v_box, 5 );

	GtkBox *h_box = (GtkBox *)gtk_box_new ( GTK_ORIENTATION_HORIZONTAL, 0 );
	gtk_box_set_spacing ( h_box, 5 );

	gtk_widget_set_margin_start  ( GTK_WIDGET ( h_box ), 10 );
	gtk_widget_set_margin_end    ( GTK_WIDGET ( h_box ), 10 );
	gtk_widget_set_margin_bottom ( GTK_WIDGET ( h_box ), 10 );

	GtkButton *button_add = (GtkButton *)gtk_button_new_with_label ( "➕" );
	GtkButton *button_rld = (GtkButton *)gtk_button_new_with_label ( "🔃" );
	GtkButton *button_del = (GtkButton *)gtk_button_new_with_label ( "➖" );
	GtkButton *button_inf = (GtkButton *)gtk_button_new_with_label ( "🛈" );

	g_signal_connect ( button_add, "clicked", G_CALLBACK ( dvbnet_clicked_button_net_add ), dvbnet );
	g_signal_connect ( button_rld, "clicked", G_CALLBACK ( dvbnet_clicked_button_net_rld ), dvbnet );
	g_signal_connect ( button_del, "clicked", G_CALLBACK ( dvbnet_clicked_button_net_del ), dvbnet );
	g_signal_connect ( button_inf, "clicked", G_CALLBACK ( dvbnet_clicked_button_net_inf ), dvbnet );

	gtk_box_pack_start ( h_box, GTK_WIDGET ( button_add ), TRUE, TRUE,  0 );
	gtk_box_pack_start ( h_box, GTK_WIDGET ( button_rld ), TRUE, TRUE,  0 );
	gtk_box_pack_start ( h_box, GTK_WIDGET ( button_del ), TRUE, TRUE,  0 );
	gtk_box_pack_start ( h_box, GTK_WIDGET ( button_inf ), TRUE, TRUE,  0 );

	gtk_box_pack_start ( v_box, GTK_WIDGET ( h_box ), FALSE, FALSE, 0 );

	return v_box;
}

static void dvbnet_new_window ( GApplication *app )
{
	Dvbnet *dvbnet = DVBNET_APPLICATION ( app );

	dvbnet->window = (GtkWindow *)gtk_application_window_new ( GTK_APPLICATION ( app ) );
	gtk_window_set_title ( dvbnet->window, "DvbNet-Gtk" );
	gtk_window_set_icon_name ( dvbnet->window, "applications-internet" );

	GtkBox *main_vbox = (GtkBox *)gtk_box_new ( GTK_ORIENTATION_VERTICAL, 0 );
	gtk_box_set_spacing ( main_vbox, 5 );

	GtkBox *net_box_p = dvbnet_create_net_box_props ( dvbnet );
	gtk_box_pack_start ( main_vbox, GTK_WIDGET ( net_box_p ), FALSE, FALSE, 0 );

	GtkBox *net_box_s = dvbnet_create_net_box_status ( dvbnet );
	gtk_box_pack_start ( main_vbox, GTK_WIDGET ( net_box_s ), FALSE, FALSE, 0 );

	GtkBox *net_box_c = dvbnet_create_net_box_control ( dvbnet );
	gtk_box_pack_end ( main_vbox, GTK_WIDGET ( net_box_c ), FALSE, FALSE, 0 );

	gtk_container_set_border_width ( GTK_CONTAINER ( main_vbox ), 10 );
	gtk_container_add   ( GTK_CONTAINER ( dvbnet->window ), GTK_WIDGET ( main_vbox ) );

	gtk_widget_show_all ( GTK_WIDGET ( dvbnet->window ) );

	dvb_net_set_if_info ( dvbnet );
}


static void dvbnet_activate ( GApplication *app )
{
	dvbnet_new_window ( app );
}

static void dvbnet_init ( Dvbnet *dvbnet )
{
	dvbnet->dvb_adapter = 0;
	dvbnet->dvb_net = 0;
	dvbnet->net_pid = 0;
	dvbnet->if_num  = 0;
	dvbnet->net_ens = 0;
	dvbnet->net_fd  = -1;
}

static void dvbnet_finalize ( GObject *object )
{
	G_OBJECT_CLASS (dvbnet_parent_class)->finalize (object);
}

static void dvbnet_class_init ( DvbnetClass *class )
{
	G_APPLICATION_CLASS (class)->activate = dvbnet_activate;

	G_OBJECT_CLASS (class)->finalize = dvbnet_finalize;
}

static Dvbnet * dvbnet_new (void)
{
	return g_object_new ( DVBNET_TYPE_APPLICATION, /*"application-id", "org.gnome.dvbnet-gtk",*/ "flags", G_APPLICATION_FLAGS_NONE, NULL );
}

int main (void)
{
	Dvbnet *app = dvbnet_new ();

	int status = g_application_run ( G_APPLICATION (app), 0, NULL );

	g_object_unref (app);

	return status;
}
