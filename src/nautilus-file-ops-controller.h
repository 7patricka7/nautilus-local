#pragma once

#include <gtk/gtk.h>
#include <gio/gio.h>

#include "nautilus-file-operations-dbus-data.h"
#include "nautilus-file-op-callback-types.h"

void nautilus_file_ops_controller_copy_move            (const GList                    *item_uris,
                                                        const char                     *target_dir_uri,
                                                        GdkDragAction                   copy_action,
                                                        GtkWidget                      *parent_view,
                                                        NautilusFileOperationsDBusData *dbus_data,
                                                        NautilusCopyCallback            done_callback,
                                                        gpointer                        done_callback_data);
