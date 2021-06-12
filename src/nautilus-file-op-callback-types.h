#pragma once

#include <gio/gio.h>

typedef void (* NautilusCopyCallback)              (GHashTable *debuting_uris,
                                                    gboolean    success,
                                                    gpointer    callback_data);
typedef void (* NautilusCreateCallback)            (GFile      *new_file,
                                                    gboolean    success,
                                                    gpointer    callback_data);
typedef void (* NautilusOpCallback)                (gboolean    success,
                                                    gpointer    callback_data);
typedef void (* NautilusDeleteCallback)            (gboolean    user_cancel,
                                                    gpointer    callback_data);
typedef void (* NautilusMountCallback)             (GVolume    *volume,
                                                    gboolean    success,
                                                    GObject    *callback_data_object);
typedef void (* NautilusUnmountCallback)           (gpointer    callback_data);
typedef void (* NautilusExtractCallback)           (GList      *outputs,
                                                    gpointer    callback_data);
