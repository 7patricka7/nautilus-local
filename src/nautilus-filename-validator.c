/*
 * Copyright (C) 2016 the Nautilus developers
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <glib/gi18n.h>

#include "nautilus-filename-validator.h"
#include "nautilus-file-utilities.h"

#define FILE_NAME_DUPLICATED_LABEL_TIMEOUT 500

struct _NautilusFilenameValidator
{
    GObject parent_instance;

    GtkWidget *error_revealer;
    GtkWidget *error_label;
    GtkWidget *name_entry;
    GtkWidget *activate_button;
    NautilusDirectory *containing_directory;
    gboolean target_is_folder;
    char *original_name;
    char *extension;

    gboolean duplicated_is_folder;
    gint duplicated_label_timeout_id;
};

enum
{
    NAME_ACCEPTED,
    LAST_SIGNAL
};

enum
{
    PROP_ERROR_REVEALER = 1,
    PROP_ERROR_LABEL,
    PROP_NAME_ENTRY,
    PROP_ACTION_BUTTON,
    PROP_CONTAINING_DIRECTORY,
    PROP_TARGET_IS_FOLDER,
    NUM_PROPERTIES
};

static guint signals[LAST_SIGNAL];

G_DEFINE_TYPE (NautilusFilenameValidator, nautilus_filename_validator, G_TYPE_OBJECT)

void
nautilus_filename_validator_set_target_is_folder (NautilusFilenameValidator *self,
                                                  gboolean                   is_folder)
{
    g_object_set (self, "target-is-folder", is_folder, NULL);
}

void
nautilus_filename_validator_set_original_name (NautilusFilenameValidator *self,
                                               const char                *original_name)
{
    g_free (self->original_name);
    self->original_name = g_strdup (original_name);
}

void
nautilus_filename_validator_set_extension (NautilusFilenameValidator *self,
                                           const char                *extension)
{
    g_free (self->extension);
    self->extension = g_strdup (extension);
}

void
nautilus_filename_validator_set_containing_directory (NautilusFilenameValidator *self,
                                                      NautilusDirectory         *directory)
{
    g_assert (NAUTILUS_IS_DIRECTORY (directory));

    g_object_set (self, "containing-directory", directory, NULL);
}

static gboolean
nautilus_filename_validator_is_name_too_long (NautilusFilenameValidator *self,
                                              gchar                     *name)
{
    size_t name_length;
    g_autoptr (GFile) location = NULL;
    glong max_name_length;

    name_length = strlen (name);
    location = nautilus_directory_get_location (self->containing_directory);
    max_name_length = nautilus_get_max_child_name_length_for_location (location);

    if (max_name_length == -1)
    {
        /* We don't know, so let's give it a chance */
        return FALSE;
    }
    else
    {
        return name_length > (gulong) max_name_length;
    }
}

static gboolean
nautilus_filename_validator_ignore_existing_file (NautilusFilenameValidator *self,
                                                  NautilusFile              *existing_file)
{
    return (self->original_name != NULL &&
            nautilus_file_compare_display_name (existing_file, self->original_name) == 0);
}

gchar *
nautilus_filename_validator_get_new_name (NautilusFilenameValidator *self)
{
    g_autofree char *basename = NULL;

    basename = g_strstrip (g_strdup (gtk_editable_get_text (GTK_EDITABLE (self->name_entry))));

    if (self->extension != NULL && !g_str_has_suffix (basename, self->extension))
    {
        return g_strconcat (basename, self->extension, NULL);
    }
    else
    {
        return g_steal_pointer (&basename);
    }
}

static gboolean
nautilus_filename_validator_name_is_valid (NautilusFilenameValidator  *self,
                                           gchar                      *name,
                                           gchar                     **error_message)
{
    gboolean is_folder = self->target_is_folder;
    gboolean is_valid;

    is_valid = TRUE;
    if (strlen (name) == 0)
    {
        is_valid = FALSE;
    }
    else if (strstr (name, "/") != NULL)
    {
        is_valid = FALSE;
        *error_message = is_folder ? _("Folder names cannot contain “/”.") :
                                     _("File names cannot contain “/”.");
    }
    else if (strcmp (name, ".") == 0)
    {
        is_valid = FALSE;
        *error_message = is_folder ? _("A folder cannot be called “.”.") :
                                     _("A file cannot be called “.”.");
    }
    else if (strcmp (name, "..") == 0)
    {
        is_valid = FALSE;
        *error_message = is_folder ? _("A folder cannot be called “..”.") :
                                     _("A file cannot be called “..”.");
    }
    else if (nautilus_filename_validator_is_name_too_long (self, name))
    {
        is_valid = FALSE;
        *error_message = is_folder ? _("Folder name is too long.") :
                                     _("File name is too long.");
    }

    if (is_valid && g_str_has_prefix (name, "."))
    {
        /* We must warn about the side effect */
        *error_message = is_folder ? _("Folders with “.” at the beginning of their name are hidden.") :
                                     _("Files with “.” at the beginning of their name are hidden.");
    }

    return is_valid;
}

static gboolean
duplicated_file_label_show (NautilusFilenameValidator *self)
{
    if (self->duplicated_is_folder)
    {
        gtk_label_set_label (GTK_LABEL (self->error_label),
                             _("A folder with that name already exists."));
    }
    else
    {
        gtk_label_set_label (GTK_LABEL (self->error_label),
                             _("A file with that name already exists."));
    }

    gtk_revealer_set_reveal_child (GTK_REVEALER (self->error_revealer),
                                   TRUE);

    self->duplicated_label_timeout_id = 0;

    return G_SOURCE_REMOVE;
}

static void
filename_validator_process_new_name (NautilusFilenameValidator *self,
                                     gboolean                  *duplicated_name,
                                     gboolean                  *valid_name)
{
    g_autofree gchar *name = NULL;
    gchar *error_message = NULL;
    NautilusFile *existing_file;

    g_return_if_fail (NAUTILUS_IS_DIRECTORY (self->containing_directory));

    name = nautilus_filename_validator_get_new_name (self);
    *valid_name = nautilus_filename_validator_name_is_valid (self,
                                                             name,
                                                             &error_message);

    gtk_label_set_label (GTK_LABEL (self->error_label), error_message);
    gtk_revealer_set_reveal_child (GTK_REVEALER (self->error_revealer),
                                   error_message != NULL);

    existing_file = nautilus_directory_get_file_by_name (self->containing_directory, name);
    *duplicated_name = existing_file != NULL &&
                       !nautilus_filename_validator_ignore_existing_file (self,
                                                                          existing_file);

    gtk_widget_set_sensitive (self->activate_button, *valid_name && !*duplicated_name);

    if (self->duplicated_label_timeout_id != 0)
    {
        g_source_remove (self->duplicated_label_timeout_id);
        self->duplicated_label_timeout_id = 0;
    }

    if (*duplicated_name)
    {
        self->duplicated_is_folder = nautilus_file_is_directory (existing_file);
    }

    if (existing_file != NULL)
    {
        nautilus_file_unref (existing_file);
    }
}

static void
filename_validator_on_changed_directory_info_ready (NautilusDirectory *directory,
                                                    GList             *files,
                                                    gpointer           user_data)
{
    NautilusFilenameValidator *self = NAUTILUS_FILENAME_VALIDATOR (user_data);
    gboolean duplicated_name;
    gboolean valid_name;

    filename_validator_process_new_name (self,
                                         &duplicated_name,
                                         &valid_name);

    /* Report duplicated file only if not other message shown (for instance,
     * folders like "." or ".." will always exists, but we consider it as an
     * error, not as a duplicated file or if the name is the same as the file
     * we are renaming also don't report as a duplicated */
    if (duplicated_name && valid_name)
    {
        self->duplicated_label_timeout_id = g_timeout_add (FILE_NAME_DUPLICATED_LABEL_TIMEOUT,
                                                           (GSourceFunc) duplicated_file_label_show,
                                                           self);
    }
}

static void
filename_validator_on_changed (gpointer user_data)
{
    NautilusFilenameValidator *self = NAUTILUS_FILENAME_VALIDATOR (user_data);

    nautilus_directory_call_when_ready (self->containing_directory,
                                        NAUTILUS_FILE_ATTRIBUTE_INFO,
                                        TRUE,
                                        filename_validator_on_changed_directory_info_ready,
                                        self);
}

static void
filename_validator_on_activate_directory_info_ready (NautilusDirectory *directory,
                                                     GList             *files,
                                                     gpointer           user_data)
{
    NautilusFilenameValidator *self = NAUTILUS_FILENAME_VALIDATOR (user_data);
    gboolean duplicated_name;
    gboolean valid_name;

    filename_validator_process_new_name (self,
                                         &duplicated_name,
                                         &valid_name);

    if (valid_name && !duplicated_name)
    {
        g_signal_emit (self, signals[NAME_ACCEPTED], 0);
    }
    else
    {
        /* Report duplicated file only if not other message shown (for instance,
         * folders like "." or ".." will always exists, but we consider it as an
         * error, not as a duplicated file) */
        if (duplicated_name && valid_name)
        {
            /* Show it inmediatily since the user tried to trigger the action */
            duplicated_file_label_show (self);
        }
    }
}

static void
filename_validator_on_activate (gpointer user_data)
{
    NautilusFilenameValidator *self = NAUTILUS_FILENAME_VALIDATOR (user_data);

    nautilus_directory_call_when_ready (self->containing_directory,
                                        NAUTILUS_FILE_ATTRIBUTE_INFO,
                                        TRUE,
                                        filename_validator_on_activate_directory_info_ready,
                                        self);
}

static void
nautilus_filename_validator_init (NautilusFilenameValidator *self)
{
}

static void
nautilus_filename_validator_set_property (GObject      *object,
                                          guint         prop_id,
                                          const GValue *value,
                                          GParamSpec   *pspec)
{
    NautilusFilenameValidator *self = NAUTILUS_FILENAME_VALIDATOR (object);

    switch (prop_id)
    {
        case PROP_ERROR_REVEALER:
        {
            self->error_revealer = GTK_WIDGET (g_value_get_object (value));
        }
        break;

        case PROP_ERROR_LABEL:
        {
            self->error_label = GTK_WIDGET (g_value_get_object (value));
        }
        break;

        case PROP_NAME_ENTRY:
        {
            self->name_entry = GTK_WIDGET (g_value_get_object (value));

            g_signal_connect_swapped (G_OBJECT (self->name_entry),
                                      "activate",
                                      (GCallback) filename_validator_on_activate,
                                      self);
            g_signal_connect_swapped (G_OBJECT (self->name_entry),
                                      "changed",
                                      (GCallback) filename_validator_on_changed,
                                      self);
        }
        break;

        case PROP_ACTION_BUTTON:
        {
            self->activate_button = GTK_WIDGET (g_value_get_object (value));

            g_signal_connect_swapped (G_OBJECT (self->activate_button),
                                      "clicked",
                                      (GCallback) filename_validator_on_activate,
                                      self);
        }
        break;

        case PROP_CONTAINING_DIRECTORY:
        {
            g_clear_object (&self->containing_directory);

            self->containing_directory = NAUTILUS_DIRECTORY (g_value_dup_object (value));
        }
        break;

        case PROP_TARGET_IS_FOLDER:
        {
            self->target_is_folder = g_value_get_boolean (value);
        }
        break;

        default:
        {
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        }
        break;
    }
}

static void
nautilus_filename_validator_finalize (GObject *object)
{
    NautilusFilenameValidator *self;

    self = NAUTILUS_FILENAME_VALIDATOR (object);

    if (self->containing_directory != NULL)
    {
        nautilus_directory_cancel_callback (self->containing_directory,
                                            filename_validator_on_changed_directory_info_ready,
                                            self);
        nautilus_directory_cancel_callback (self->containing_directory,
                                            filename_validator_on_activate_directory_info_ready,
                                            self);
        g_clear_object (&self->containing_directory);
    }

    if (self->duplicated_label_timeout_id > 0)
    {
        g_source_remove (self->duplicated_label_timeout_id);
        self->duplicated_label_timeout_id = 0;
    }

    g_free (self->original_name);
    g_free (self->extension);

    G_OBJECT_CLASS (nautilus_filename_validator_parent_class)->finalize (object);
}

static void
nautilus_filename_validator_class_init (NautilusFilenameValidatorClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->set_property = nautilus_filename_validator_set_property;
    object_class->finalize = nautilus_filename_validator_finalize;

    signals[NAME_ACCEPTED] =
        g_signal_new ("name-accepted",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_FIRST,
                      0,
                      NULL, NULL,
                      g_cclosure_marshal_generic,
                      G_TYPE_NONE, 0);

    g_object_class_install_property (
        object_class,
        PROP_ERROR_REVEALER,
        g_param_spec_object ("error-revealer",
                             "Error Revealer",
                             "The error label revealer",
                             GTK_TYPE_WIDGET,
                             G_PARAM_WRITABLE |
                             G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property (
        object_class,
        PROP_ERROR_LABEL,
        g_param_spec_object ("error-label",
                             "Error Label",
                             "The label used for displaying errors",
                             GTK_TYPE_WIDGET,
                             G_PARAM_WRITABLE |
                             G_PARAM_CONSTRUCT_ONLY));
    g_object_class_install_property (
        object_class,
        PROP_NAME_ENTRY,
        g_param_spec_object ("name-entry",
                             "Name Entry",
                             "The entry for the file name",
                             GTK_TYPE_WIDGET,
                             G_PARAM_WRITABLE |
                             G_PARAM_CONSTRUCT_ONLY));
    g_object_class_install_property (
        object_class,
        PROP_ACTION_BUTTON,
        g_param_spec_object ("activate-button",
                             "Activate Button",
                             "The activate button of the widget",
                             GTK_TYPE_WIDGET,
                             G_PARAM_WRITABLE |
                             G_PARAM_CONSTRUCT_ONLY));
    g_object_class_install_property (
        object_class,
        PROP_CONTAINING_DIRECTORY,
        g_param_spec_object ("containing-directory",
                             "Containing Directory",
                             "The directory used to check for duplicate names",
                             NAUTILUS_TYPE_DIRECTORY,
                             G_PARAM_WRITABLE));
    g_object_class_install_property (
        object_class,
        PROP_TARGET_IS_FOLDER,
        g_param_spec_boolean ("target-is-folder", NULL, NULL,
                              FALSE,
                              G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));
}
