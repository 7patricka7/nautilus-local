/* nautilus-column-chooser.h - A column chooser widget
 *
 *  Copyright (C) 2004 Novell, Inc.
 *
 *  The Gnome Library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public License as
 *  published by the Free Software Foundation; either version 2 of the
 *  License, or (at your option) any later version.
 *
 *  The Gnome Library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Library General Public License for more details.
 *
 *  You should have received a copy of the GNU Library General Public
 *  License along with the Gnome Library; see the column COPYING.LIB.  If not,
 *  see <http://www.gnu.org/licenses/>.
 *
 *  Authors: Dave Camp <dave@ximian.com>
 *           Corey Berla <corey@berla.me>
 */

#include <config.h>
#include "nautilus-column-chooser.h"

#include <adwaita.h>
#include <string.h>
#include <gtk/gtk.h>
#include <glib/gi18n.h>

#include <nautilus-extension.h>

#include "nautilus-column-utilities.h"

struct _NautilusColumnChooser
{
    AdwWindow parent;

    GListModel *model;
    GtkWidget *list_box;
    GtkWidget *use_default_button;
    GtkWidget *window_title;

    NautilusColumn *drag_column;
    guint orig_drag_pos;

    NautilusFile *file;
};

enum
{
    PROP_FILE = 1,
    NUM_PROPERTIES
};

enum
{
    CHANGED,
    USE_DEFAULT,
    LAST_SIGNAL
};
static guint signals[LAST_SIGNAL];

G_DEFINE_TYPE (NautilusColumnChooser, nautilus_column_chooser, ADW_TYPE_WINDOW);

static GStrv
get_column_names (NautilusColumnChooser *chooser,
                  gboolean               only_visible)
{
    g_autoptr (GStrvBuilder) builder = g_strv_builder_new ();

    for (guint i = 0; i < g_list_model_get_n_items (chooser->model); i++)
    {
        g_autoptr (NautilusColumn) column = g_list_model_get_item (chooser->model, i);
        char *name;
        gboolean visible;

        g_object_get (column, "name", &name, "visible", &visible, NULL);

        if (!only_visible || visible)
        {
            g_strv_builder_add (builder, name);
        }
        else
        {
            g_free (name);
        }
    }

    return g_strv_builder_end (builder);
}

static void
list_changed (NautilusColumnChooser *chooser)
{
    g_auto (GStrv) column_order = get_column_names (chooser, FALSE);
    g_auto (GStrv) visible_columns = get_column_names (chooser, TRUE);

    g_signal_emit (chooser, signals[CHANGED], 0, column_order, visible_columns);
}

static void
nautilus_column_chooser_set_property (GObject      *object,
                                      guint         param_id,
                                      const GValue *value,
                                      GParamSpec   *pspec)
{
    NautilusColumnChooser *chooser;

    chooser = NAUTILUS_COLUMN_CHOOSER (object);

    switch (param_id)
    {
        case PROP_FILE:
        {
            chooser->file = g_value_get_object (value);
        }
        break;

        default:
        {
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
        }
        break;
    }
}

static void
notify_row_switch_cb (GObject    *object,
                      GParamSpec *pspec,
                      gpointer    user_data)
{
    NautilusColumnChooser *chooser = user_data;

    list_changed (chooser);
}

static GdkContentProvider *
on_row_drag_prepare (GtkDragSource *source,
                     gdouble        x,
                     gdouble        y,
                     gpointer       user_data)
{
    GtkWidget *widget = gtk_event_controller_get_widget (GTK_EVENT_CONTROLLER (source));
    g_autoptr (GdkPaintable) paintable = gtk_widget_paintable_new (widget);
    NautilusColumn *column = user_data;
    NautilusColumnChooser *chooser;

    chooser = NAUTILUS_COLUMN_CHOOSER (gtk_widget_get_ancestor (widget, NAUTILUS_TYPE_COLUMN_CHOOSER));
    if (!g_list_store_find (G_LIST_STORE (chooser->model), column, &chooser->orig_drag_pos))
    {
        return NULL;
    }

    chooser->drag_column = column;
    gtk_drag_source_set_icon (source, paintable, 0, 0);
    return gdk_content_provider_new_typed (NAUTILUS_TYPE_COLUMN, user_data);
}

static gboolean
on_row_drag_cancel (GtkDragSource       *source,
                    GdkDrag             *drag,
                    GdkDragCancelReason *reason,
                    gpointer             user_data)
{
    NautilusColumnChooser *chooser = user_data;
    guint pos;

    if (g_list_store_find (G_LIST_STORE (chooser->model), chooser->drag_column, &pos))
    {
        g_list_store_remove (G_LIST_STORE (chooser->model), pos);
        g_list_store_insert (G_LIST_STORE (chooser->model), chooser->orig_drag_pos, chooser->drag_column);
        list_changed (chooser);

        return FALSE;
    }

    return TRUE;
}

static GdkDragAction
on_row_drop_enter (GtkDropTarget *target,
                   gdouble        x,
                   gdouble        y,
                   gpointer       user_data)
{
    NautilusColumn *drop_column = user_data;
    NautilusColumn *drag_column;
    NautilusColumnChooser *chooser;
    GtkWidget *row = gtk_event_controller_get_widget (GTK_EVENT_CONTROLLER (target));
    const GValue *value = gtk_drop_target_get_value (target);
    guint orig_pos, dest_pos = 0;

    if (value == NULL || !G_VALUE_HOLDS (value, NAUTILUS_TYPE_COLUMN))
    {
        return 0;
    }

    chooser = NAUTILUS_COLUMN_CHOOSER (gtk_widget_get_ancestor (row, NAUTILUS_TYPE_COLUMN_CHOOSER));
    drag_column = g_value_get_object (value);

    if (drop_column == drag_column)
    {
        return GDK_ACTION_MOVE;
    }

    if (g_list_store_find (G_LIST_STORE (chooser->model), drag_column, &orig_pos) &&
        g_list_store_find (G_LIST_STORE (chooser->model), drop_column, &dest_pos))
    {
        g_list_store_remove (G_LIST_STORE (chooser->model), orig_pos);
        g_list_store_insert (G_LIST_STORE (chooser->model), dest_pos, drag_column);
        list_changed (chooser);

        return GDK_ACTION_MOVE;
    }

    return 0;
}

static gboolean
on_row_drop (GtkDropTarget *self,
             const GValue  *value,
             gdouble        x,
             gdouble        y,
             gpointer       user_data)
{
    return TRUE;
}

static GtkWidget *
add_list_box_row (GObject  *item,
                  gpointer  user_data)
{
    NautilusColumn *column = NAUTILUS_COLUMN (item);
    NautilusColumnChooser *chooser = user_data;
    g_autofree char *label = NULL;
    g_autofree char *name = NULL;
    GtkWidget *row;
    GtkWidget *row_switch;
    GtkEventController *controller;

    g_object_get (column, "label", &label, "name", &name, NULL);

    row = adw_action_row_new ();
    adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row), label);

    if (g_strcmp0 (name, "name") == 0)
    {
        return row;
    }

    row_switch = gtk_switch_new ();
    g_object_bind_property (column, "visible", row_switch, "active",
                            G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);
    g_signal_connect (row_switch, "notify::active",
                      G_CALLBACK (notify_row_switch_cb),
                      chooser);

    gtk_widget_set_halign (row_switch, GTK_ALIGN_END);
    gtk_widget_set_valign (row_switch, GTK_ALIGN_CENTER);
    adw_action_row_add_suffix (ADW_ACTION_ROW (row), row_switch);

    controller = GTK_EVENT_CONTROLLER (gtk_drag_source_new ());
    gtk_drag_source_set_actions (GTK_DRAG_SOURCE (controller), GDK_ACTION_MOVE);
    g_signal_connect (controller, "prepare", G_CALLBACK (on_row_drag_prepare), column);
    g_signal_connect (controller, "drag-cancel", G_CALLBACK (on_row_drag_cancel), chooser);
    gtk_widget_add_controller (row, controller);

    controller = GTK_EVENT_CONTROLLER (gtk_drop_target_new (NAUTILUS_TYPE_COLUMN, GDK_ACTION_MOVE));
    gtk_drop_target_set_preload (GTK_DROP_TARGET (controller), TRUE);
    g_signal_connect (controller, "enter", G_CALLBACK (on_row_drop_enter), column);
    g_signal_connect (controller, "drop", G_CALLBACK (on_row_drop), column);
    gtk_widget_add_controller (row, controller);

    return row;
}

static guint
strv_index (char **column_order,
            char  *name)
{
    for (gint i = 0; column_order[i] != NULL; i++)
    {
        if (g_strcmp0 (column_order[i], name) == 0)
        {
            return i;
        }
    }

    return G_MAXUINT;
}

static gint
column_sort_func (gconstpointer a,
                  gconstpointer b,
                  gpointer      user_data)
{
    g_autofree char *a_name = NULL;
    g_autofree char *b_name = NULL;
    guint a_pos, b_pos;
    char **column_order = user_data;

    g_object_get ((gpointer) a, "name", &a_name, NULL);
    g_object_get ((gpointer) b, "name", &b_name, NULL);
    a_pos = strv_index (column_order, a_name);
    b_pos = strv_index (column_order, b_name);

    return a_pos == b_pos ? 0 : a_pos < b_pos ? -1 : 1;
}

static void
set_column_order (NautilusColumnChooser  *chooser,
                  char                  **column_order)
{
    g_list_store_sort (G_LIST_STORE (chooser->model), column_sort_func, column_order);
}

static void
set_visible_columns (NautilusColumnChooser  *chooser,
                     char                  **visible_columns)
{
    for (guint i = 0; i < g_list_model_get_n_items (chooser->model); i++)
    {
        g_autoptr (NautilusColumn) column = g_list_model_get_item (chooser->model, i);
        g_autofree char *name = NULL;
        gboolean visible;

        g_object_get (column, "name", &name, NULL);
        visible = g_strv_contains ((const char **) visible_columns, name);
        g_object_set (column, "visible", visible, NULL);
    }
}

static void
use_default_clicked_callback (GtkWidget *button,
                              gpointer   user_data)
{
    NautilusColumnChooser *chooser = user_data;
    g_auto (GStrv) default_columns = NULL;
    g_auto (GStrv) default_order = NULL;

    nautilus_column_save_metadata (chooser->file, NULL, NULL);

    /* set view values ourselves, as new metadata could not have been
     * updated yet.
     */
    default_columns = nautilus_column_get_default_visible_columns (chooser->file);
    default_order = nautilus_column_get_default_column_order (chooser->file);
    set_visible_columns (chooser, default_columns);
    set_column_order (chooser, default_order);

    list_changed (chooser);
}

static void
populate_list (NautilusColumnChooser *chooser)
{
    GList *columns = nautilus_get_columns_for_file (chooser->file);
    g_auto (GStrv) visible_columns = nautilus_column_get_visible_columns (chooser->file);
    g_auto (GStrv) column_order = nautilus_column_get_column_order (chooser->file);

    g_list_store_remove_all (G_LIST_STORE (chooser->model));

    for (GList *l = columns; l != NULL; l = l->next)
    {
        g_autofree char *name = NULL;

        g_object_get (l->data, "name", &name, NULL);

        if (strcmp (name, "starred") == 0)
        {
            continue;
        }

        g_list_store_append (G_LIST_STORE (chooser->model), l->data);
    }

    set_visible_columns (chooser, visible_columns);
    set_column_order (chooser, column_order);

    nautilus_column_list_free (columns);
}

static void
nautilus_column_chooser_constructed (GObject *object)
{
    NautilusColumnChooser *chooser;
    g_autofree gchar *name = NULL;

    G_OBJECT_CLASS (nautilus_column_chooser_parent_class)->constructed (object);

    chooser = NAUTILUS_COLUMN_CHOOSER (object);
    name = nautilus_file_get_display_name (chooser->file);

    adw_window_title_set_subtitle (ADW_WINDOW_TITLE (chooser->window_title), name);

    populate_list (chooser);
}

static void
nautilus_column_chooser_finalize (GObject *object)
{
    NautilusColumnChooser *chooser = NAUTILUS_COLUMN_CHOOSER (object);

    g_clear_object (&chooser->model);

    G_OBJECT_CLASS (nautilus_column_chooser_parent_class)->finalize (object);
}

static void
nautilus_column_chooser_class_init (NautilusColumnChooserClass *chooser_class)
{
    GtkWidgetClass *widget_class;
    GObjectClass *oclass;

    widget_class = GTK_WIDGET_CLASS (chooser_class);
    oclass = G_OBJECT_CLASS (chooser_class);

    oclass->set_property = nautilus_column_chooser_set_property;
    oclass->constructed = nautilus_column_chooser_constructed;
    oclass->finalize = nautilus_column_chooser_finalize;

    gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/nautilus/ui/nautilus-column-chooser.ui");
    gtk_widget_class_bind_template_child (widget_class, NautilusColumnChooser, list_box);
    gtk_widget_class_bind_template_child (widget_class, NautilusColumnChooser, use_default_button);
    gtk_widget_class_bind_template_child (widget_class, NautilusColumnChooser, window_title);
    gtk_widget_class_bind_template_callback (widget_class, use_default_clicked_callback);

    gtk_widget_class_add_binding_action (widget_class, GDK_KEY_Escape, 0, "window.close", NULL);

    signals[CHANGED] = g_signal_new
                           ("changed",
                           G_TYPE_FROM_CLASS (chooser_class),
                           G_SIGNAL_RUN_LAST,
                           0, NULL, NULL,
                           g_cclosure_marshal_generic,
                           G_TYPE_NONE,
                           2, G_TYPE_STRV, G_TYPE_STRV);

    signals[USE_DEFAULT] = g_signal_new
                               ("use-default",
                               G_TYPE_FROM_CLASS (chooser_class),
                               G_SIGNAL_RUN_LAST,
                               0, NULL, NULL,
                               g_cclosure_marshal_VOID__VOID,
                               G_TYPE_NONE, 0);

    g_object_class_install_property (oclass,
                                     PROP_FILE,
                                     g_param_spec_object ("file", NULL, NULL,
                                                          NAUTILUS_TYPE_FILE,
                                                          G_PARAM_CONSTRUCT_ONLY |
                                                          G_PARAM_WRITABLE));
}

static void
nautilus_column_chooser_init (NautilusColumnChooser *chooser)
{
    gtk_widget_init_template (GTK_WIDGET (chooser));

    chooser->model = G_LIST_MODEL (g_list_store_new (G_TYPE_OBJECT));

    gtk_list_box_bind_model (GTK_LIST_BOX (chooser->list_box),
                             G_LIST_MODEL (chooser->model),
                             (GtkListBoxCreateWidgetFunc) add_list_box_row,
                             chooser,
                             NULL);
}

GtkWidget *
nautilus_column_chooser_new (NautilusFile *file)
{
    return g_object_new (NAUTILUS_TYPE_COLUMN_CHOOSER, "file", file, NULL);
}
