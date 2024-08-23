/*
 * nautilus-multiple-conflict-dialog.c
 *
 * Copyright 2024 Anuraag Reddy Patllollu <>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "nautilus-file-conflict-dialog.h"
#include "nautilus-file-operations.h"
#include "nautilus-multiple-conflict-dialog.h"
#include "nautilus-operations-ui-manager.h"

struct _NautilusMultipleConflictDialog
{
    AdwDialog parent_instance;

    ConflictResponse dialog_response;

    GtkWidget *replacement_list_box;
    GtkWidget *conflict_number_label;
    GtkWidget *cancel_button;
    GtkWidget *replace_button;
};

G_DEFINE_TYPE (NautilusMultipleConflictDialog, nautilus_multiple_conflict_dialog, ADW_TYPE_DIALOG);

static void
on_check_button_toggle (GtkCheckButton *check_button,
                        gpointer        user_data)
{
    GList *conflict = (GList *) user_data;
    FileData *file_data = (FileData *) conflict->data;

    if (gtk_check_button_get_active (check_button))
    {
        file_data->response->id = CONFLICT_RESPONSE_REPLACE;
    }
    else
    {
        file_data->response->id = CONFLICT_RESPONSE_SKIP;
    }
}

void
nautilus_multiple_conflict_dialog_set_conflict_rows (NautilusMultipleConflictDialog *self,
                                                     GList                          *conflicts,
                                                     GList                          *dest_names,
                                                     GList                          *dest_dates,
                                                     GList                          *src_dates)
{
    GtkWidget *row, *check_button;
    GList *conflict, *dest_name, *dest_date, *src_date;
    guint num_conflicts;

    num_conflicts = g_list_length (dest_names);
    g_autofree gchar *label = g_strdup_printf (_("%d files that are being pasted have the same "
                                                 "name as files that are already present"), num_conflicts);

    gtk_label_set_text (GTK_LABEL (self->conflict_number_label), label);

    for (conflict = conflicts, dest_name = dest_names, dest_date = dest_dates, src_date = src_dates;
         conflict != NULL && dest_name != NULL && dest_date != NULL && src_date != NULL;
         conflict = conflict->next, dest_name = dest_name->next, dest_date = dest_date->next, src_date = src_date->next)
    {
        row = adw_action_row_new ();
        adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row), (const gchar *) dest_name->data);

        g_autofree gchar *subtitle = g_strdup_printf ("Modified %s 🡢 %s",
                                                      (char *) src_date->data,
                                                      (char *) dest_date->data);
        adw_action_row_set_subtitle (ADW_ACTION_ROW (row), subtitle);

        check_button = gtk_check_button_new ();
        gtk_check_button_set_active (GTK_CHECK_BUTTON (check_button), TRUE);
        adw_action_row_set_activatable_widget (ADW_ACTION_ROW (row), check_button);
        FileData *file_data = conflict->data;
        file_data->response->id = CONFLICT_RESPONSE_REPLACE;
        g_signal_connect (check_button,
                          "toggled",
                          G_CALLBACK (on_check_button_toggle),
                          conflict);

        adw_action_row_add_prefix (ADW_ACTION_ROW (row), check_button);

        gtk_list_box_append (GTK_LIST_BOX (self->replacement_list_box), row);
    }
}

static void
replace_button_clicked (GtkButton                      *button,
                        NautilusMultipleConflictDialog *dialog)
{
    dialog->dialog_response = CONFLICT_RESPONSE_REPLACE;
    adw_dialog_close (ADW_DIALOG (dialog));
}

static void
cancel_button_clicked (GtkButton                      *button,
                       NautilusMultipleConflictDialog *dialog)
{
    dialog->dialog_response = CONFLICT_RESPONSE_CANCEL;
    adw_dialog_close (ADW_DIALOG (dialog));
}

static void
nautilus_multiple_conflict_dialog_class_init (NautilusMultipleConflictDialogClass *klass)
{
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

    gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/nautilus/ui/nautilus-multiple-conflict-dialog.ui");

    gtk_widget_class_bind_template_child (widget_class, NautilusMultipleConflictDialog, replacement_list_box);
    gtk_widget_class_bind_template_child (widget_class, NautilusMultipleConflictDialog, conflict_number_label);
    gtk_widget_class_bind_template_child (widget_class, NautilusMultipleConflictDialog, replace_button);
    gtk_widget_class_bind_template_child (widget_class, NautilusMultipleConflictDialog, cancel_button);

    gtk_widget_class_bind_template_callback (widget_class, replace_button_clicked);
    gtk_widget_class_bind_template_callback (widget_class, cancel_button_clicked);
}

static void
nautilus_multiple_conflict_dialog_init (NautilusMultipleConflictDialog *self)
{
    gtk_widget_init_template (GTK_WIDGET (self));

    adw_dialog_set_can_close (ADW_DIALOG (self), FALSE);
}

NautilusMultipleConflictDialog *
nautilus_multiple_conflict_dialog_new (void)
{
    return NAUTILUS_MULTIPLE_CONFLICT_DIALOG (g_object_new (NAUTILUS_TYPE_MULTIPLE_CONFLICT_DIALOG,
                                                            NULL));
}
static gboolean
activate_buttons (NautilusMultipleConflictDialog *self)
{
    gtk_widget_set_sensitive (GTK_WIDGET (self->cancel_button), TRUE);
    gtk_widget_set_sensitive (GTK_WIDGET (self->replace_button), TRUE);
    return G_SOURCE_REMOVE;
}

void
nautilus_multiple_conflict_dialog_delay_buttons_activation (NautilusMultipleConflictDialog *self)
{
    gtk_widget_set_sensitive (self->cancel_button, FALSE);
    gtk_widget_set_sensitive (self->replace_button, FALSE);

    g_timeout_add_seconds (BUTTON_ACTIVATION_DELAY_IN_SECONDS,
                           G_SOURCE_FUNC (activate_buttons),
                           self);
}

ConflictResponse
nautilus_multiple_conflict_dialog_get_response (NautilusMultipleConflictDialog *dialog)
{
    return dialog->dialog_response;
}
