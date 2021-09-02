#include <config.h>
#include <glib/gi18n.h>
#include "nautilus-file-ops-controller.h"
#include "nautilus-file-op-helper.h"
#include "nautilus-file-operations.h"
#include "nautilus-ui-utilities.h"
#include "nautilus-file-utilities.h"
#include "nautilus-file.h"

typedef enum
{
    NAUTILUS_ADMIN_FILE_OP_INVALID,
    NAUTILUS_ADMIN_FILE_OP_COPY,
    NAUTILUS_ADMIN_FILE_OP_MOVE,
    NAUTILUS_ADMIN_FILE_OP_DELETE,
} NautilusAdminFileOp;

typedef enum
{
    NAUTILUS_ADMIN_FILE_OP_PERMISSION_DLG_INVALID,
    NAUTILUS_ADMIN_FILE_OP_PERMISSION_DLG_COPY_TO_DIR,
    NAUTILUS_ADMIN_FILE_OP_PERMISSION_DLG_MOVE_TO_DIR,
    NAUTILUS_ADMIN_FILE_OP_PERMISSION_DLG_MOVE_FROM_DIR,
    NAUTILUS_ADMIN_FILE_OP_PERMISSION_DLG_COPY_FILE,
    NAUTILUS_ADMIN_FILE_OP_PERMISSION_DLG_MOVE_FILE,
    NAUTILUS_ADMIN_FILE_OP_PERMISSION_DLG_DELETE_FILE,
} NautilusAdminFileOpPermissionDlgType;

typedef enum
{
    NAUTILUS_ADMIN_FILE_OP_PERMISSION_DLG_RESPONSE_INVALID,
    NAUTILUS_ADMIN_FILE_OP_PERMISSION_DLG_RESPONSE_YES,
    NAUTILUS_ADMIN_FILE_OP_PERMISSION_DLG_RESPONSE_SKIP,
    NAUTILUS_ADMIN_FILE_OP_PERMISSION_DLG_RESPONSE_SKIP_ALL,
    NAUTILUS_ADMIN_FILE_OP_PERMISSION_DLG_RESPONSE_CANCEL,
} NautilusAdminFileOpPermissionDlgResponse;

#define NAUTILUS_TYPE_ADMIN_FILE_OP_HELPER nautilus_admin_file_op_helper_get_type ()

G_DECLARE_FINAL_TYPE (NautilusAdminFileOpHelper, nautilus_admin_file_op_helper,
                      NAUTILUS, ADMIN_FILE_OP_HELPER,
                      NautilusFileOpHelper)

struct _NautilusAdminFileOpHelper
{
    NautilusFileOpHelper parent_instance;

    NautilusAdminFileOp file_op;
    GtkWindow *parent_window;
    GFile *admin_dest_dir;
    GHashTable *map_original_to_admin_directory_for_enumerate;
    GHashTable *original_dest_files_opaquely_handled_as_admin;
    gboolean admin_permission_granted;
    gboolean admin_permission_denied;
    NautilusAdminFileOpHelper *primary_helper;
};

G_DEFINE_TYPE (NautilusAdminFileOpHelper, nautilus_admin_file_op_helper, NAUTILUS_TYPE_FILE_OP_HELPER)

typedef void (*FileOpAutoAdminMountFinishedCallback) (gboolean success,
                                                      gpointer callback_data,
                                                      GError  *error);

typedef struct
{
    FileOpAutoAdminMountFinishedCallback mount_finished_cb;
    gpointer mount_finished_cb_data;
} AdminMountCbData;

typedef struct
{
    gboolean completed;
    gboolean success;
    GMutex mutex;
    GCond cond;
} FileSubopAutoAdminMountData;

typedef struct
{
    NautilusAdminFileOpPermissionDlgType dlg_type;
    GtkWindow *parent_window;
    GFile *file;
    gboolean completed;
    int response;
    GMutex mutex;
    GCond cond;
} AdminOpPermissionDialogData;

typedef struct
{
    NautilusCopyCallback real_callback;
    gpointer real_data;
} MoveTrashCBData;

static gboolean admin_vfs_mounted = FALSE;

static GFile *
get_as_admin_file (GFile *file)
{
    g_autofree gchar *uri_path = NULL;
    g_autofree gchar *uri = NULL;
    g_autofree char *admin_uri = NULL;
    gboolean uri_op_success;

    if (file == NULL)
    {
        return NULL;
    }

    uri = g_file_get_uri (file);
    uri_op_success = g_uri_split (uri, G_URI_FLAGS_NONE,
                                  NULL, NULL, NULL, NULL,
                                  &uri_path,
                                  NULL, NULL, NULL);

    g_assert (uri_op_success);

    admin_uri = g_strconcat ("admin://", uri_path, NULL);

    return g_file_new_for_uri (admin_uri);
}

static void
file_op_async_auto_admin_vfs_mount_cb (GObject      *source_object,
                                       GAsyncResult *res,
                                       gpointer      user_data)
{
    /*This is only called in thread-default context of main (aka UI) thread*/

    g_autoptr (GError) error = NULL;
    AdminMountCbData *cb_data = user_data;
    gboolean mount_success;

    mount_success = g_file_mount_enclosing_volume_finish (G_FILE (source_object), res, &error);

    if (mount_success || g_error_matches (error, G_IO_ERROR, G_IO_ERROR_ALREADY_MOUNTED))
    {
        admin_vfs_mounted = TRUE;
        mount_success = TRUE;
    }

    cb_data->mount_finished_cb (mount_success, cb_data->mount_finished_cb_data, error);

    g_slice_free (AdminMountCbData, cb_data);
}

static void
mount_admin_vfs_async (FileOpAutoAdminMountFinishedCallback mount_finished_cb,
                       gpointer                             mount_finished_cb_data)
{
    g_autoptr (GFile) admin_root = NULL;
    AdminMountCbData *cb_data;

    cb_data = g_slice_new0 (AdminMountCbData);
    cb_data->mount_finished_cb = mount_finished_cb;
    cb_data->mount_finished_cb_data = mount_finished_cb_data;

    admin_root = g_file_new_for_uri ("admin:///");

    g_file_mount_enclosing_volume (admin_root,
                                   G_MOUNT_MOUNT_NONE,
                                   NULL,
                                   NULL,
                                   file_op_async_auto_admin_vfs_mount_cb,
                                   cb_data);
}

static void
mount_admin_vfs_sync_finished (gboolean  success,
                               gpointer  callback_data,
                               GError   *error)
{
    FileSubopAutoAdminMountData *data = callback_data;

    g_mutex_lock (&data->mutex);

    data->success = success;
    data->completed = TRUE;

    g_cond_signal (&data->cond);
    g_mutex_unlock (&data->mutex);
}

static gboolean
mount_admin_vfs_sync (void)
{
    FileSubopAutoAdminMountData data = { 0 };

    data.completed = FALSE;
    data.success = FALSE;

    g_mutex_init (&data.mutex);
    g_cond_init (&data.cond);

    g_mutex_lock (&data.mutex);

    mount_admin_vfs_async (mount_admin_vfs_sync_finished,
                           &data);

    while (!data.completed)
    {
        g_cond_wait (&data.cond, &data.mutex);
    }

    g_mutex_unlock (&data.mutex);
    g_mutex_clear (&data.mutex);
    g_cond_clear (&data.cond);

    return data.success;
}

static int
do_admin_file_op_permission_dialog (GtkWindow                            *parent_window,
                                    NautilusAdminFileOpPermissionDlgType  dlg_type,
                                    GFile                                *file)
{
    GtkWidget *dialog;
    int response;
    GtkWidget *button;

    if (dlg_type == NAUTILUS_ADMIN_FILE_OP_PERMISSION_DLG_COPY_TO_DIR ||
        dlg_type == NAUTILUS_ADMIN_FILE_OP_PERMISSION_DLG_MOVE_TO_DIR ||
        dlg_type == NAUTILUS_ADMIN_FILE_OP_PERMISSION_DLG_MOVE_FROM_DIR)
    {
        dialog = gtk_message_dialog_new (parent_window,
                                         0,
                                         GTK_MESSAGE_OTHER,
                                         GTK_BUTTONS_NONE,
                                         NULL);

        g_object_set (dialog,
                      "text", _("Destination directory access denied"),
                      "secondary-text", _("You'll need to provide administrator permissions to paste files into this directory."),
                      NULL);

        gtk_dialog_add_button (GTK_DIALOG (dialog), _("_Cancel"),
                               NAUTILUS_ADMIN_FILE_OP_PERMISSION_DLG_RESPONSE_CANCEL);
        button = gtk_dialog_add_button (GTK_DIALOG (dialog), _("_Continue"),
                                        NAUTILUS_ADMIN_FILE_OP_PERMISSION_DLG_RESPONSE_YES);
        gtk_style_context_add_class (gtk_widget_get_style_context (button),
                                     "suggested-action");

        gtk_dialog_set_default_response (GTK_DIALOG (dialog), NAUTILUS_ADMIN_FILE_OP_PERMISSION_DLG_RESPONSE_CANCEL);
    }
    else
    {
        g_autofree gchar *secondary_text = NULL;
        g_autofree gchar *basename = NULL;

        basename = nautilus_get_display_basename (file);

        switch (dlg_type)
        {
            case NAUTILUS_ADMIN_FILE_OP_PERMISSION_DLG_COPY_FILE:
            {
                secondary_text = g_strdup_printf (_("You'll need to provide administrator permissions to copy“%s”."), basename);
            }
            break;

            case NAUTILUS_ADMIN_FILE_OP_PERMISSION_DLG_MOVE_FILE:
            {
                secondary_text = g_strdup_printf (_("You'll need to provide administrator permissions to move “%s”."), basename);
            }
            break;

            case NAUTILUS_ADMIN_FILE_OP_PERMISSION_DLG_DELETE_FILE:
            {
                secondary_text = g_strdup_printf (_("You'll need to provide administrator permissions to permanently delete “%s”."), basename);
            }
            break;

            default:
            {
                g_return_val_if_reached (NAUTILUS_ADMIN_FILE_OP_PERMISSION_DLG_RESPONSE_CANCEL);
            }
        }

        dialog = gtk_message_dialog_new (parent_window,
                                         0,
                                         GTK_MESSAGE_QUESTION,
                                         GTK_BUTTONS_NONE,
                                         NULL);

        g_object_set (dialog,
                      "text", _("Insufficient Access"),
                      "secondary-text", secondary_text,
                      NULL);

        /*TODO: Confirm correct pnemonic keys */

        gtk_dialog_add_button (GTK_DIALOG (dialog), _("_Cancel"), NAUTILUS_ADMIN_FILE_OP_PERMISSION_DLG_RESPONSE_CANCEL);
        gtk_dialog_add_button (GTK_DIALOG (dialog), _("Skip _All"), NAUTILUS_ADMIN_FILE_OP_PERMISSION_DLG_RESPONSE_SKIP_ALL);
        gtk_dialog_add_button (GTK_DIALOG (dialog), _("_Skip"), NAUTILUS_ADMIN_FILE_OP_PERMISSION_DLG_RESPONSE_SKIP);
        button = gtk_dialog_add_button (GTK_DIALOG (dialog), _("_Continue"), NAUTILUS_ADMIN_FILE_OP_PERMISSION_DLG_RESPONSE_YES);
        gtk_style_context_add_class (gtk_widget_get_style_context (button),
                                     "suggested-action");

        gtk_dialog_set_default_response (GTK_DIALOG (dialog), NAUTILUS_ADMIN_FILE_OP_PERMISSION_DLG_RESPONSE_CANCEL);
    }

    response = gtk_dialog_run (GTK_DIALOG (dialog));

    gtk_widget_destroy (dialog);

    return response;
}

static gboolean
do_admin_file_op_permission_dialog2 (gpointer callback_data)
{
    AdminOpPermissionDialogData *data = callback_data;
    int response;

    response = do_admin_file_op_permission_dialog (data->parent_window, data->dlg_type, data->file);

    g_mutex_lock (&data->mutex);

    data->completed = TRUE;
    data->response = response;

    g_cond_signal (&data->cond);
    g_mutex_unlock (&data->mutex);

    return FALSE;
}

static int
ask_permission_for_admin_file_subop_in_main_thread (GtkWindow                            *parent_window,
                                                    NautilusAdminFileOpPermissionDlgType  prompt_type,
                                                    GFile                                *file)
{
    AdminOpPermissionDialogData data = { 0 };

    data.dlg_type = prompt_type;
    data.parent_window = parent_window;
    data.file = file;
    data.completed = FALSE;

    g_mutex_init (&data.mutex);
    g_cond_init (&data.cond);

    g_mutex_lock (&data.mutex);

    g_main_context_invoke (NULL,
                           do_admin_file_op_permission_dialog2,
                           &data);

    while (!data.completed)
    {
        g_cond_wait (&data.cond, &data.mutex);
    }

    g_mutex_unlock (&data.mutex);
    g_mutex_clear (&data.mutex);
    g_cond_clear (&data.cond);

    return data.response;
}

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

inline static GFile *
original_to_admin_file_for_enumerate_hash_table_lookup (NautilusAdminFileOpHelper *self,
                                                        GFile                     *original_file)
{
    GFile *admin_file = NULL;

    if (self->map_original_to_admin_directory_for_enumerate != NULL)
    {
        admin_file = g_hash_table_lookup (self->map_original_to_admin_directory_for_enumerate, original_file);
    }

    return admin_file;
}

inline static gboolean
original_dest_files_opaquely_handled_as_admin_hash_table_contains (NautilusAdminFileOpHelper *self,
                                                                   GFile                     *original_file)
{
    gboolean ret = FALSE;

    if (self->original_dest_files_opaquely_handled_as_admin != NULL)
    {
        ret = g_hash_table_contains (self->original_dest_files_opaquely_handled_as_admin, original_file);
    }

    return ret;
}

inline static void
original_dest_files_opaquely_handled_as_admin_hash_table_add (NautilusAdminFileOpHelper *self,
                                                              GFile                     *file)
{
    if (self->original_dest_files_opaquely_handled_as_admin == NULL)
    {
        self->original_dest_files_opaquely_handled_as_admin = g_hash_table_new (g_file_hash, (GEqualFunc) g_file_equal);
    }

    g_hash_table_add (self->original_dest_files_opaquely_handled_as_admin, g_object_ref (file));
}

static GFileEnumerator *
admin_file_op_helper_enumerate_children_on_error (NautilusFileOpHelper   *helper,
                                                  GError                 *original_error,
                                                  GFile                  *file,
                                                  const char             *attributes,
                                                  GFileQueryInfoFlags     flags,
                                                  GCancellable           *cancellable,
                                                  NautilusFileSubopError *subop_error)
{
    NautilusAdminFileOpHelper *self;
    GFileEnumerator *enumerator = NULL;

    if (!g_error_matches (original_error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED))
    {
        return enumerator;
    }

    if (!g_file_is_native (file) ||
        g_file_has_uri_scheme (file, "admin"))
    {
        return enumerator;
    }

    self = NAUTILUS_ADMIN_FILE_OP_HELPER (helper);

    if (!self->admin_permission_granted && !self->admin_permission_denied)
    {
        int dlg_user_response;
        NautilusAdminFileOpPermissionDlgType prompt_type;

        switch (self->file_op)
        {
            case NAUTILUS_ADMIN_FILE_OP_COPY:
            {
                prompt_type = NAUTILUS_ADMIN_FILE_OP_PERMISSION_DLG_COPY_FILE;
            }
            break;

            case NAUTILUS_ADMIN_FILE_OP_MOVE:
            {
                prompt_type = NAUTILUS_ADMIN_FILE_OP_PERMISSION_DLG_MOVE_FILE;
            }
            break;

            case NAUTILUS_ADMIN_FILE_OP_DELETE:
            {
                prompt_type = NAUTILUS_ADMIN_FILE_OP_PERMISSION_DLG_DELETE_FILE;
            }
            break;

            default:
            {
                g_return_val_if_reached (enumerator);
            }
        }

        dlg_user_response = ask_permission_for_admin_file_subop_in_main_thread (self->parent_window,
                                                                                prompt_type,
                                                                                file);

        switch (dlg_user_response)
        {
            case NAUTILUS_ADMIN_FILE_OP_PERMISSION_DLG_RESPONSE_YES:
            {
                self->admin_permission_granted = TRUE;
            }
            break;

            case NAUTILUS_ADMIN_FILE_OP_PERMISSION_DLG_RESPONSE_SKIP:
            {
            }
            break;

            case NAUTILUS_ADMIN_FILE_OP_PERMISSION_DLG_RESPONSE_SKIP_ALL:
            {
                self->admin_permission_denied = TRUE;
            }
            break;

            case NAUTILUS_ADMIN_FILE_OP_PERMISSION_DLG_RESPONSE_CANCEL:
            {
                if (subop_error != NULL)
                {
                    subop_error->user_response_cancel_all = TRUE;
                }
            }
            break;

            default:
            {
                g_assert_not_reached ();
            }
        }
    }

    if (self->admin_permission_granted)
    {
        g_autoptr (GFile) admin_file = NULL;

        if (!admin_vfs_mounted)
        {
            if (!mount_admin_vfs_sync ())
            {
                return enumerator;
            }
        }

        admin_file = get_as_admin_file (file);

        enumerator = g_file_enumerate_children (admin_file,
                                                attributes,
                                                flags,
                                                cancellable,
                                                (subop_error != NULL) ? &subop_error->gerror : NULL);

        if (enumerator != NULL)
        {
            if (self->map_original_to_admin_directory_for_enumerate == NULL)
            {
                self->map_original_to_admin_directory_for_enumerate = g_hash_table_new_full (g_file_hash, (GEqualFunc) g_file_equal, g_object_unref, g_object_unref);
            }

            g_hash_table_insert (self->map_original_to_admin_directory_for_enumerate,
                                 g_object_ref (file),
                                 g_steal_pointer (&admin_file));
        }
    }

    return enumerator;
}

static GFile *
admin_file_op_helper_get_child (NautilusFileOpHelper *helper,
                                GFile                *dir,
                                const char           *name)
{
    NautilusAdminFileOpHelper *self;
    GFile *admin_dir;

    self = NAUTILUS_ADMIN_FILE_OP_HELPER (helper);

    admin_dir = original_to_admin_file_for_enumerate_hash_table_lookup (self, dir);

    if (admin_dir != NULL)
    {
        dir = admin_dir;
    }

    return g_file_get_child (dir, name);
}

static gboolean
admin_file_op_helper_retry_copy_move_file_on_permission_denied (NautilusAdminFileOpHelper *self,
                                                                gboolean                   is_move,
                                                                GFile                     *source,
                                                                GFile                     *destination,
                                                                GFileCopyFlags             flags,
                                                                GCancellable              *cancellable,
                                                                GFileProgressCallback      progress_callback,
                                                                gpointer                   progress_callback_data,
                                                                NautilusFileSubopError    *subop_error)
{
    gboolean succeeded;
    g_autoptr (GFile) admin_source = NULL;
    g_autoptr (GFile) admin_destination = NULL;
    GFile *original_destination;

    if (!g_file_is_native (source) ||
        g_file_has_uri_scheme (source, "admin"))
    {
        return FALSE;
    }

    if (!self->admin_permission_granted && !self->admin_permission_denied)
    {
        int dlg_user_response;
        NautilusAdminFileOpPermissionDlgType prompt_type;

        if (is_move)
        {
            prompt_type = NAUTILUS_ADMIN_FILE_OP_PERMISSION_DLG_MOVE_FILE;
        }
        else
        {
            prompt_type = NAUTILUS_ADMIN_FILE_OP_PERMISSION_DLG_COPY_FILE;
        }

        dlg_user_response = ask_permission_for_admin_file_subop_in_main_thread (self->parent_window,
                                                                                prompt_type,
                                                                                source);

        switch (dlg_user_response)
        {
            case NAUTILUS_ADMIN_FILE_OP_PERMISSION_DLG_RESPONSE_YES:
            {
                self->admin_permission_granted = TRUE;
            }
            break;

            case NAUTILUS_ADMIN_FILE_OP_PERMISSION_DLG_RESPONSE_SKIP:
            {
            }
            break;

            case NAUTILUS_ADMIN_FILE_OP_PERMISSION_DLG_RESPONSE_SKIP_ALL:
            {
                self->admin_permission_denied = TRUE;
            }
            break;

            case NAUTILUS_ADMIN_FILE_OP_PERMISSION_DLG_RESPONSE_CANCEL:
            {
                if (subop_error != NULL)
                {
                    subop_error->user_response_cancel_all = TRUE;
                }
            }
            break;

            default:
            {
                g_assert_not_reached ();
            }
        }
    }

    if (!self->admin_permission_granted)
    {
        return FALSE;
    }

    if (!admin_vfs_mounted && !mount_admin_vfs_sync ())
    {
        return FALSE;
    }

    admin_source = get_as_admin_file (source);
    source = admin_source;
    original_destination = destination;

    if (g_file_is_native (destination))
    {
        admin_destination = get_as_admin_file (destination);
        destination = admin_destination;
    }

    if (is_move)
    {
        succeeded = g_file_move (source,
                                 destination,
                                 flags,
                                 cancellable,
                                 progress_callback,
                                 progress_callback_data,
                                 (subop_error != NULL) ? &subop_error->gerror : NULL);
    }
    else
    {
        succeeded = g_file_copy (source,
                                 destination,
                                 flags,
                                 cancellable,
                                 progress_callback,
                                 progress_callback_data,
                                 (subop_error != NULL) ? &subop_error->gerror : NULL);
    }

    if (succeeded)
    {
        original_dest_files_opaquely_handled_as_admin_hash_table_add (self, g_object_ref (original_destination));
    }

    return succeeded;
}

static gboolean
admin_file_op_helper_copy_move_file (NautilusFileOpHelper   *helper,
                                     gboolean                is_move,
                                     GFile                  *source,
                                     GFile                  *destination,
                                     GFileCopyFlags          flags,
                                     GCancellable           *cancellable,
                                     GFileProgressCallback   progress_callback,
                                     gpointer                progress_callback_data,
                                     NautilusFileSubopError *subop_error)
{
    NautilusAdminFileOpHelper *self;
    g_autoptr (GFile) admin_source = NULL;
    gboolean succeeded;
    g_autoptr (GError) error = NULL;
    g_autoptr (GFile) admin_destination = NULL;
    GFile *original_destination;

    self = NAUTILUS_ADMIN_FILE_OP_HELPER (helper);

    if (self->primary_helper != NULL &&
        original_dest_files_opaquely_handled_as_admin_hash_table_contains (self->primary_helper, source))
    {
        admin_source = get_as_admin_file (source);
    }

    original_destination = destination;

    if (admin_source != NULL)
    {
        source = admin_source;

        if (g_file_is_native (destination))
        {
            admin_destination = get_as_admin_file (destination);
            destination = admin_destination;
        }
    }

    if (is_move)
    {
        succeeded = g_file_move (source,
                                 destination,
                                 flags,
                                 cancellable,
                                 progress_callback,
                                 progress_callback_data,
                                 &error);
    }
    else
    {
        succeeded = g_file_copy (source,
                                 destination,
                                 flags,
                                 cancellable,
                                 progress_callback,
                                 progress_callback_data,
                                 &error);
    }

    if (succeeded &&
        admin_source != NULL)
    {
        original_dest_files_opaquely_handled_as_admin_hash_table_add (self, g_object_ref (original_destination));
    }
    else if (!succeeded &&
             admin_source == NULL &&
             g_error_matches (error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED))
    {
        succeeded = admin_file_op_helper_retry_copy_move_file_on_permission_denied (self,
                                                                                    is_move,
                                                                                    source,
                                                                                    destination,
                                                                                    flags,
                                                                                    cancellable,
                                                                                    progress_callback,
                                                                                    progress_callback_data,
                                                                                    subop_error);
    }

    if (!succeeded &&
        subop_error != NULL &&
        subop_error->gerror == NULL)
    {
        subop_error->gerror = g_steal_pointer (&error);
    }

    return succeeded;
}

static gboolean
admin_file_op_helper_move_file (NautilusFileOpHelper   *helper,
                                GFile                  *source,
                                GFile                  *destination,
                                GFileCopyFlags          flags,
                                GCancellable           *cancellable,
                                GFileProgressCallback   progress_callback,
                                gpointer                progress_callback_data,
                                NautilusFileSubopError *subop_error)
{
    return admin_file_op_helper_copy_move_file (helper,
                                                TRUE,
                                                source,
                                                destination,
                                                flags,
                                                cancellable,
                                                progress_callback,
                                                progress_callback_data,
                                                subop_error);
}

static gboolean
admin_file_op_helper_copy_file (NautilusFileOpHelper   *helper,
                                GFile                  *source,
                                GFile                  *destination,
                                GFileCopyFlags          flags,
                                GCancellable           *cancellable,
                                GFileProgressCallback   progress_callback,
                                gpointer                progress_callback_data,
                                NautilusFileSubopError *subop_error)
{
    return admin_file_op_helper_copy_move_file (helper,
                                                FALSE,
                                                source,
                                                destination,
                                                flags,
                                                cancellable,
                                                progress_callback,
                                                progress_callback_data,
                                                subop_error);
}

static gboolean
admin_file_op_helper_delete_file (NautilusFileOpHelper   *helper,
                                  GFile                  *file,
                                  GCancellable           *cancellable,
                                  NautilusFileSubopError *subop_error)
{
    NautilusAdminFileOpHelper *self;
    gboolean succeeded;
    g_autoptr (GFile) admin_file = NULL;
    g_autoptr (GError) error = NULL;
    GFile *original_file;

    self = NAUTILUS_ADMIN_FILE_OP_HELPER (helper);

    if (self->primary_helper != NULL &&
        original_dest_files_opaquely_handled_as_admin_hash_table_contains (self->primary_helper, file))
    {
        admin_file = get_as_admin_file (file);
    }

    original_file = file;

    if (admin_file != NULL)
    {
        file = admin_file;
    }

    succeeded = g_file_delete (file,
                               cancellable,
                               &error);

    if (!succeeded &&
        admin_file == NULL &&
        g_error_matches (error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED) &&
        g_file_is_native (file) &&
        !g_file_has_uri_scheme (file, "admin"))
    {
        if (!self->admin_permission_granted && !self->admin_permission_denied)
        {
            int dlg_user_response;

            /*TODO: Set correct dialog prompt type based on main file op type */

            dlg_user_response = ask_permission_for_admin_file_subop_in_main_thread (
                self->parent_window,
                NAUTILUS_ADMIN_FILE_OP_PERMISSION_DLG_DELETE_FILE,
                file);

            switch (dlg_user_response)
            {
                case NAUTILUS_ADMIN_FILE_OP_PERMISSION_DLG_RESPONSE_YES:
                {
                    self->admin_permission_granted = TRUE;
                }
                break;

                case NAUTILUS_ADMIN_FILE_OP_PERMISSION_DLG_RESPONSE_SKIP:
                {
                }
                break;

                case NAUTILUS_ADMIN_FILE_OP_PERMISSION_DLG_RESPONSE_SKIP_ALL:
                {
                    self->admin_permission_denied = TRUE;
                }
                break;

                case NAUTILUS_ADMIN_FILE_OP_PERMISSION_DLG_RESPONSE_CANCEL:
                {
                    if (subop_error != NULL)
                    {
                        subop_error->user_response_cancel_all = TRUE;
                    }
                }
                break;

                default:
                {
                    g_assert_not_reached ();
                }
            }
        }

        if (!self->admin_permission_granted)
        {
            return succeeded;
        }

        if (!admin_vfs_mounted && !mount_admin_vfs_sync ())
        {
            return succeeded;
        }

        admin_file = get_as_admin_file (file);

        succeeded = g_file_delete (admin_file,
                                   cancellable,
                                   (subop_error != NULL) ? &subop_error->gerror : NULL);
    }

    if (succeeded &&
        admin_file != NULL)
    {
        original_dest_files_opaquely_handled_as_admin_hash_table_add (self, g_object_ref (original_file));
    }
    else if (!succeeded &&
             subop_error != NULL &&
             subop_error->gerror == NULL)
    {
        subop_error->gerror = g_steal_pointer (&error);
    }

    return succeeded;
}

static gboolean
admin_file_op_helper_make_directory (NautilusFileOpHelper   *helper,
                                     GFile                  *file,
                                     GCancellable           *cancellable,
                                     NautilusFileSubopError *subop_error)
{
    NautilusAdminFileOpHelper *self;
    g_autoptr (GFile) admin_file = NULL;
    gboolean succeeded;
    g_autoptr (GError) error = NULL;
    GFile *original_file;

    self = NAUTILUS_ADMIN_FILE_OP_HELPER (helper);

    if (self->primary_helper != NULL &&
        original_dest_files_opaquely_handled_as_admin_hash_table_contains (self->primary_helper, file))
    {
        admin_file = get_as_admin_file (file);
    }

    original_file = file;

    if (admin_file != NULL)
    {
        file = admin_file;
    }

    succeeded = g_file_make_directory (file,
                                       cancellable,
                                       &error);

    if (!succeeded &&
        g_error_matches (error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED) &&
        admin_file == NULL &&
        g_file_is_native (file) &&
        !g_file_has_uri_scheme (file, "admin"))
    {
        if (!self->admin_permission_granted && !self->admin_permission_denied)
        {
            int dlg_user_response;

            /*TODO: Set correct dialog prompt type based on main file op type */

            dlg_user_response = ask_permission_for_admin_file_subop_in_main_thread (
                self->parent_window,
                NAUTILUS_ADMIN_FILE_OP_PERMISSION_DLG_COPY_FILE,
                file);

            switch (dlg_user_response)
            {
                case NAUTILUS_ADMIN_FILE_OP_PERMISSION_DLG_RESPONSE_YES:
                {
                    self->admin_permission_granted = TRUE;
                }
                break;

                case NAUTILUS_ADMIN_FILE_OP_PERMISSION_DLG_RESPONSE_SKIP:
                {
                }
                break;

                case NAUTILUS_ADMIN_FILE_OP_PERMISSION_DLG_RESPONSE_SKIP_ALL:
                {
                    self->admin_permission_denied = TRUE;
                }
                break;

                case NAUTILUS_ADMIN_FILE_OP_PERMISSION_DLG_RESPONSE_CANCEL:
                {
                    if (subop_error != NULL)
                    {
                        subop_error->user_response_cancel_all = TRUE;
                    }
                }
                break;

                default:
                {
                    g_assert_not_reached ();
                }
            }
        }

        if (!self->admin_permission_granted)
        {
            return succeeded;
        }

        if (!admin_vfs_mounted && !mount_admin_vfs_sync ())
        {
            return succeeded;
        }

        admin_file = get_as_admin_file (file);

        succeeded = g_file_make_directory (admin_file,
                                           cancellable,
                                           (subop_error != NULL) ? &subop_error->gerror : NULL);
    }

    if (succeeded &&
        admin_file != NULL)
    {
        original_dest_files_opaquely_handled_as_admin_hash_table_add (self, g_object_ref (original_file));
    }
    else if (!succeeded &&
             subop_error != NULL &&
             subop_error->gerror == NULL)
    {
        subop_error->gerror = g_steal_pointer (&error);
    }

    return succeeded;
}

static GFile *
admin_op_get_copy_move_target_as_admin_if_needed_else_null (NautilusAdminFileOpHelper *self,
                                                            GFile                     *src,
                                                            GFile                     *target)
{
    GFile *mapped_src;
    GFile *admin_target = NULL;

    mapped_src = original_to_admin_file_for_enumerate_hash_table_lookup (self, src);

    if (mapped_src != NULL &&
        g_file_is_native (target) &&
        !g_file_has_uri_scheme (target, "admin"))
    {
        admin_target = get_as_admin_file (target);
    }

    return admin_target;
}

static GFile *
admin_op_get_copy_move_target (NautilusFileOpHelper *helper,
                               GFile                *src,
                               GFile                *dest_dir,
                               const char           *target_name)
{
    /* TODO: Re-evaluate if this function is really needed */

    NautilusAdminFileOpHelper *self;
    g_autoptr (GFile) target = NULL;
    GFile *admin_target = NULL;

    self = NAUTILUS_ADMIN_FILE_OP_HELPER (helper);

    target = g_file_get_child (dest_dir, target_name);

    admin_target = admin_op_get_copy_move_target_as_admin_if_needed_else_null (self, src, target);

    if (admin_target != NULL)
    {
        return admin_target;
    }

    return g_steal_pointer (&target);
}

static GFile *
admin_op_get_copy_move_target_for_display_name (NautilusFileOpHelper  *helper,
                                                GFile                 *src,
                                                GFile                 *dest_dir,
                                                const char            *target_display_name,
                                                GError               **error)
{
    /* TODO: Re-evaluate if this function is really needed */

    NautilusAdminFileOpHelper *self;
    g_autoptr (GFile) target = NULL;
    GFile *admin_target = NULL;

    self = NAUTILUS_ADMIN_FILE_OP_HELPER (helper);

    target = g_file_get_child_for_display_name (dest_dir, target_display_name, error);

    admin_target = admin_op_get_copy_move_target_as_admin_if_needed_else_null (self, src, target);

    if (admin_target != NULL)
    {
        return admin_target;
    }

    return g_steal_pointer (&target);
}

static void
nautilus_admin_file_op_helper_init (NautilusAdminFileOpHelper *self)
{
    self->file_op = NAUTILUS_ADMIN_FILE_OP_INVALID;
    self->admin_dest_dir = NULL;
    self->map_original_to_admin_directory_for_enumerate = NULL;
    self->original_dest_files_opaquely_handled_as_admin = NULL;
    self->admin_permission_granted = FALSE;
    self->admin_permission_denied = FALSE;
    self->primary_helper = NULL;
}

static NautilusAdminFileOpHelper *
nautilus_admin_file_op_helper_new (NautilusAdminFileOp  file_op,
                                   GtkWindow           *parent_window)
{
    NautilusAdminFileOpHelper *self;

    self = g_object_new (NAUTILUS_TYPE_ADMIN_FILE_OP_HELPER, NULL);
    self->file_op = file_op;
    if (parent_window != NULL)
    {
        self->parent_window = parent_window;
        g_object_add_weak_pointer (G_OBJECT (self->parent_window),
                                   (gpointer *) &self->parent_window);
    }

    return self;
}

static NautilusFileOpHelper *
admin_file_op_helper_duplicate_for_undo_op (NautilusFileOpHelper *helper)
{
    NautilusAdminFileOpHelper *self;
    NautilusAdminFileOpHelper *ret;

    self = NAUTILUS_ADMIN_FILE_OP_HELPER (helper);

    ret = g_object_new (NAUTILUS_TYPE_ADMIN_FILE_OP_HELPER, NULL);
    ret->file_op = self->file_op;
    ret->primary_helper = g_object_ref (self);

    return NAUTILUS_FILE_OP_HELPER (ret);
}

static void
nautilus_admin_file_op_helper_finalize (GObject *obj)
{
    NautilusAdminFileOpHelper *self;

    g_assert (NAUTILUS_IS_ADMIN_FILE_OP_HELPER (obj));

    self = NAUTILUS_ADMIN_FILE_OP_HELPER (obj);

    if (self->map_original_to_admin_directory_for_enumerate != NULL)
    {
        g_hash_table_unref (self->map_original_to_admin_directory_for_enumerate);
    }

    if (self->original_dest_files_opaquely_handled_as_admin != NULL)
    {
        g_hash_table_unref (self->original_dest_files_opaquely_handled_as_admin);
    }

    if (self->parent_window != NULL)
    {
        g_object_remove_weak_pointer (G_OBJECT (self->parent_window),
                                      (gpointer *) &self->parent_window);
    }

    g_clear_object (&self->primary_helper);

    G_OBJECT_CLASS (nautilus_admin_file_op_helper_parent_class)->finalize (obj);
}

static void
nautilus_admin_file_op_helper_class_init (NautilusAdminFileOpHelperClass *klass)
{
    GObjectClass *oclass = G_OBJECT_CLASS (klass);
    NautilusFileOpHelperClass *base_class = NAUTILUS_FILE_OP_HELPER_CLASS (klass);

    oclass->finalize = nautilus_admin_file_op_helper_finalize;
    base_class->duplicate_for_undo_op = admin_file_op_helper_duplicate_for_undo_op;
    base_class->retry_enumerate_children_on_error = admin_file_op_helper_enumerate_children_on_error;
    base_class->get_child = admin_file_op_helper_get_child;
    base_class->copy_file = admin_file_op_helper_copy_file;
    base_class->move_file = admin_file_op_helper_move_file;
    base_class->delete_file = admin_file_op_helper_delete_file;
    base_class->make_directory = admin_file_op_helper_make_directory;
    base_class->get_copy_move_target = admin_op_get_copy_move_target;
    base_class->get_copy_move_target_for_display_name = admin_op_get_copy_move_target_for_display_name;
}

static void
copy_move_async (gboolean                        is_move,
                 GList                          *files,
                 GFile                          *target_dir,
                 GtkWindow                      *parent_window,
                 NautilusFileOperationsDBusData *dbus_data,
                 NautilusCopyCallback            done_callback,
                 gpointer                        done_callback_data,
                 gboolean                        as_admin_if_needed)
{
    g_autoptr (NautilusFileOpHelper) helper = NULL;

    if (as_admin_if_needed)
    {
        NautilusAdminFileOp op;
        NautilusAdminFileOpHelper *admin_helper;

        op = is_move ? NAUTILUS_ADMIN_FILE_OP_MOVE : NAUTILUS_ADMIN_FILE_OP_COPY;

        admin_helper = nautilus_admin_file_op_helper_new (op, parent_window);
        nautilus_file_op_helper_set_copy_move_callback (NAUTILUS_FILE_OP_HELPER (admin_helper), done_callback);

        helper = NAUTILUS_FILE_OP_HELPER (admin_helper);
    }
    else
    {
        helper = nautilus_simple_file_op_helper_new ();
        nautilus_file_op_helper_set_copy_move_callback (helper, done_callback);
    }

    if (is_move)
    {
        nautilus_file_operations_move_async (files,
                                             target_dir,
                                             parent_window,
                                             dbus_data,
                                             helper,
                                             done_callback_data);
    }
    else
    {
        nautilus_file_operations_copy_async (files,
                                             target_dir,
                                             parent_window,
                                             dbus_data,
                                             helper,
                                             done_callback_data);
    }
}

void
nautilus_file_ops_controller_copy_move (const GList                    *item_uris,
                                        const char                     *target_dir,
                                        GdkDragAction                   copy_action,
                                        GtkWidget                      *parent_view,
                                        NautilusFileOperationsDBusData *dbus_data,
                                        NautilusCopyCallback            done_callback,
                                        gpointer                        done_callback_data,
                                        gboolean                        as_admin_if_needed)
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
            break;
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
        copy_move_async (FALSE,
                         locations,
                         dest,
                         parent_window,
                         dbus_data,
                         done_callback, done_callback_data,
                         as_admin_if_needed);
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
                                                            done_callback_data);
        }
        else
        {
            copy_move_async (TRUE,
                             locations,
                             dest,
                             parent_window,
                             dbus_data,
                             done_callback,
                             done_callback_data,
                             as_admin_if_needed);
        }
    }
    else
    {
        g_assert (!as_admin_if_needed); /*TODO: Implement admin support for link op! */

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

void
nautilus_file_ops_controller_delete_async (GList                  *files,
                                           GtkWindow              *parent_window,
                                           NautilusDeleteCallback  done_callback,
                                           gpointer                done_callback_data,
                                           gboolean                as_admin_if_needed)
{
    g_autoptr (NautilusFileOpHelper) helper = NULL;

    if (as_admin_if_needed)
    {
        NautilusAdminFileOpHelper *admin_helper;

        admin_helper = nautilus_admin_file_op_helper_new (NAUTILUS_ADMIN_FILE_OP_DELETE, parent_window);
        nautilus_file_op_helper_set_delete_callback (NAUTILUS_FILE_OP_HELPER (admin_helper), done_callback);

        helper = NAUTILUS_FILE_OP_HELPER (admin_helper);
    }
    else
    {
        helper = nautilus_simple_file_op_helper_new ();
        nautilus_file_op_helper_set_delete_callback (helper, done_callback);
    }

    nautilus_file_operations_delete_async (files,
                                           parent_window,
                                           NULL,
                                           helper,
                                           done_callback_data);
}
