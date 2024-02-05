/*
 * Copyright (C) 2023 The GNOME project contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "nautilus-dialog-utilities.h"

#include "nautilus-error-reporting.h"
#include "nautilus-operations-ui-manager.h"

#include <adwaita.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>


static const char *CANCEL = "cancel";
static const char *COPY_FORCE = "copy_force";
static const char *DELETE = "delete";
static const char *DELETE_ALL = "delete_all";
static const char *EMPTY_TRASH = "empty_trash";
static const char *MERGE = "merge";
static const char *PROCEED = "proceed";
static const char *REPLACE = "replace";
static const char *RETRY = "retry";
static const char *SKIP = "skip";
static const char *SKIP_ALL = "skip_all";
static const char *SKIP_FILES = "skip_files";


NautilusDialogResponse
nautilus_dialog_response_from_string (const char *response)
{
    if (g_str_equal (response, CANCEL))
    {
        return RESPONSE_CANCEL;
    }
    else if (g_str_equal (response, COPY_FORCE))
    {
        return RESPONSE_COPY_FORCE;
    }
    else if (g_str_equal (response, DELETE))
    {
        return RESPONSE_DELETE;
    }
    else if (g_str_equal (response, DELETE_ALL))
    {
        return RESPONSE_DELETE_ALL;
    }
    else if (g_str_equal (response, EMPTY_TRASH))
    {
        return RESPONSE_EMPTY_TRASH;
    }
    else if (g_str_equal (response, MERGE))
    {
        return RESPONSE_MERGE;
    }
    else if (g_str_equal (response, PROCEED))
    {
        return RESPONSE_PROCEED;
    }
    else if (g_str_equal (response, REPLACE))
    {
        return RESPONSE_REPLACE;
    }
    else if (g_str_equal (response, RETRY))
    {
        return RESPONSE_RETRY;
    }
    else if (g_str_equal (response, SKIP))
    {
        return RESPONSE_SKIP;
    }
    else if (g_str_equal (response, SKIP_ALL))
    {
        return RESPONSE_SKIP_ALL;
    }
    else if (g_str_equal (response, SKIP_FILES))
    {
        return RESPONSE_SKIP_FILES;
    }
    else
    {
        /* Treat unknown responses as cancel */
        return RESPONSE_CANCEL;
    }
}

static void
add_dialog_responses (AdwMessageDialog       *dialog,
                      NautilusDialogResponse  response)
{
    /* Cancel response is always available and the default response */
    adw_message_dialog_add_response (dialog, CANCEL, _("_Cancel"));
    adw_message_dialog_set_default_response (dialog, CANCEL);
    adw_message_dialog_set_close_response (ADW_MESSAGE_DIALOG (dialog), CANCEL);

    /* The order here affects the order the dialog shows the responses. */
    if (response & RESPONSE_COPY_FORCE)
    {
        adw_message_dialog_add_response (dialog, COPY_FORCE, _("Copy _Anyway"));
    }
    if (response & RESPONSE_PROCEED)
    {
        adw_message_dialog_add_response (dialog, PROCEED, _("Proceed _Anyway"));
    }
    if (response & RESPONSE_EMPTY_TRASH)
    {
        adw_message_dialog_add_response (dialog, EMPTY_TRASH, _("Empty _Trash"));
        adw_message_dialog_set_response_appearance (dialog, EMPTY_TRASH, ADW_RESPONSE_DESTRUCTIVE);
    }
    if (response & RESPONSE_MERGE)
    {
        adw_message_dialog_add_response (dialog, MERGE, _("_Merge"));
    }
    if (response & RESPONSE_REPLACE)
    {
        adw_message_dialog_add_response (dialog, REPLACE, _("_Replace"));
    }
    if (response & RESPONSE_SKIP_ALL)
    {
        adw_message_dialog_add_response (dialog, SKIP_ALL, _("S_kip All"));
    }
    if (response & RESPONSE_SKIP)
    {
        adw_message_dialog_add_response (dialog, SKIP, _("_Skip"));
    }
    if (response & RESPONSE_SKIP_FILES)
    {
        adw_message_dialog_add_response (dialog, SKIP_FILES, _("_Skip Files"));
    }
    if (response & RESPONSE_DELETE_ALL)
    {
        adw_message_dialog_add_response (dialog, DELETE_ALL, _("Delete _All"));
        adw_message_dialog_set_response_appearance (dialog, DELETE_ALL, ADW_RESPONSE_DESTRUCTIVE);
    }
    if (response & RESPONSE_DELETE)
    {
        adw_message_dialog_add_response (dialog, DELETE, _("_Delete"));
        adw_message_dialog_set_response_appearance (dialog, DELETE, ADW_RESPONSE_DESTRUCTIVE);
    }
    if (response & RESPONSE_RETRY)
    {
        adw_message_dialog_add_response (dialog, RETRY, _("_Retry"));
    }
}

static void
enable_interactivity (gpointer widget)
{
    gtk_widget_set_sensitive (GTK_WIDGET (widget), TRUE);
}

GtkWidget *
nautilus_dialog_with_responses (GtkWindow              *parent_window,
                                const char             *heading,
                                const char             *body,
                                const char             *details,
                                gboolean                delay_interactivity,
                                NautilusDialogResponse  responses)
{
    GtkWidget *dialog = adw_message_dialog_new (parent_window, heading, body);

    if (delay_interactivity)
    {
        gtk_widget_set_sensitive (dialog, FALSE);
        g_timeout_add_seconds_once (BUTTON_ACTIVATION_DELAY_IN_SECONDS,
                                    enable_interactivity,
                                    dialog);
    }

    if (details)
    {
        GtkLabel *label = GTK_LABEL (gtk_label_new (details));
        gtk_label_set_wrap (label, TRUE);
        gtk_label_set_selectable (label, TRUE);
        gtk_label_set_xalign (label, 0);
        /* Ideally, we shouldn’t do this.
         *
         * Refer to https://gitlab.gnome.org/GNOME/nautilus/merge_requests/94
         * and https://gitlab.gnome.org/GNOME/nautilus/issues/270.
         */
        gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_MIDDLE);
        gtk_label_set_max_width_chars (label, MAXIMUM_DISPLAYED_ERROR_MESSAGE_LENGTH);

        adw_message_dialog_set_extra_child (ADW_MESSAGE_DIALOG (dialog), GTK_WIDGET (label));
    }

    add_dialog_responses (ADW_MESSAGE_DIALOG (dialog), responses);

    return dialog;
}
