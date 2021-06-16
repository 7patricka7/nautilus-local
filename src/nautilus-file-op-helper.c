#include <config.h>
#include "nautilus-file-op-helper.h"

typedef struct
{
    NautilusCopyCallback original_copy_op_done_cb;
    NautilusDeleteCallback original_delete_op_done_cb;
} NautilusFileOpHelperPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (NautilusFileOpHelper, nautilus_file_op_helper, G_TYPE_OBJECT)

#define NAUTILUS_TYPE_SIMPLE_FILE_OP_HELPER nautilus_simple_file_op_helper_get_type ()

G_DECLARE_FINAL_TYPE (NautilusSimpleFileOpHelper, nautilus_simple_file_op_helper,
                      NAUTILUS, SIMPLE_FILE_OP_HELPER,
                      NautilusFileOpHelper)

struct _NautilusSimpleFileOpHelper
{
    NautilusFileOpHelper parent_instance;
};

G_DEFINE_TYPE (NautilusSimpleFileOpHelper, nautilus_simple_file_op_helper, NAUTILUS_TYPE_FILE_OP_HELPER)

static void
nautilus_file_op_helper_class_init (NautilusFileOpHelperClass *klass)
{
    klass->get_child = NULL;
    klass->retry_enumerate_children_on_error = NULL;
    klass->copy_file = NULL;
    klass->move_file = NULL;
    klass->delete_file = NULL;
    klass->make_directory = NULL;
}

static void
nautilus_file_op_helper_init (NautilusFileOpHelper *self)
{
    NautilusFileOpHelperPrivate *priv;

    priv = nautilus_file_op_helper_get_instance_private (self);

    priv->original_copy_op_done_cb = NULL;
    priv->original_delete_op_done_cb = NULL;
}

static NautilusFileOpHelper *
simple_file_op_helper_duplicate_for_undo_op (NautilusFileOpHelper *self)
{
    NautilusSimpleFileOpHelper *ret;

    ret = g_object_new (NAUTILUS_TYPE_SIMPLE_FILE_OP_HELPER, NULL);

    return NAUTILUS_FILE_OP_HELPER (ret);
}

static void
nautilus_simple_file_op_helper_class_init (NautilusSimpleFileOpHelperClass *klass)
{
    NautilusFileOpHelperClass *base_class = NAUTILUS_FILE_OP_HELPER_CLASS (klass);

    base_class->duplicate_for_undo_op = simple_file_op_helper_duplicate_for_undo_op;
}

static void
nautilus_simple_file_op_helper_init (NautilusSimpleFileOpHelper *self)
{
}

void
nautilus_file_op_helper_set_copy_move_callback (NautilusFileOpHelper *self,
                                                NautilusCopyCallback  done_cb)
{
    NautilusFileOpHelperPrivate *priv;

    priv = nautilus_file_op_helper_get_instance_private (self);

    priv->original_copy_op_done_cb = done_cb;
}

void
nautilus_file_op_helper_set_delete_callback (NautilusFileOpHelper   *self,
                                             NautilusDeleteCallback  done_cb)
{
    NautilusFileOpHelperPrivate *priv;

    priv = nautilus_file_op_helper_get_instance_private (self);

    priv->original_delete_op_done_cb = done_cb;
}

void
nautilus_file_op_helper_on_copy_op_done (NautilusFileOpHelper *self,
                                         GHashTable           *debuting_uris,
                                         gboolean              success,
                                         gpointer              callback_data)
{
    NautilusFileOpHelperPrivate *priv;

    priv = nautilus_file_op_helper_get_instance_private (self);

    if (priv->original_copy_op_done_cb != NULL)
    {
        priv->original_copy_op_done_cb (debuting_uris, success, callback_data);
    }
}

void
nautilus_file_op_helper_on_delete_op_done (NautilusFileOpHelper *self,
                                           gboolean              user_cancel,
                                           gpointer              callback_data)
{
    NautilusFileOpHelperPrivate *priv;

    priv = nautilus_file_op_helper_get_instance_private (self);

    if (priv->original_delete_op_done_cb != NULL)
    {
        priv->original_delete_op_done_cb (user_cancel, callback_data);
    }
}

GFileEnumerator *
nautilus_file_op_helper_enumerate_children (NautilusFileOpHelper   *self,
                                            GFile                  *file,
                                            const char             *attributes,
                                            GFileQueryInfoFlags     flags,
                                            GCancellable           *cancellable,
                                            NautilusFileSubopError *subop_error)
{
    NautilusFileOpHelperClass *klass;
    GFileEnumerator *enumerator;
    g_autoptr (GError) error = NULL;

    if (subop_error != NULL)
    {
        subop_error->user_response_cancel_all = FALSE;
    }

    enumerator = g_file_enumerate_children (file, attributes, flags, cancellable, &error);

    if (enumerator != NULL)
    {
        return enumerator;
    }

    klass = NAUTILUS_FILE_OP_HELPER_GET_CLASS (self);

    if (klass->retry_enumerate_children_on_error != NULL)
    {
        enumerator = klass->retry_enumerate_children_on_error (self, error,
                                                               file, attributes, flags, cancellable,
                                                               subop_error);
    }

    if ((enumerator == NULL) && (subop_error != NULL))
    {
        if (subop_error->gerror == NULL)
        {
            subop_error->gerror = g_steal_pointer (&error);
        }
    }

    return enumerator;
}

GFile *
nautilus_file_op_helper_get_child (NautilusFileOpHelper *self,
                                   GFile                *dir,
                                   const char           *name)
{
    NautilusFileOpHelperClass *klass;
    GFile *child;

    klass = NAUTILUS_FILE_OP_HELPER_GET_CLASS (self);

    if (klass->get_child != NULL)
    {
        child = klass->get_child (self, dir, name);
    }
    else
    {
        child = g_file_get_child (dir, name);
    }

    return child;
}

gboolean
nautilus_file_op_helper_copy_file (NautilusFileOpHelper   *self,
                                   GFile                  *source,
                                   GFile                  *destination,
                                   GFileCopyFlags          flags,
                                   GCancellable           *cancellable,
                                   GFileProgressCallback   progress_callback,
                                   gpointer                progress_callback_data,
                                   NautilusFileSubopError *subop_error)
{
    NautilusFileOpHelperClass *klass;
    gboolean success;

    if (subop_error != NULL)
    {
        subop_error->user_response_cancel_all = FALSE;
    }

    klass = NAUTILUS_FILE_OP_HELPER_GET_CLASS (self);

    if (klass->copy_file != NULL)
    {
        success = klass->copy_file (self,
                                    source,
                                    destination,
                                    flags,
                                    cancellable,
                                    progress_callback,
                                    progress_callback_data,
                                    subop_error);
    }
    else
    {
        success = g_file_copy (source,
                               destination,
                               flags,
                               cancellable,
                               progress_callback,
                               progress_callback_data,
                               (subop_error != NULL) ? &subop_error->gerror : NULL);
    }

    return success;
}

gboolean
nautilus_file_op_helper_move_file (NautilusFileOpHelper   *self,
                                   GFile                  *source,
                                   GFile                  *destination,
                                   GFileCopyFlags          flags,
                                   GCancellable           *cancellable,
                                   GFileProgressCallback   progress_callback,
                                   gpointer                progress_callback_data,
                                   NautilusFileSubopError *subop_error)
{
    NautilusFileOpHelperClass *klass;
    gboolean success;

    if (subop_error != NULL)
    {
        subop_error->user_response_cancel_all = FALSE;
    }

    klass = NAUTILUS_FILE_OP_HELPER_GET_CLASS (self);

    if (klass->move_file != NULL)
    {
        success = klass->move_file (self,
                                    source,
                                    destination,
                                    flags,
                                    cancellable,
                                    progress_callback,
                                    progress_callback_data,
                                    subop_error);
    }
    else
    {
        success = g_file_move (source,
                               destination,
                               flags,
                               cancellable,
                               progress_callback,
                               progress_callback_data,
                               (subop_error != NULL) ? &subop_error->gerror : NULL);
    }

    return success;
}

gboolean
nautilus_file_op_helper_delete_file (NautilusFileOpHelper   *self,
                                     GFile                  *file,
                                     GCancellable           *cancellable,
                                     NautilusFileSubopError *subop_error)
{
    NautilusFileOpHelperClass *klass;
    gboolean success;

    if (subop_error != NULL)
    {
        subop_error->user_response_cancel_all = FALSE;
    }

    klass = NAUTILUS_FILE_OP_HELPER_GET_CLASS (self);

    if (klass->delete_file != NULL)
    {
        success = klass->delete_file (self,
                                      file,
                                      cancellable,
                                      subop_error);
    }
    else
    {
        success = g_file_delete (file,
                                 cancellable,
                                 (subop_error != NULL) ? &subop_error->gerror : NULL);
    }

    return success;
}

gboolean
nautilus_file_op_helper_make_directory (NautilusFileOpHelper   *self,
                                        GFile                  *file,
                                        GCancellable           *cancellable,
                                        NautilusFileSubopError *subop_error)
{
    NautilusFileOpHelperClass *klass;
    gboolean success;

    if (subop_error != NULL)
    {
        subop_error->user_response_cancel_all = FALSE;
    }

    klass = NAUTILUS_FILE_OP_HELPER_GET_CLASS (self);

    if (klass->make_directory != NULL)
    {
        success = klass->make_directory (self,
                                         file,
                                         cancellable,
                                         subop_error);
    }
    else
    {
        success = g_file_make_directory (file,
                                         cancellable,
                                         (subop_error != NULL) ? &subop_error->gerror : NULL);
    }

    return success;
}

GFile *
nautilus_file_op_helper_get_copy_move_target (NautilusFileOpHelper *self,
                                              GFile                *src,
                                              GFile                *dest_dir,
                                              const char           *name)
{
    NautilusFileOpHelperClass *klass;
    GFile *child;

    klass = NAUTILUS_FILE_OP_HELPER_GET_CLASS (self);

    if (klass->get_copy_move_target != NULL)
    {
        child = klass->get_copy_move_target (self, src, dest_dir, name);
    }
    else
    {
        child = g_file_get_child (dest_dir, name);
    }

    return child;
}

GFile *
nautilus_file_op_helper_get_copy_move_target_for_display_name (NautilusFileOpHelper  *self,
                                                               GFile                 *src,
                                                               GFile                 *dest_dir,
                                                               const char            *target_display_name,
                                                               GError               **error)
{
    NautilusFileOpHelperClass *klass;
    GFile *child;

    klass = NAUTILUS_FILE_OP_HELPER_GET_CLASS (self);

    if (klass->get_copy_move_target_for_display_name != NULL)
    {
        child = klass->get_copy_move_target_for_display_name (self, src, dest_dir, target_display_name, error);
    }
    else
    {
        child = g_file_get_child_for_display_name (dest_dir, target_display_name, error);
    }

    return child;
}

NautilusFileOpHelper *
nautilus_file_op_helper_duplicate_for_undo_op (NautilusFileOpHelper *self)
{
    NautilusFileOpHelperClass *klass;

    klass = NAUTILUS_FILE_OP_HELPER_GET_CLASS (self);

    g_assert (klass->duplicate_for_undo_op != NULL);

    return klass->duplicate_for_undo_op (self);
}

NautilusFileOpHelper *
nautilus_simple_file_op_helper_new (void)
{
    NautilusSimpleFileOpHelper *self;

    self = g_object_new (NAUTILUS_TYPE_SIMPLE_FILE_OP_HELPER, NULL);

    return NAUTILUS_FILE_OP_HELPER (self);
}
