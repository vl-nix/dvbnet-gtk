/*
* Copyright 2020 Stepan Perun
* This program is free software.
*
* License: Gnu General Public License GPL-3
* file:///usr/share/common-licenses/GPL-3
* http://www.gnu.org/licenses/gpl-3.0.html
*/

#include <math.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#include <net/if.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <net/if_arp.h>
#include <netinet/in.h>

#include <linux/dvb/net.h>

#include <gtk/gtk.h>

#define DVBNET_TYPE_APPLICATION dvbnet_get_type()

G_DECLARE_FINAL_TYPE ( Dvbnet, dvbnet, DVBNET, APPLICATION, GtkApplication )

enum mode 
{
	SET_IP,
	SET_MAC,
	DEL_IF
};

enum cols_n
{
	COL_NUM,
	COL_NAME,
	COL_PID,
	COL_ECPS,
	COL_STR_IP,
	COL_STR_MAC,
	NUM_COLS
};

struct _Dvbnet
{
	GtkApplication  parent_instance;

	GtkWindow *window;
	GtkEntry *entry_ip;
	GtkEntry *entry_mac;
	GtkTreeView *treeview;

	uint16_t net_pid;
	uint8_t  dvb_adapter, dvb_net, if_num, net_ens;
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
	gtk_about_dialog_set_version ( dialog, "1.1.2" );
	gtk_about_dialog_set_license_type ( dialog, GTK_LICENSE_GPL_3_0 );
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

static int dvbnet_open ( Dvbnet *dvbnet )
{
	char file[80] = {};
	sprintf ( file, "/dev/dvb/adapter%u/net%u", dvbnet->dvb_adapter, dvbnet->dvb_net );

	int fd = open ( file, O_RDWR );

	if ( fd == -1 )
	{
		perror ( "Open net device failed" );
		dvbnet_message_dialog ( file, g_strerror ( errno ), GTK_MESSAGE_ERROR, dvbnet->window );
	}

	return fd;
}

static int dvbnet_get_if_info ( int fd, uint8_t ifnum, uint16_t *pid, uint8_t *encaps )
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

static char * dvbnet_get_mac_ip ( const char *net_name, uint8_t mac_ip )
{
	struct ifreq ifr;

	int fd = socket ( AF_INET, SOCK_DGRAM, 0 );

	if ( fd < 0 ) { perror ( "socket" ); return NULL; }

	memset ( &ifr, 0x00, sizeof(ifr) );
	strcpy ( ifr.ifr_name, net_name  );

	if ( mac_ip )
	{
		if ( ioctl ( fd, SIOCGIFHWADDR, &ifr ) < 0 ) { close ( fd ); return NULL; }

		char *ret = malloc ( 18 );

		uint8_t i = 0; for ( i = 0; i < 6; ++i )
			snprintf ( ret + (i*3), (size_t)(18 - (i*3)), "%02x%s", (uint8_t)ifr.ifr_addr.sa_data[i], ":" );

		close ( fd );

		return ret;
	}
	else
	{
		if ( ioctl ( fd, SIOCGIFADDR, &ifr ) < 0 ) { close ( fd ); return NULL; }

		char *ret = g_strdup ( inet_ntoa ( ( (struct sockaddr_in *)&ifr.ifr_addr )->sin_addr ) );

		close ( fd );

		return ret;
	}

	return NULL;
}

static void dvbnet_set_mac ( const char *net_name, const char *mac )
{
	struct ifreq ifr;

	int fd = socket ( AF_INET, SOCK_DGRAM, 0 );

	if ( fd < 0 ) { perror ( "socket" ); return; }

	sscanf ( mac, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
		&ifr.ifr_hwaddr.sa_data[0],
		&ifr.ifr_hwaddr.sa_data[1],
		&ifr.ifr_hwaddr.sa_data[2],
		&ifr.ifr_hwaddr.sa_data[3],
		&ifr.ifr_hwaddr.sa_data[4],
		&ifr.ifr_hwaddr.sa_data[5] );

	strcpy ( ifr.ifr_name, net_name );

	ifr.ifr_hwaddr.sa_family = ARPHRD_ETHER;

	if ( ioctl ( fd, SIOCSIFHWADDR, &ifr ) < 0 ) perror ( "SIOCSIFHWADDR" );

	close ( fd );
}

static void dvbnet_set_ip ( const char *net_name, const char *host )
{
	struct ifreq ifr;
	struct sockaddr_in inet_addr;

	int fd = socket ( AF_INET, SOCK_DGRAM, 0 );

	if ( fd < 0 ) { perror ( "socket" ); return; }

	bzero  ( ifr.ifr_name, IFNAMSIZ );
	strcpy ( ifr.ifr_name, net_name );

	inet_addr.sin_family = AF_INET;
	inet_pton ( AF_INET, host, &(inet_addr.sin_addr) );

	memcpy ( &(ifr.ifr_addr), &inet_addr, sizeof (struct sockaddr) );

	if ( ioctl ( fd, SIOCSIFADDR, &ifr ) < 0 ) perror ( "SIOCSIFADDR" );

	close ( fd );
}

static void dvbnet_treeview_append ( const char *name, uint8_t if_num, uint16_t pid, uint8_t encaps, const char *ip_str, const char *str_mac, Dvbnet *dvbnet )
{
	GtkTreeIter iter;
	GtkTreeModel *model = gtk_tree_view_get_model ( dvbnet->treeview );

	int ind = gtk_tree_model_iter_n_children ( model, NULL );
	if ( ind >= UINT8_MAX ) return;

	char buf[20] = {};
	sprintf ( buf, "0x%.4X", pid );

	gtk_list_store_append ( GTK_LIST_STORE ( model ), &iter );
	gtk_list_store_set    ( GTK_LIST_STORE ( model ), &iter,
				COL_NUM, if_num,
				COL_NAME, name,
				COL_PID,  buf,
				COL_ECPS, ( encaps ) ? "Ule" : "Mpe",
				COL_STR_IP, ip_str,
				COL_STR_MAC, str_mac,
				-1 );
}

static void dvbnet_set_if_info ( Dvbnet *dvbnet )
{
	int net_fd = dvbnet_open ( dvbnet );

	if ( net_fd == -1 ) return;

	char net_name[20] = {};

	gtk_list_store_clear ( GTK_LIST_STORE ( gtk_tree_view_get_model ( dvbnet->treeview ) ) );

	uint8_t ifs = 0; for ( ifs = 0; ifs < UINT8_MAX - 1; ifs++ )
	{
		uint16_t pid = 0;
		uint8_t encaps = 0;

		int ret = dvbnet_get_if_info ( net_fd, ifs, &pid, &encaps );

		if ( ret == -1 ) continue;

		sprintf ( net_name, "dvb%u_%u", dvbnet->dvb_adapter, ifs );

		char *str_ip  = dvbnet_get_mac_ip ( net_name, 0 );
		char *str_mac = dvbnet_get_mac_ip ( net_name, 1 );

		dvbnet_treeview_append ( net_name, ifs, pid, encaps, ( str_ip ) ? str_ip : "None", ( str_mac ) ? str_mac : "None", dvbnet );

		free ( str_ip  );
		free ( str_mac );
	}

	close ( net_fd );
}

static void dvbnet_del_if ( int net_fd, Dvbnet *dvbnet )
{
	char net_name[20] = {};
	sprintf ( net_name, "dvb%u_%u", dvbnet->dvb_adapter, dvbnet->if_num );

	struct ifreq ifr;

	int fd = socket ( AF_INET, SOCK_DGRAM, 0 );

	if ( fd < 0 ) { perror ( "socket" ); return; }

	strcpy ( ifr.ifr_name, net_name );

	ioctl ( fd, SIOCGIFFLAGS, &ifr );

	ifr.ifr_flags &= ~ IFF_UP;
	ioctl ( fd, SIOCSIFFLAGS, &ifr );

	close ( fd );

	sleep  ( 1 );

	int ret = ioctl ( net_fd, NET_REMOVE_IF, dvbnet->if_num );

	if ( ret == -1 )
	{
		perror ( "NET_REMOVE_IF" );
		dvbnet_message_dialog ( "NET_REMOVE_IF", g_strerror ( errno ), GTK_MESSAGE_ERROR, dvbnet->window );
	}
}

static int dvbnet_add_if ( int net_fd, Dvbnet *dvbnet )
{
	struct dvb_net_if params;

	memset ( &params, 0, sizeof(params) );
	params.pid = dvbnet->net_pid;
	params.feedtype = ( dvbnet->net_ens ) ? DVB_NET_FEEDTYPE_ULE : DVB_NET_FEEDTYPE_MPE;

	int ret = ioctl ( net_fd, NET_ADD_IF, &params );

	if ( ret == -1 )
	{
		perror ( "NET_ADD_IF" );
		dvbnet_message_dialog ( "NET_ADD_IF", g_strerror ( errno ), GTK_MESSAGE_ERROR, dvbnet->window );
	}

	return ret;
}

static void dvbnet_add ( Dvbnet *dvbnet )
{
	int net_fd = dvbnet_open ( dvbnet );

	if ( net_fd == -1 ) return;

	dvbnet_add_if ( net_fd, dvbnet );

	close ( net_fd );
}

static void dvbnet_click_set_ip ( G_GNUC_UNUSED GtkButton *button, Dvbnet *dvbnet )
{
	char net_name[20] = {};
	sprintf ( net_name, "dvb%u_%u", dvbnet->dvb_adapter, dvbnet->if_num );

	dvbnet_set_ip ( net_name, gtk_entry_get_text ( dvbnet->entry_ip ) );

	dvbnet_set_if_info ( dvbnet );
}

static void dvbnet_click_set_mac ( G_GNUC_UNUSED GtkButton *button, Dvbnet *dvbnet )
{
	char net_name[20] = {};
	sprintf ( net_name, "dvb%u_%u", dvbnet->dvb_adapter, dvbnet->if_num );

	dvbnet_set_mac ( net_name, gtk_entry_get_text ( dvbnet->entry_mac ) );

	dvbnet_set_if_info ( dvbnet );
}

static void dvbnet_click_del_if ( G_GNUC_UNUSED GtkButton *button, Dvbnet *dvbnet )
{
	int net_fd = dvbnet_open ( dvbnet );

	if ( net_fd == -1 ) return;

	dvbnet_del_if ( net_fd, dvbnet );

	close ( net_fd );

	dvbnet_set_if_info ( dvbnet );
}

static void dvbnet_changed_if_num ( GtkSpinButton *button, Dvbnet *dvbnet )
{
	dvbnet->if_num = (uint8_t)gtk_spin_button_get_value_as_int ( button );
}

static void dvbnet_act_if_num ( enum mode act, Dvbnet *dvbnet )
{
	GtkWindow *window = (GtkWindow *)gtk_window_new ( GTK_WINDOW_TOPLEVEL );
	gtk_window_set_title ( window, "DvbNet interface" );
	gtk_window_set_modal ( window, TRUE );
	gtk_window_set_default_size ( window, 300, 100 );
	gtk_window_set_icon_name ( window, "applications-internet" );

	GtkBox *m_box = (GtkBox *)gtk_box_new ( GTK_ORIENTATION_VERTICAL, 0 );
	gtk_box_set_spacing ( m_box, 5 );

	GtkBox *h_box = (GtkBox *)gtk_box_new ( GTK_ORIENTATION_HORIZONTAL, 0 );
	gtk_box_set_spacing ( h_box, 5 );

	GtkLabel *label = (GtkLabel *)gtk_label_new ( "IF-Num " );
	gtk_widget_set_halign ( GTK_WIDGET ( label ), GTK_ALIGN_START );

	GtkSpinButton *spinbutton = (GtkSpinButton *)gtk_spin_button_new_with_range ( 0, UINT8_MAX - 1, 1 );
	gtk_spin_button_set_value ( spinbutton, dvbnet->if_num );
	g_signal_connect ( spinbutton, "changed", G_CALLBACK ( dvbnet_changed_if_num ), dvbnet );

	gtk_box_pack_start ( h_box, GTK_WIDGET ( label      ), FALSE, FALSE, 0 );
	gtk_box_pack_start ( h_box, GTK_WIDGET ( spinbutton ), TRUE,  TRUE,  0 );

	gtk_box_pack_start ( m_box, GTK_WIDGET ( h_box ), FALSE, FALSE, 0 );

	h_box = (GtkBox *)gtk_box_new ( GTK_ORIENTATION_HORIZONTAL, 0 );
	gtk_box_set_spacing ( h_box, 5 );

	GtkButton *button = (GtkButton *)gtk_button_new_with_label ( "⏻" );
	g_signal_connect_swapped ( button, "clicked", G_CALLBACK ( gtk_widget_destroy ), window );
	gtk_box_pack_start ( h_box, GTK_WIDGET ( button ), TRUE, TRUE, 0 );

	if ( act == DEL_IF )
	{
		button = (GtkButton *)gtk_button_new_with_label ( "➖" );
		g_signal_connect ( button, "clicked", G_CALLBACK ( dvbnet_click_del_if ), dvbnet );
	}

	if ( act == SET_IP )
	{
		button = (GtkButton *)gtk_button_new_with_label ( "Set IP" );
		g_signal_connect ( button, "clicked", G_CALLBACK ( dvbnet_click_set_ip ), dvbnet );
	}

	if ( act == SET_MAC )
	{
		button = (GtkButton *)gtk_button_new_with_label ( "Set Mac" );
		g_signal_connect ( button, "clicked", G_CALLBACK ( dvbnet_click_set_mac ), dvbnet );
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
	dvbnet_act_if_num ( SET_IP, dvbnet );
}

static void dvbnet_clicked_button_net_mac ( G_GNUC_UNUSED GtkButton *button, Dvbnet *dvbnet )
{
	dvbnet_act_if_num ( SET_MAC, dvbnet );
}

static void dvbnet_del ( Dvbnet *dvbnet )
{
	dvbnet_act_if_num ( DEL_IF, dvbnet );
}

static void dvbnet_clicked_button_net_add ( G_GNUC_UNUSED GtkButton *button, Dvbnet *dvbnet )
{
	dvbnet_add ( dvbnet );
	dvbnet_set_if_info ( dvbnet );
}

static void dvbnet_clicked_button_net_rld ( G_GNUC_UNUSED GtkButton *button, Dvbnet *dvbnet )
{
	dvbnet_set_if_info ( dvbnet );
}

static void dvbnet_clicked_button_net_del ( G_GNUC_UNUSED GtkButton *button, Dvbnet *dvbnet )
{
	dvbnet_del ( dvbnet );
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

static int dvbnet_spinbutton_hex_output ( GtkSpinButton *spinbutton, Dvbnet *dvbnet )
{
	GtkAdjustment *adjustment = gtk_spin_button_get_adjustment (spinbutton);

	int val = (int) gtk_adjustment_get_value (adjustment);

	dvbnet->net_pid = (uint16_t)val;

	char buf[20] = {};
	sprintf ( buf, "0x%.4X", val );

	gtk_entry_set_text (GTK_ENTRY (spinbutton), buf);

	return TRUE;
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

	GtkAdjustment *adjustment = gtk_adjustment_new ( dvbnet->net_pid, 0, UINT16_MAX, 1, 16, 0 );
	spinbutton = (GtkSpinButton *)gtk_spin_button_new ( adjustment, 1, 0 );
	g_signal_connect ( spinbutton, "output", G_CALLBACK ( dvbnet_spinbutton_hex_output ), dvbnet );

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
	gtk_widget_set_margin_start  ( GTK_WIDGET ( v_box ), 10 );
	gtk_widget_set_margin_end    ( GTK_WIDGET ( v_box ), 10 );

	GtkScrolledWindow *scroll = (GtkScrolledWindow *)gtk_scrolled_window_new ( NULL, NULL );
	gtk_scrolled_window_set_policy ( scroll, GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC );

	GtkListStore *store = gtk_list_store_new ( NUM_COLS, G_TYPE_UINT, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING );

	dvbnet->treeview = (GtkTreeView *)gtk_tree_view_new_with_model ( GTK_TREE_MODEL ( store ) );

	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;

	struct Column { const char *name; const char *type; uint8_t num; } column_n[] =
	{
		{ "IF-Num",        "text", COL_NUM  },
		{ "Net-Name",      "text", COL_NAME },
		{ "Pid",           "text", COL_PID  },
		{ "Encapsulation", "text", COL_ECPS },
		{ "Ip",            "text", COL_STR_IP  },
		{ "Mac",           "text", COL_STR_MAC }
	};

	uint8_t c = 0; for ( c = 0; c < G_N_ELEMENTS ( column_n ); c++ )
	{
		renderer = gtk_cell_renderer_text_new ();

		column = gtk_tree_view_column_new_with_attributes ( column_n[c].name, renderer, column_n[c].type, column_n[c].num, NULL );
		gtk_tree_view_append_column ( dvbnet->treeview, column );
	}

	gtk_container_add ( GTK_CONTAINER ( scroll ), GTK_WIDGET ( dvbnet->treeview ) );
	g_object_unref ( G_OBJECT (store) );

	gtk_box_pack_start ( v_box, GTK_WIDGET ( scroll ), TRUE, TRUE, 0 );

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
	gtk_box_pack_start ( main_vbox, GTK_WIDGET ( net_box_s ), TRUE, TRUE, 0 );

	GtkBox *net_box_c = dvbnet_create_net_box_control ( dvbnet );
	gtk_box_pack_end ( main_vbox, GTK_WIDGET ( net_box_c ), FALSE, FALSE, 0 );

	gtk_container_set_border_width ( GTK_CONTAINER ( main_vbox ), 10 );
	gtk_container_add   ( GTK_CONTAINER ( dvbnet->window ), GTK_WIDGET ( main_vbox ) );

	gtk_widget_show_all ( GTK_WIDGET ( dvbnet->window ) );

	dvbnet_set_if_info ( dvbnet );
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
