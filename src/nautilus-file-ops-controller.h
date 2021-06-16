#pragma once

#include <gtk/gtk.h>
#include <gio/gio.h>

#include "nautilus-file-operations-dbus-data.h"
#include "nautilus-file-op-callback-types.h"

typedef struct _NautilusFileOpHelper NautilusFileOpHelper;

void nautilus_file_ops_controller_copy_move            (const GList                    *item_uris,
                                                        const char                     *target_dir_uri,
                                                        GdkDragAction                   copy_action,
                                                        GtkWidget                      *parent_view,
                                                        NautilusFileOperationsDBusData *dbus_data,
                                                        NautilusCopyCallback            done_callback,
                                                        gpointer                        done_callback_data,
                                                        gboolean                        as_admin_if_needed);
void nautilus_file_ops_controller_delete_async         (GList                          *files,
                                                        GtkWindow                      *parent_window,
                                                        NautilusDeleteCallback          done_callback,
                                                        gpointer                        done_callback_data,
                                                        gboolean                        as_admin_if_needed);