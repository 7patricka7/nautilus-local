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
#include "nautilus-multiple-conflict-dialog.h"

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
