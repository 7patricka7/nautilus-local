#include <config.h>
#include <glib/gi18n.h>
#include "nautilus-file-ops-controller.h"
#include "nautilus-file-operations.h"
#include "nautilus-ui-utilities.h"
#include "nautilus-file-utilities.h"
#include "nautilus-file.h"

static GList *
location_list_from_uri_list (const GList *uris)
{
    const GList *l;
    GList *files;
    GFile *f;

    files = NULL;
    for (l = uris; l != NULL; l = l->next)
    {
        f = g_file_new_for_uri (l->data);
        files = g_list_prepend (files, f);
    }

    return g_list_reverse (files);
}

typedef struct
{
    NautilusCopyCallback real_callback;
    gpointer real_data;
} MoveTrashCBData;

static void
callback_for_move_to_trash (gboolean         user_cancelled,
                            MoveTrashCBData *data)
{
    if (data->real_callback)
    {
        g_autoptr (GHashTable) debuting_uris = NULL;

        debuting_uris = g_hash_table_new_full (g_file_hash, (GEqualFunc) g_file_equal, g_object_unref, NULL);

        data->real_callback (debuting_uris, !user_cancelled, data->real_data);
    }
    g_slice_free (MoveTrashCBData, data);
}

void
nautilus_file_ops_controller_copy_move (const GList                    *item_uris,
                                        const char                     *target_dir,
                                        GdkDragAction                   copy_action,
                                        GtkWidget                      *parent_view,
                                        NautilusFileOperationsDBusData *dbus_data,
                                        NautilusCopyCallback            done_callback,
                                        gpointer                        done_callback_data)
{
    GList *locations;
    GList *p;
    GFile *dest;
    GtkWindow *parent_window;
    gboolean target_is_mapping;
    gboolean have_nonmapping_source;

    dest = NULL;
    target_is_mapping = FALSE;
    have_nonmapping_source = FALSE;

    if (target_dir)
    {
        dest = g_file_new_for_uri (target_dir);
        if (g_file_has_uri_scheme (dest, "burn"))
        {
            target_is_mapping = TRUE;
        }
    }

    locations = location_list_from_uri_list (item_uris);

    for (p = locations; p != NULL; p = p->next)
    {
        if (!g_file_has_uri_scheme ((GFile * ) p->data, "burn"))
        {
            have_nonmapping_source = TRUE;
        }
    }

    if (target_is_mapping && have_nonmapping_source && copy_action == GDK_ACTION_MOVE)
    {
        /* never move to "burn:///", but fall back to copy.
         * This is a workaround, because otherwise the source files would be removed.
         */
        copy_action = GDK_ACTION_COPY;
    }

    parent_window = NULL;
    if (parent_view)
    {
        parent_window = (GtkWindow *) gtk_widget_get_ancestor (parent_view, GTK_TYPE_WINDOW);
    }

    if (copy_action == GDK_ACTION_COPY)
    {
        nautilus_file_operations_copy_async (locations,
                                             dest,
                                             parent_window,
                                             dbus_data,
                                             done_callback, done_callback_data);
    }
    else if (copy_action == GDK_ACTION_MOVE)
    {
        if (g_file_has_uri_scheme (dest, "trash"))
        {
            MoveTrashCBData *cb_data;

            cb_data = g_slice_new0 (MoveTrashCBData);
            cb_data->real_callback = done_callback;
            cb_data->real_data = done_callback_data;

            nautilus_file_operations_trash_or_delete_async (locations,
                                                            parent_window,
                                                            dbus_data,
                                                            (NautilusDeleteCallback) callback_for_move_to_trash,
                                                            cb_data);
        }
        else
        {
            nautilus_file_operations_move_async (locations,
                                                 dest,
                                                 parent_window,
                                                 dbus_data,
                                                 done_callback, done_callback_data);
        }
    }
    else
    {
        nautilus_file_operations_link (locations,
                                       dest,
                                       parent_window,
                                       dbus_data,
                                       done_callback, done_callback_data);
    }

    g_list_free_full (locations, g_object_unref);
    if (dest)
    {
        g_object_unref (dest);
    }
}
