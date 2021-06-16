#pragma once

#include <gio/gio.h>
#include "nautilus-file-op-callback-types.h"

typedef struct
{
    GError                          *gerror;
    gboolean                         user_response_cancel_all;
} NautilusFileSubopError;

#define NAUTILUS_TYPE_FILE_OP_HELPER nautilus_file_op_helper_get_type ()

G_DECLARE_DERIVABLE_TYPE (NautilusFileOpHelper, nautilus_file_op_helper,
                          NAUTILUS, FILE_OP_HELPER,
                          GObject)

struct _NautilusFileOpHelperClass
{
    GObjectClass parent_class;

    /* Public virtual methods */
    GFile *(*get_child)                                                (NautilusFileOpHelper   *self,
                                                                        GFile                  *dir,
                                                                        const char             *name);
    gboolean (*copy_file)                                              (NautilusFileOpHelper   *self,
                                                                        GFile                  *source,
                                                                        GFile                  *destination,
                                                                        GFileCopyFlags          flags,
                                                                        GCancellable           *cancellable,
                                                                        GFileProgressCallback   progress_callback,
                                                                        gpointer                progress_callback_data,
                                                                        NautilusFileSubopError *subop_error);
    gboolean (*move_file)                                              (NautilusFileOpHelper   *self,
                                                                        GFile                  *source,
                                                                        GFile                  *destination,
                                                                        GFileCopyFlags          flags,
                                                                        GCancellable           *cancellable,
                                                                        GFileProgressCallback   progress_callback,
                                                                        gpointer                progress_callback_data,
                                                                        NautilusFileSubopError *subop_error);
    gboolean (*delete_file)                                            (NautilusFileOpHelper   *self,
                                                                        GFile                  *file,
                                                                        GCancellable           *cancellable,
                                                                        NautilusFileSubopError *subop_error);
    gboolean (*make_directory)                                         (NautilusFileOpHelper   *self,
                                                                        GFile                  *file,
                                                                        GCancellable           *cancellable,
                                                                        NautilusFileSubopError *subop_error);
    GFile *(*get_copy_move_target)                                     (NautilusFileOpHelper   *self,
                                                                        GFile                  *src,
                                                                        GFile                  *dest_dir,
                                                                        const char             *name);
    GFile *(*get_copy_move_target_for_display_name)                    (NautilusFileOpHelper   *self,
                                                                        GFile                  *src,
                                                                        GFile                  *dest_dir,
                                                                        const char             *target_display_name,
                                                                        GError                **error);
    NautilusFileOpHelper *(*duplicate_for_undo_op)                     (NautilusFileOpHelper   *self);

    /* Private virtual methods */
    GFileEnumerator *(* retry_enumerate_children_on_error)             (NautilusFileOpHelper   *self,
                                                                        GError                 *original_error,
                                                                        GFile                  *file,
                                                                        const char             *attributes,
                                                                        GFileQueryInfoFlags    flags,
                                                                        GCancellable           *cancellable,
                                                                        NautilusFileSubopError *subop_error);
};

NautilusFileOpHelper *nautilus_simple_file_op_helper_new               (void);
void nautilus_file_op_helper_set_copy_move_callback                    (NautilusFileOpHelper  *self,
                                                                        NautilusCopyCallback   done_cb);
void nautilus_file_op_helper_set_delete_callback                       (NautilusFileOpHelper  *self,
                                                                        NautilusDeleteCallback done_cb);
void nautilus_file_op_helper_on_copy_op_done                           (NautilusFileOpHelper  *self,
                                                                        GHashTable            *debuting_uris,
                                                                        gboolean               success,
                                                                        gpointer               callback_data);
void nautilus_file_op_helper_on_delete_op_done                         (NautilusFileOpHelper  *self,
                                                                        gboolean               user_cancel,
                                                                        gpointer               callback_data);
GFileEnumerator *nautilus_file_op_helper_enumerate_children            (NautilusFileOpHelper   *self,
                                                                        GFile                  *file,
                                                                        const char             *attributes,
                                                                        GFileQueryInfoFlags     flags,
                                                                        GCancellable           *cancellable,
                                                                        NautilusFileSubopError *subop_error);
GFile *nautilus_file_op_helper_get_child                               (NautilusFileOpHelper   *self,
                                                                        GFile                  *dir,
                                                                        const char             *name);
gboolean nautilus_file_op_helper_copy_file                             (NautilusFileOpHelper   *self,
                                                                        GFile                  *source,
                                                                        GFile                  *destination,
                                                                        GFileCopyFlags          flags,
                                                                        GCancellable           *cancellable,
                                                                        GFileProgressCallback   progress_callback,
                                                                        gpointer                progress_callback_data,
                                                                        NautilusFileSubopError *subop_error);
gboolean nautilus_file_op_helper_move_file                             (NautilusFileOpHelper   *self,
                                                                        GFile                  *source,
                                                                        GFile                  *destination,
                                                                        GFileCopyFlags          flags,
                                                                        GCancellable           *cancellable,
                                                                        GFileProgressCallback   progress_callback,
                                                                        gpointer                progress_callback_data,
                                                                        NautilusFileSubopError *subop_error);
gboolean nautilus_file_op_helper_delete_file                           (NautilusFileOpHelper   *self,
                                                                        GFile                  *file,
                                                                        GCancellable           *cancellable,
                                                                        NautilusFileSubopError *subop_error);
gboolean nautilus_file_op_helper_make_directory                        (NautilusFileOpHelper   *self,
                                                                        GFile                  *file,
                                                                        GCancellable           *cancellable,
                                                                        NautilusFileSubopError *subop_error);
GFile *nautilus_file_op_helper_get_copy_move_target                    (NautilusFileOpHelper   *self,
                                                                        GFile                  *src,
                                                                        GFile                  *dest_dir,
                                                                        const char             *name);
GFile *nautilus_file_op_helper_get_copy_move_target_for_display_name   (NautilusFileOpHelper   *self,
                                                                        GFile                  *src,
                                                                        GFile                  *dest_dir,
                                                                        const char             *target_display_name,
                                                                        GError                **error);
NautilusFileOpHelper *nautilus_file_op_helper_duplicate_for_undo_op    (NautilusFileOpHelper   *self);
