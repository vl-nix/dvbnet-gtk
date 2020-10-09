/*
* Copyright 2020 Stepan Perun
* This program is free software.
*
* License: Gnu General Public License GPL-3
* file:///usr/share/common-licenses/GPL-3
* http://www.gnu.org/licenses/gpl-3.0.html
*/

#pragma once

#include <gtk/gtk.h>

#define DVBNET_TYPE_APPLICATION dvbnet_get_type()

G_DECLARE_FINAL_TYPE ( Dvbnet, dvbnet, DVBNET, APPLICATION, GtkApplication )
