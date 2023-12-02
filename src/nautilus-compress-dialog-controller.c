/*
 * Copyright (C) 2023 The GNOME project contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <adwaita.h>
#include <glib/gi18n.h>
#include <gnome-autoar/gnome-autoar.h>

#include "nautilus-compress-dialog-controller.h"

#include "nautilus-filename-validator.h"
#include "nautilus-global-preferences.h"

struct _NautilusCompressDialog
{
    AdwWindow parent_instance;

    NautilusFilenameValidator *validator;

    GtkWidget *activate_button;
    GtkWidget *error_label;
    GtkWidget *name_entry;
    GtkWidget *extension_dropdown;
    GtkSizeGroup *extension_sizegroup;
    GtkWidget *passphrase_label;
    GtkWidget *passphrase_entry;
    GtkWidget *passphrase_confirm_label;
    GtkWidget *passphrase_confirm_entry;

    CompressCallback callback;
    gpointer callback_data;

    const char *extension;
    gchar *passphrase;
    gchar *passphrase_confirm;
};

G_DEFINE_TYPE (NautilusCompressDialog, nautilus_compress_dialog, ADW_TYPE_WINDOW);

#define NAUTILUS_TYPE_COMPRESS_ITEM (nautilus_compress_item_get_type ())
G_DECLARE_FINAL_TYPE (NautilusCompressItem, nautilus_compress_item, NAUTILUS, COMPRESS_ITEM, GObject)

struct _NautilusCompressItem
{
    GObject parent_instance;
    NautilusCompressionFormat format;
    char *extension;
    char *description;
};

G_DEFINE_TYPE (NautilusCompressItem, nautilus_compress_item, G_TYPE_OBJECT);

static void
nautilus_compress_item_init (NautilusCompressItem *item)
{
}

static void
nautilus_compress_item_finalize (GObject *object)
{
    NautilusCompressItem *item = NAUTILUS_COMPRESS_ITEM (object);

    g_free (item->extension);
    g_free (item->description);

    G_OBJECT_CLASS (nautilus_compress_item_parent_class)->finalize (object);
}

static void
nautilus_compress_item_class_init (NautilusCompressItemClass *class)
{
    GObjectClass *object_class = G_OBJECT_CLASS (class);

    object_class->finalize = nautilus_compress_item_finalize;
}

static NautilusCompressItem *
nautilus_compress_item_new (NautilusCompressionFormat  format,
                            const char                *extension,
                            const char                *description)
{
    NautilusCompressItem *item = g_object_new (NAUTILUS_TYPE_COMPRESS_ITEM, NULL);

    item->format = format;
    item->extension = g_strdup (extension);
    item->description = g_strdup (description);

    return item;
}

static void
update_selected_format (NautilusCompressDialog *self)
{
    gboolean show_passphrase = FALSE;
    guint selected;
    GListModel *model;
    NautilusCompressItem *item;

    selected = gtk_drop_down_get_selected (GTK_DROP_DOWN (self->extension_dropdown));
    if (selected == GTK_INVALID_LIST_POSITION)
    {
        return;
    }

    model = gtk_drop_down_get_model (GTK_DROP_DOWN (self->extension_dropdown));
    item = g_list_model_get_item (model, selected);
    if (item == NULL)
    {
        return;
    }

    if (item->format == NAUTILUS_COMPRESSION_ENCRYPTED_ZIP)
    {
        show_passphrase = TRUE;
    }

    self->extension = item->extension;
    nautilus_filename_validator_set_extension (self->validator, self->extension);

    gtk_widget_set_visible (self->passphrase_label, show_passphrase);
    gtk_widget_set_visible (self->passphrase_entry, show_passphrase);
    gtk_widget_set_visible (self->passphrase_confirm_label, show_passphrase);
    gtk_widget_set_visible (self->passphrase_confirm_entry, show_passphrase);
    if (!show_passphrase)
    {
        gtk_editable_set_text (GTK_EDITABLE (self->passphrase_entry), "");
        gtk_editable_set_text (GTK_EDITABLE (self->passphrase_confirm_entry), "");
    }

    g_settings_set_enum (nautilus_compression_preferences,
                         NAUTILUS_PREFERENCES_DEFAULT_COMPRESSION_FORMAT,
                         item->format);

    /* Since the extension changes when the button is toggled, force a
     * verification of the new file name by simulating an entry change
     */
    gtk_widget_set_sensitive (self->activate_button, FALSE);
    g_signal_emit_by_name (self->name_entry, "changed");
}

static void
extension_dropdown_setup_item (GtkSignalListItemFactory *factory,
                               GtkListItem              *item,
                               gpointer                  user_data)
{
    GtkWidget *title;

    title = gtk_label_new ("");
    gtk_label_set_xalign (GTK_LABEL (title), 0.0);

    g_object_set_data (G_OBJECT (item), "title", title);
    gtk_list_item_set_child (item, title);
}


static void
extension_dropdown_setup_item_full (GtkSignalListItemFactory *factory,
                                    GtkListItem              *item,
                                    gpointer                  user_data)
{
    GtkWidget *hbox, *vbox, *title, *subtitle, *checkmark;

    title = gtk_label_new ("");
    gtk_label_set_xalign (GTK_LABEL (title), 0.0);
    gtk_widget_set_halign (title, GTK_ALIGN_START);

    subtitle = gtk_label_new ("");
    gtk_label_set_xalign (GTK_LABEL (subtitle), 0.0);
    gtk_widget_add_css_class (subtitle, "dim-label");
    gtk_widget_add_css_class (subtitle, "caption");

    checkmark = gtk_image_new_from_icon_name ("object-select-symbolic");

    hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
    vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 3);
    gtk_widget_set_hexpand (vbox, TRUE);

    gtk_box_append (GTK_BOX (hbox), vbox);
    gtk_box_append (GTK_BOX (vbox), title);
    gtk_box_append (GTK_BOX (vbox), subtitle);
    gtk_box_append (GTK_BOX (hbox), checkmark);

    g_object_set_data (G_OBJECT (item), "title", title);
    g_object_set_data (G_OBJECT (item), "subtitle", subtitle);
    g_object_set_data (G_OBJECT (item), "checkmark", checkmark);

    gtk_list_item_set_child (item, hbox);
}

static void
extension_dropdown_on_selected_item_notify (GtkDropDown *dropdown,
                                            GParamSpec  *pspec,
                                            GtkListItem *item)
{
    GtkWidget *checkmark;

    checkmark = g_object_get_data (G_OBJECT (item), "checkmark");

    if (gtk_drop_down_get_selected_item (dropdown) == gtk_list_item_get_item (item))
    {
        gtk_widget_set_opacity (checkmark, 1.0);
    }
    else
    {
        gtk_widget_set_opacity (checkmark, 0.0);
    }
}

static void
extension_dropdown_bind (GtkSignalListItemFactory *factory,
                         GtkListItem              *list_item,
                         gpointer                  user_data)
{
    NautilusCompressDialog *self = NAUTILUS_COMPRESS_DIALOG (user_data);
    GtkWidget *title, *subtitle, *checkmark;
    NautilusCompressItem *item;

    item = gtk_list_item_get_item (list_item);

    title = g_object_get_data (G_OBJECT (list_item), "title");
    subtitle = g_object_get_data (G_OBJECT (list_item), "subtitle");
    checkmark = g_object_get_data (G_OBJECT (list_item), "checkmark");

    gtk_label_set_label (GTK_LABEL (title), item->extension);
    gtk_size_group_add_widget (self->extension_sizegroup, title);

    if (item->format == NAUTILUS_COMPRESSION_ENCRYPTED_ZIP)
    {
        gtk_widget_add_css_class (title, "encrypted_zip");
    }

    if (subtitle)
    {
        gtk_label_set_label (GTK_LABEL (subtitle), item->description);
    }

    if (checkmark)
    {
        g_signal_connect (self->extension_dropdown,
                          "notify::selected-item",
                          G_CALLBACK (extension_dropdown_on_selected_item_notify),
                          list_item);
        extension_dropdown_on_selected_item_notify (GTK_DROP_DOWN (self->extension_dropdown),
                                                    NULL,
                                                    list_item);
    }
}

static void
extension_dropdown_unbind (GtkSignalListItemFactory *factory,
                           GtkListItem              *item,
                           gpointer                  user_data)
{
    NautilusCompressDialog *self = NAUTILUS_COMPRESS_DIALOG (user_data);
    GtkWidget *title;

    if (self->extension_dropdown == NULL)
    {
        return;
    }

    g_signal_handlers_disconnect_by_func (self->extension_dropdown,
                                          extension_dropdown_on_selected_item_notify,
                                          item);

    title = g_object_get_data (G_OBJECT (item), "title");
    if (title)
    {
        gtk_widget_remove_css_class (title, "encrypted_zip");
    }
}

static void
update_passphrase (NautilusCompressDialog  *self,
                   gchar                  **passphrase,
                   GtkEditable             *editable)
{
    const gchar *error_message;

    g_free (*passphrase);
    *passphrase = g_strdup (gtk_editable_get_text (editable));

    /* Simulate a change of the name_entry to ensure the correct sensitivity of
     * the activate_button, but only if the name_entry is valid in order to
     * avoid changes of the error_revealer.
     */
    error_message = gtk_label_get_text (GTK_LABEL (self->error_label));
    if (error_message[0] == '\0')
    {
        gtk_widget_set_sensitive (self->activate_button, FALSE);
        g_signal_emit_by_name (self->name_entry, "changed");
    }
}

static void
passphrase_entry_on_changed (GtkEditable *editable,
                             gpointer     user_data)
{
    NautilusCompressDialog *self = NAUTILUS_COMPRESS_DIALOG (user_data);

    update_passphrase (self, &self->passphrase, editable);
}

static void
passphrase_confirm_entry_on_changed (GtkEditable *editable,
                                     gpointer     user_data)
{
    NautilusCompressDialog *self = NAUTILUS_COMPRESS_DIALOG (user_data);

    update_passphrase (self, &self->passphrase_confirm, editable);
}

static void
activate_button_on_sensitive_notify (GObject    *gobject,
                                     GParamSpec *pspec,
                                     gpointer    user_data)
{
    NautilusCompressDialog *self = NAUTILUS_COMPRESS_DIALOG (user_data);
    NautilusCompressionFormat format;

    format = g_settings_get_enum (nautilus_compression_preferences,
                                  NAUTILUS_PREFERENCES_DEFAULT_COMPRESSION_FORMAT);
    if (format == NAUTILUS_COMPRESSION_ENCRYPTED_ZIP &&
        (self->passphrase == NULL || self->passphrase[0] == '\0' || g_strcmp0 (self->passphrase, self->passphrase_confirm) != 0))
    {
        /* Reset sensitivity of the activate_button if password is not set. */
        gtk_widget_set_sensitive (self->activate_button, FALSE);
    }
}

static void
extension_dropdown_setup (NautilusCompressDialog *self)
{
    GtkListItemFactory *factory, *list_factory;
    GListStore *store;
    NautilusCompressItem *item;
    NautilusCompressionFormat format;

    store = g_list_store_new (NAUTILUS_TYPE_COMPRESS_ITEM);
    item = nautilus_compress_item_new (NAUTILUS_COMPRESSION_ZIP,
                                       ".zip",
                                       _("Compatible with all operating systems."));
    g_list_store_append (store, item);
    g_object_unref (item);
    item = nautilus_compress_item_new (NAUTILUS_COMPRESSION_ENCRYPTED_ZIP,
                                       ".zip",
                                       _("Password protected .zip, must be installed on Windows and Mac."));
    g_list_store_append (store, item);
    g_object_unref (item);
    item = nautilus_compress_item_new (NAUTILUS_COMPRESSION_TAR_XZ,
                                       ".tar.xz",
                                       _("Smaller archives but Linux and Mac only."));
    g_list_store_append (store, item);
    g_object_unref (item);
    item = nautilus_compress_item_new (NAUTILUS_COMPRESSION_7ZIP,
                                       ".7z",
                                       _("Smaller archives but must be installed on Windows and Mac."));
    g_list_store_append (store, item);
    g_object_unref (item);

    factory = gtk_signal_list_item_factory_new ();
    g_signal_connect_object (factory, "setup",
                             G_CALLBACK (extension_dropdown_setup_item), self, 0);
    g_signal_connect_object (factory, "bind",
                             G_CALLBACK (extension_dropdown_bind), self, 0);
    g_signal_connect_object (factory, "unbind",
                             G_CALLBACK (extension_dropdown_unbind), self, 0);

    list_factory = gtk_signal_list_item_factory_new ();
    g_signal_connect_object (list_factory, "setup",
                             G_CALLBACK (extension_dropdown_setup_item_full), self, 0);
    g_signal_connect_object (list_factory, "bind",
                             G_CALLBACK (extension_dropdown_bind), self, 0);
    g_signal_connect_object (list_factory, "unbind",
                             G_CALLBACK (extension_dropdown_unbind), self, 0);

    gtk_drop_down_set_factory (GTK_DROP_DOWN (self->extension_dropdown), factory);
    gtk_drop_down_set_list_factory (GTK_DROP_DOWN (self->extension_dropdown), list_factory);
    gtk_drop_down_set_model (GTK_DROP_DOWN (self->extension_dropdown), G_LIST_MODEL (store));

    format = g_settings_get_enum (nautilus_compression_preferences,
                                  NAUTILUS_PREFERENCES_DEFAULT_COMPRESSION_FORMAT);
    for (guint i = 0; i < g_list_model_get_n_items (G_LIST_MODEL (store)); i++)
    {
        item = g_list_model_get_item (G_LIST_MODEL (store), i);
        if (item->format == format)
        {
            gtk_drop_down_set_selected (GTK_DROP_DOWN (self->extension_dropdown), i);
            update_selected_format (self);
            g_object_unref (item);
            break;
        }

        g_object_unref (item);
    }

    g_object_unref (store);
    g_object_unref (factory);
    g_object_unref (list_factory);
}

static void
on_name_accepted (NautilusCompressDialog *self)
{
    g_autofree char *name = nautilus_filename_validator_get_new_name (self->validator);

    self->callback (name, self->passphrase, self->callback_data);

    gtk_window_close (GTK_WINDOW (self));
}

NautilusCompressDialog *
nautilus_compress_dialog_new (GtkWindow         *parent_window,
                              NautilusDirectory *destination_directory,
                              const char        *initial_name,
                              CompressCallback   callback,
                              gpointer           callback_data)
{
    NautilusCompressDialog *self = g_object_new (NAUTILUS_TYPE_COMPRESS_DIALOG,
                                                 "transient-for", parent_window,
                                                 NULL);

    nautilus_filename_validator_set_containing_directory (self->validator,
                                                          destination_directory);

    self->callback = callback;
    self->callback_data = callback_data;

    if (initial_name != NULL)
    {
        gtk_editable_set_text (GTK_EDITABLE (self->name_entry), initial_name);
    }
    gtk_widget_grab_focus (self->name_entry);

    gtk_window_present (GTK_WINDOW (self));

    return self;
}

static void
nautilus_compress_dialog_init (NautilusCompressDialog *self)
{
    g_type_ensure (NAUTILUS_TYPE_FILENAME_VALIDATOR);
    gtk_widget_init_template (GTK_WIDGET (self));

    extension_dropdown_setup (self);
    g_signal_connect (self->activate_button, "notify::sensitive",
                      G_CALLBACK (activate_button_on_sensitive_notify), self);
}

static void
nautilus_compress_dialog_dispose (GObject *object)
{
    gtk_widget_dispose_template (GTK_WIDGET (object), NAUTILUS_TYPE_COMPRESS_DIALOG);

    G_OBJECT_CLASS (nautilus_compress_dialog_parent_class)->dispose (object);
}

static void
nautilus_compress_dialog_finalize (GObject *object)
{
    NautilusCompressDialog *self = NAUTILUS_COMPRESS_DIALOG (object);

    g_free (self->passphrase);

    G_OBJECT_CLASS (nautilus_compress_dialog_parent_class)->finalize (object);
}

static void
nautilus_compress_dialog_class_init (NautilusCompressDialogClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

    object_class->finalize = nautilus_compress_dialog_finalize;
    object_class->dispose = nautilus_compress_dialog_dispose;

    gtk_widget_class_set_template_from_resource (widget_class,
                                                 "/org/gnome/nautilus/ui/nautilus-compress-dialog.ui");

    gtk_widget_class_bind_template_child (widget_class, NautilusCompressDialog, activate_button);
    gtk_widget_class_bind_template_child (widget_class, NautilusCompressDialog, error_label);
    gtk_widget_class_bind_template_child (widget_class, NautilusCompressDialog, extension_dropdown);
    gtk_widget_class_bind_template_child (widget_class, NautilusCompressDialog, extension_sizegroup);
    gtk_widget_class_bind_template_child (widget_class, NautilusCompressDialog, name_entry);
    gtk_widget_class_bind_template_child (widget_class, NautilusCompressDialog, passphrase_confirm_entry);
    gtk_widget_class_bind_template_child (widget_class, NautilusCompressDialog, passphrase_confirm_label);
    gtk_widget_class_bind_template_child (widget_class, NautilusCompressDialog, passphrase_entry);
    gtk_widget_class_bind_template_child (widget_class, NautilusCompressDialog, passphrase_label);
    gtk_widget_class_bind_template_child (widget_class, NautilusCompressDialog, validator);

    gtk_widget_class_bind_template_callback (widget_class, passphrase_entry_on_changed);
    gtk_widget_class_bind_template_callback (widget_class, passphrase_confirm_entry_on_changed);
    gtk_widget_class_bind_template_callback (widget_class, update_selected_format);
    gtk_widget_class_bind_template_callback (widget_class, on_name_accepted);
}
