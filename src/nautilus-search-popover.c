/* nautilus-search-popover.c
 *
 * Copyright (C) 2015 Georges Basile Stavracas Neto <georges.stavracas@gmail.com>
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

#include <gtk/gtk.h>
#include <adwaita.h>

#include "nautilus-enum-types.h"
#include "nautilus-search-popover.h"
#include "nautilus-mime-actions.h"

#include <glib/gi18n.h>
#include "nautilus-file.h"
#include "nautilus-icon-info.h"
#include "nautilus-minimal-cell.h"
#include "nautilus-ui-utilities.h"
#include "nautilus-global-preferences.h"

 #define SEARCH_FILTER_MAX_YEARS 5

typedef struct
{
    GtkWidget *date_from_row;
    GtkWidget *date_to_row;
    GtkWidget *select_button;
} DateDialog;

struct _NautilusSearchPopover
{
    GtkPopover parent;

    AdwDialog *type_dialog;
    GtkStack *type_dialog_stack;

    NautilusQuery *query;

    GtkWidget *other_types_button;
    GtkWidget *audio_button;
    GtkWidget *documents_button;
    GtkWidget *folders_button;
    GtkWidget *images_button;
    GtkWidget *text_button;
    GtkWidget *spreadsheets_button;
    GtkWidget *pdf_button;
    GtkWidget *videos_button;
    GtkWidget *file_activated_button;

    GtkWidget *calendar_button;
    GtkWidget *today_button;
    GtkWidget *yesterday_button;
    GtkWidget *seven_days_button;
    GtkWidget *fourteen_days_button;
    GtkWidget *thirty_days_button;
    GtkWidget *ninety_days_button;
    GtkWidget *date_activated_button;

    GtkWidget *filename_search_button;
    GtkWidget *full_text_search_button;
    gboolean fts_enabled;
    gboolean fts_sensitive;

    GtkSingleSelection *other_types_model;
};

static void          show_other_types_dialog (NautilusSearchPopover *popover);

G_DEFINE_TYPE (NautilusSearchPopover, nautilus_search_popover, GTK_TYPE_POPOVER)

enum
{
    PROP_0,
    PROP_QUERY,
    PROP_FTS_ENABLED,
    LAST_PROP
};

enum
{
    MIME_TYPE,
    TIME_TYPE,
    DATE_RANGE,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

static void
date_button_clicked (GtkButton *button,
                     gpointer   user_data)
{
    NautilusSearchPopover *popover;
    popover = NAUTILUS_SEARCH_POPOVER (user_data);

    gtk_widget_set_sensitive (GTK_WIDGET (button), FALSE);
    if (popover->date_activated_button != NULL)
    {
        gtk_widget_set_sensitive (GTK_WIDGET (popover->date_activated_button), TRUE);
    }
    popover->date_activated_button = GTK_WIDGET (button);

    GDateTime *date;
    GDateTime *now;
    GPtrArray *date_range = NULL;
    const gchar *button_name;

    now = g_date_time_new_now_local ();
    date = g_object_get_data (G_OBJECT (button), "date");
    button_name = gtk_button_get_label (button);
    g_autofree GString *cleaned_name = g_string_new (button_name);
    g_string_replace (cleaned_name, "_", "", 0);
    const char *label = cleaned_name->str;

    if (date)
    {
        date_range = g_ptr_array_new_full (2, (GDestroyNotify) g_date_time_unref);
        g_ptr_array_add (date_range, g_date_time_ref (date));
        g_ptr_array_add (date_range, g_date_time_ref (now));
    }
    g_signal_emit_by_name (popover, "date-range", date_range, label);
    if (date_range)
    {
        g_ptr_array_unref (date_range);
    }
    g_date_time_unref (now);
}

static void
on_other_types_dialog_response (NautilusSearchPopover *popover)
{
    NautilusMinimalCell *item = gtk_single_selection_get_selected_item (popover->other_types_model);
    const gchar *mimetype = nautilus_minimal_cell_get_subtitle (item);

    g_signal_emit_by_name (popover, "mime-type", -1, NULL, mimetype);

    g_clear_object (&popover->other_types_model);

    adw_dialog_close (popover->type_dialog);
}

static gchar *
join_type_and_description (NautilusMinimalCell *cell)
{
    const gchar *description = nautilus_minimal_cell_get_title (cell);
    const gchar *content_type = nautilus_minimal_cell_get_subtitle (cell);

    return g_strdup_printf ("%s %s", content_type, description);
}

static void
file_type_search_changed (GtkEditable           *search_entry,
                          GParamSpec            *pspec,
                          NautilusSearchPopover *self)
{
    const gchar *string = gtk_editable_get_text (search_entry);

    if (string == NULL || *string == '\0')
    {
        gtk_stack_set_visible_child_name (self->type_dialog_stack, "start");
        gtk_widget_set_sensitive (GTK_WIDGET (adw_dialog_get_default_widget (self->type_dialog)),
                                  FALSE);

        return;
    }

    guint result_count = g_list_model_get_n_items (G_LIST_MODEL (self->other_types_model));

    if (result_count == 0)
    {
        gtk_stack_set_visible_child_name (self->type_dialog_stack, "empty");
        gtk_widget_set_sensitive (GTK_WIDGET (adw_dialog_get_default_widget (self->type_dialog)),
                                  FALSE);
    }
    else
    {
        gtk_stack_set_visible_child_name (self->type_dialog_stack, "results");
        gtk_widget_set_sensitive (GTK_WIDGET (adw_dialog_get_default_widget (self->type_dialog)),
                                  TRUE);
    }
}

static gboolean
click_policy_mapping_get (GValue   *gvalue,
                          GVariant *variant,
                          gpointer  listview)
{
    int click_policy = g_settings_get_enum (nautilus_preferences,
                                            NAUTILUS_PREFERENCES_CLICK_POLICY);

    g_value_set_boolean (gvalue, click_policy == NAUTILUS_CLICK_POLICY_SINGLE);

    return TRUE;
}

static void
show_other_types_dialog (NautilusSearchPopover *popover)
{
    GtkStringFilter *filter;
    GtkFilterListModel *filter_model;
    g_autoptr (GList) mime_infos = NULL;
    GListStore *file_type_list = g_list_store_new (NAUTILUS_TYPE_MINIMAL_CELL);
    g_autoptr (GtkBuilder) builder = NULL;
    AdwToolbarView *toolbar_view;
    GtkWidget *content_area;
    GtkWidget *search_entry;
    GtkListView *listview;
    GtkRoot *toplevel = gtk_widget_get_root (GTK_WIDGET (popover));

    gtk_popover_popdown (GTK_POPOVER (popover));

    mime_infos = g_content_types_get_registered ();
    mime_infos = g_list_sort (mime_infos, (GCompareFunc) g_strcmp0);
    gint scale = gtk_widget_get_scale_factor (GTK_WIDGET (toplevel));
    for (GList *l = mime_infos; l != NULL; l = l->next)
    {
        g_autofree gchar *content_type = l->data;
        g_autofree gchar *description = g_content_type_get_description (content_type);
        g_autoptr (GIcon) icon = g_content_type_get_icon (content_type);
        g_autoptr (NautilusIconInfo) icon_info = nautilus_icon_info_lookup (icon, 32, scale);
        GdkPaintable *paintable = nautilus_icon_info_get_paintable (icon_info);

        g_list_store_append (file_type_list, nautilus_minimal_cell_new (description,
                                                                        content_type,
                                                                        GDK_PAINTABLE (paintable)));
    }

    filter = gtk_string_filter_new (gtk_cclosure_expression_new (G_TYPE_STRING,
                                                                 NULL, 0, NULL,
                                                                 G_CALLBACK (join_type_and_description),
                                                                 NULL, NULL));
    filter_model = gtk_filter_list_model_new (G_LIST_MODEL (file_type_list), GTK_FILTER (filter));
    popover->other_types_model = gtk_single_selection_new (G_LIST_MODEL (filter_model));

    builder = gtk_builder_new_from_resource ("/org/gnome/nautilus/ui/nautilus-search-types-dialog.ui");

    popover->type_dialog = ADW_DIALOG (gtk_builder_get_object (builder, "file_types_dialog"));
    search_entry = GTK_WIDGET (gtk_builder_get_object (builder, "search_entry"));
    toolbar_view = ADW_TOOLBAR_VIEW (gtk_builder_get_object (builder, "toolbar_view"));
    popover->type_dialog_stack = GTK_STACK (gtk_builder_get_object (builder, "search_stack"));
    listview = GTK_LIST_VIEW (gtk_builder_get_object (builder, "listview"));

    content_area = adw_toolbar_view_get_content (toolbar_view);
    gtk_search_entry_set_key_capture_widget (GTK_SEARCH_ENTRY (search_entry), content_area);
    g_object_bind_property (search_entry, "text", filter, "search", G_BINDING_SYNC_CREATE);
    g_signal_connect_after (search_entry, "notify::text",
                            G_CALLBACK (file_type_search_changed), popover);

    gtk_list_view_set_model (listview,
                             GTK_SELECTION_MODEL (g_object_ref (popover->other_types_model)));
    g_settings_bind_with_mapping (nautilus_preferences, NAUTILUS_PREFERENCES_CLICK_POLICY,
                                  listview, "single-click-activate", G_SETTINGS_BIND_GET,
                                  click_policy_mapping_get, NULL, listview, NULL);

    g_signal_connect_swapped (adw_dialog_get_default_widget (popover->type_dialog), "clicked",
                              G_CALLBACK (on_other_types_dialog_response), popover);
    g_signal_connect_swapped (popover->type_dialog, "close-attempt",
                              G_CALLBACK (on_other_types_dialog_response), popover);
    g_signal_connect_swapped (listview, "activate",
                              G_CALLBACK (on_other_types_dialog_response), popover);

    adw_dialog_present (popover->type_dialog, GTK_WIDGET (toplevel));
}

static void
file_types_button_clicked (GtkButton *button,
                           gpointer   user_data)
{
    NautilusSearchPopover *popover = NAUTILUS_SEARCH_POPOVER (user_data);

    gint group = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (button), "mimetype-group"));

    if (group == -1)
    {
        if (popover->file_activated_button != NULL)
        {
            gtk_widget_set_sensitive (GTK_WIDGET (popover->file_activated_button), TRUE);
        }
        popover->file_activated_button = NULL;
        show_other_types_dialog (popover);
    }
    else
    {
        gtk_widget_set_sensitive (GTK_WIDGET (button), FALSE);
        if (popover->file_activated_button != NULL)
        {
            gtk_widget_set_sensitive (GTK_WIDGET (popover->file_activated_button), TRUE);
        }
        popover->file_activated_button = GTK_WIDGET (button);

        const gchar *button_name = adw_button_content_get_label (ADW_BUTTON_CONTENT (gtk_button_get_child (button)));
        g_autofree GString *cleaned_name = g_string_new (button_name);
        g_string_replace (cleaned_name, "_", "", 0);
        const char *label = cleaned_name->str;
        g_signal_emit_by_name (popover, "mime-type", group, label, NULL);
    }
}

static void
filename_search_button_clicked (GtkButton *button,
                                gpointer   user_data)
{
    NautilusSearchPopover *popover;
    popover = NAUTILUS_SEARCH_POPOVER (user_data);

    gtk_widget_set_sensitive (GTK_WIDGET (button), FALSE);
    gtk_widget_set_sensitive (GTK_WIDGET (popover->full_text_search_button), TRUE);

    popover->fts_enabled = FALSE;
    g_settings_set_boolean (nautilus_preferences, NAUTILUS_PREFERENCES_FTS_ENABLED, FALSE);
    g_object_notify (G_OBJECT (popover), "fts-enabled");
}

static void
full_text_search_button_clicked (GtkButton *button,
                                 gpointer   user_data)
{
    NautilusSearchPopover *popover;
    popover = NAUTILUS_SEARCH_POPOVER (user_data);

    gtk_widget_set_sensitive (GTK_WIDGET (button), FALSE);
    gtk_widget_set_sensitive (GTK_WIDGET (popover->filename_search_button), TRUE);

    popover->fts_enabled = TRUE;
    g_settings_set_boolean (nautilus_preferences, NAUTILUS_PREFERENCES_FTS_ENABLED, TRUE);
    g_object_notify (G_OBJECT (popover), "fts-enabled");
}

void
nautilus_search_popover_set_fts_sensitive (NautilusSearchPopover *popover,
                                           gboolean               sensitive)
{
    popover->fts_sensitive = sensitive;
    gtk_widget_set_sensitive (popover->full_text_search_button, sensitive & !popover->fts_enabled);
    gtk_widget_set_sensitive (popover->filename_search_button, sensitive & popover->fts_enabled);
    g_object_notify (G_OBJECT (popover), "fts-enabled");
}

static void
nautilus_search_popover_get_property (GObject    *object,
                                      guint       prop_id,
                                      GValue     *value,
                                      GParamSpec *pspec)
{
    NautilusSearchPopover *self;

    self = NAUTILUS_SEARCH_POPOVER (object);

    switch (prop_id)
    {
        case PROP_QUERY:
        {
            g_value_set_object (value, self->query);
        }
        break;

        case PROP_FTS_ENABLED:
        {
            g_value_set_boolean (value, self->fts_enabled);
        }
        break;

        default:
        {
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        }
    }
}

static void
nautilus_search_popover_set_property (GObject      *object,
                                      guint         prop_id,
                                      const GValue *value,
                                      GParamSpec   *pspec)
{
    NautilusSearchPopover *self;

    self = NAUTILUS_SEARCH_POPOVER (object);

    switch (prop_id)
    {
        case PROP_QUERY:
        {
            nautilus_search_popover_set_query (self, g_value_get_object (value));
        }
        break;

        case PROP_FTS_ENABLED:
        {
            self->fts_enabled = g_value_get_boolean (value);
        }
        break;

        default:
        {
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        }
    }
}

static void
nautilus_search_popover_dispose (GObject *obj)
{
    NautilusSearchPopover *self = NAUTILUS_SEARCH_POPOVER (obj);

    gtk_popover_set_child (GTK_POPOVER (obj), NULL);
    gtk_widget_dispose_template (GTK_WIDGET (self), NAUTILUS_TYPE_SEARCH_POPOVER);

    G_OBJECT_CLASS (nautilus_search_popover_parent_class)->dispose (obj);
}

static void
nautilus_search_popover_class_init (NautilusSearchPopoverClass *klass)
{
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->get_property = nautilus_search_popover_get_property;
    object_class->set_property = nautilus_search_popover_set_property;
    object_class->dispose = nautilus_search_popover_dispose;

    signals[DATE_RANGE] = g_signal_new ("date-range",
                                        NAUTILUS_TYPE_SEARCH_POPOVER,
                                        G_SIGNAL_RUN_LAST,
                                        0,
                                        NULL,
                                        NULL,
                                        g_cclosure_marshal_generic,
                                        G_TYPE_NONE,
                                        2,
                                        G_TYPE_PTR_ARRAY,
                                        G_TYPE_STRING);

    signals[MIME_TYPE] = g_signal_new ("mime-type",
                                       NAUTILUS_TYPE_SEARCH_POPOVER,
                                       G_SIGNAL_RUN_LAST,
                                       0,
                                       NULL,
                                       NULL,
                                       g_cclosure_marshal_generic,
                                       G_TYPE_NONE,
                                       3,
                                       G_TYPE_INT,
                                       G_TYPE_STRING,
                                       G_TYPE_STRING);

    /**
     * NautilusSearchPopover::query:
     *
     * The current #NautilusQuery being edited.
     */
    g_object_class_install_property (object_class,
                                     PROP_QUERY,
                                     g_param_spec_object ("query",
                                                          "Query of the popover",
                                                          "The current query being edited",
                                                          NAUTILUS_TYPE_QUERY,
                                                          G_PARAM_READWRITE));

    g_object_class_install_property (object_class,
                                     PROP_FTS_ENABLED,
                                     g_param_spec_boolean ("fts-enabled",
                                                           "fts enabled",
                                                           "fts enabled",
                                                           FALSE,
                                                           G_PARAM_READWRITE));

    gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/nautilus/ui/nautilus-search-popover.ui");

    gtk_widget_class_bind_template_child (widget_class, NautilusSearchPopover, other_types_button);
    gtk_widget_class_bind_template_child (widget_class, NautilusSearchPopover, audio_button);
    gtk_widget_class_bind_template_child (widget_class, NautilusSearchPopover, documents_button);
    gtk_widget_class_bind_template_child (widget_class, NautilusSearchPopover, folders_button);
    gtk_widget_class_bind_template_child (widget_class, NautilusSearchPopover, images_button);
    gtk_widget_class_bind_template_child (widget_class, NautilusSearchPopover, text_button);
    gtk_widget_class_bind_template_child (widget_class, NautilusSearchPopover, spreadsheets_button);
    gtk_widget_class_bind_template_child (widget_class, NautilusSearchPopover, pdf_button);
    gtk_widget_class_bind_template_child (widget_class, NautilusSearchPopover, videos_button);
    gtk_widget_class_bind_template_child (widget_class, NautilusSearchPopover, calendar_button);

    gtk_widget_class_bind_template_child (widget_class, NautilusSearchPopover, today_button);
    gtk_widget_class_bind_template_child (widget_class, NautilusSearchPopover, yesterday_button);
    gtk_widget_class_bind_template_child (widget_class, NautilusSearchPopover, seven_days_button);
    gtk_widget_class_bind_template_child (widget_class, NautilusSearchPopover, fourteen_days_button);
    gtk_widget_class_bind_template_child (widget_class, NautilusSearchPopover, thirty_days_button);
    gtk_widget_class_bind_template_child (widget_class, NautilusSearchPopover, ninety_days_button);

    gtk_widget_class_bind_template_child (widget_class, NautilusSearchPopover, filename_search_button);
    gtk_widget_class_bind_template_child (widget_class, NautilusSearchPopover, full_text_search_button);

    gtk_widget_class_bind_template_callback (widget_class, date_button_clicked);
    gtk_widget_class_bind_template_callback (widget_class, file_types_button_clicked);
    gtk_widget_class_bind_template_callback (widget_class, filename_search_button_clicked);
    gtk_widget_class_bind_template_callback (widget_class, full_text_search_button_clicked);
}

static void
nautilus_search_popover_init (NautilusSearchPopover *self)
{
    gtk_widget_init_template (GTK_WIDGET (self));

    self->fts_enabled = g_settings_get_boolean (nautilus_preferences,
                                                NAUTILUS_PREFERENCES_FTS_ENABLED);

    self->file_activated_button = NULL;
    self->date_activated_button = NULL;

    g_object_set_data (G_OBJECT (self->audio_button), "mimetype-group", GINT_TO_POINTER (5));
    g_object_set_data (G_OBJECT (self->documents_button), "mimetype-group", GINT_TO_POINTER (3));
    g_object_set_data (G_OBJECT (self->folders_button), "mimetype-group", GINT_TO_POINTER (2));
    g_object_set_data (G_OBJECT (self->images_button), "mimetype-group", GINT_TO_POINTER (7));
    g_object_set_data (G_OBJECT (self->text_button), "mimetype-group", GINT_TO_POINTER (10));
    g_object_set_data (G_OBJECT (self->spreadsheets_button), "mimetype-group", GINT_TO_POINTER (9));
    g_object_set_data (G_OBJECT (self->pdf_button), "mimetype-group", GINT_TO_POINTER (6));
    g_object_set_data (G_OBJECT (self->videos_button), "mimetype-group", GINT_TO_POINTER (11));
    g_object_set_data (G_OBJECT (self->other_types_button), "mimetype-group", GINT_TO_POINTER (-1));

    GDateTime *now, *now_start;
    gint year, month, day;

    now = g_date_time_new_now_local ();
    year = g_date_time_get_year (now);
    month = g_date_time_get_month (now);
    day = g_date_time_get_day_of_month (now);
    now_start = g_date_time_new_local (year, month, day, 0, 0, 0);

    g_object_set_data_full (G_OBJECT (self->today_button),
                            "date",
                            g_date_time_ref (now_start),
                            (GDestroyNotify) g_date_time_unref);
    g_object_set_data_full (G_OBJECT (self->yesterday_button),
                            "date",
                            g_date_time_ref (g_date_time_add_days (now, -1)),
                            (GDestroyNotify) g_date_time_unref);
    g_object_set_data_full (G_OBJECT (self->seven_days_button),
                            "date",
                            g_date_time_ref (g_date_time_add_days (now, -7)),
                            (GDestroyNotify) g_date_time_unref);
    g_object_set_data_full (G_OBJECT (self->fourteen_days_button),
                            "date",
                            g_date_time_ref (g_date_time_add_days (now, -14)),
                            (GDestroyNotify) g_date_time_unref);
    g_object_set_data_full (G_OBJECT (self->thirty_days_button),
                            "date",
                            g_date_time_ref (g_date_time_add_days (now, -30)),
                            (GDestroyNotify) g_date_time_unref);
    g_object_set_data_full (G_OBJECT (self->ninety_days_button),
                            "date",
                            g_date_time_ref (g_date_time_add_days (now, -90)),
                            (GDestroyNotify) g_date_time_unref);

    g_date_time_unref (now);
    g_date_time_unref (now_start);

    /* if fts is enabled, then don't show the fts button as it is already
     * selected and part of the query */
    gtk_widget_set_sensitive (self->full_text_search_button, !self->fts_enabled);
    gtk_widget_set_sensitive (self->filename_search_button, self->fts_enabled);
}

GtkWidget *
nautilus_search_popover_new (void)
{
    return g_object_new (NAUTILUS_TYPE_SEARCH_POPOVER, NULL);
}

/**
 * nautilus_search_popover_get_query:
 * @popover: a #NautilusSearchPopover
 *
 * Gets the current query for @popover.
 *
 * Returns: (transfer none): the current #NautilusQuery from @popover.
 */
NautilusQuery *
nautilus_search_popover_get_query (NautilusSearchPopover *popover)
{
    g_return_val_if_fail (NAUTILUS_IS_SEARCH_POPOVER (popover), NULL);

    return popover->query;
}

/**
 * nautilus_search_popover_set_query:
 * @popover: a #NautilusSearchPopover
 * @query (nullable): a #NautilusQuery
 *
 * Sets the current query for @popover.
 *
 * Returns:
 */
void
nautilus_search_popover_set_query (NautilusSearchPopover *popover,
                                   NautilusQuery         *query)
{
    g_return_if_fail (NAUTILUS_IS_SEARCH_POPOVER (popover));

    if (popover->query != query)
    {
        g_set_object (&popover->query, query);

        nautilus_search_popover_reset_mime_types (popover);
        nautilus_search_popover_reset_date_range (popover);
    }
}

void
nautilus_search_popover_reset_mime_types (NautilusSearchPopover *popover)
{
    g_return_if_fail (NAUTILUS_IS_SEARCH_POPOVER (popover));

    if (popover->file_activated_button != NULL)
    {
        gtk_widget_set_sensitive (popover->file_activated_button, TRUE);
    }
    popover->file_activated_button = NULL;
    g_signal_emit_by_name (popover, "mime-type", 0, NULL, NULL);
}


void
nautilus_search_popover_reset_date_range (NautilusSearchPopover *popover)
{
    g_return_if_fail (NAUTILUS_IS_SEARCH_POPOVER (popover));

    if (popover->date_activated_button != NULL)
    {
        gtk_widget_set_sensitive (popover->date_activated_button, TRUE);
    }
    popover->date_activated_button = NULL;
    g_signal_emit_by_name (popover, "date-range", NULL);
}

gboolean
nautilus_search_popover_get_fts_enabled (NautilusSearchPopover *popover)
{
    return popover->fts_enabled;
}

gboolean
nautilus_search_popover_get_fts_sensitive (NautilusSearchPopover *popover)
{
    return popover->fts_sensitive;
}
