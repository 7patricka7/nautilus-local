/*
 * Copyright (C) 2022 The GNOME project contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "nautilus-grid-cell.h"

#include "nautilus-global-preferences.h"
#include "nautilus-tag-manager.h"
#include "nautilus-thumbnails.h"

struct _NautilusGridCell
{
    NautilusViewCell parent_instance;

    GSignalGroup *item_signal_group;

    GQuark *caption_attributes;

    GtkWidget *fixed_height_box;
    GtkWidget *icon;
    GtkWidget *emblems_box;
    GtkWidget *first_caption;
    GtkWidget *second_caption;
    GtkWidget *third_caption;
};

G_DEFINE_TYPE (NautilusGridCell, nautilus_grid_cell, NAUTILUS_TYPE_VIEW_CELL)

static void
update_icon (NautilusGridCell *self)
{
    g_autoptr (NautilusViewItem) item = NULL;
    NautilusFileIconFlags flags;
    g_autoptr (GdkPaintable) icon_paintable = NULL;
    NautilusFile *file;
    guint icon_size;
    gint scale_factor;

    item = nautilus_view_cell_get_item (NAUTILUS_VIEW_CELL (self));
    g_return_if_fail (item != NULL);
    file = nautilus_view_item_get_file (item);
    g_object_get (self, "icon-size", &icon_size, NULL);
    scale_factor = gtk_widget_get_scale_factor (GTK_WIDGET (self));
    flags = NAUTILUS_FILE_ICON_FLAGS_USE_THUMBNAILS;

    icon_paintable = nautilus_file_get_icon_paintable (file, icon_size, scale_factor, flags);
    gtk_picture_set_paintable (GTK_PICTURE (self->icon), icon_paintable);

    /* Set the same height and width for all icons regardless of aspect ratio.
     */
    gtk_widget_set_size_request (self->fixed_height_box, icon_size, icon_size);
    if (nautilus_file_get_thumbnail_path (file) != NULL &&
        nautilus_file_should_show_thumbnail (file) &&
        nautilus_file_check_if_ready (file, NAUTILUS_FILE_ATTRIBUTE_THUMBNAIL))
    {
        gtk_widget_add_css_class (self->icon, "thumbnail");
    }
    else
    {
        gtk_widget_remove_css_class (self->icon, "thumbnail");
    }
}

static void
update_captions (NautilusGridCell *self)
{
    g_autoptr (NautilusViewItem) item = NULL;
    NautilusFile *file;
    GtkWidget * const caption_labels[] =
    {
        self->first_caption,
        self->second_caption,
        self->third_caption
    };
    G_STATIC_ASSERT (G_N_ELEMENTS (caption_labels) == NAUTILUS_GRID_CELL_N_CAPTIONS);

    item = nautilus_view_cell_get_item (NAUTILUS_VIEW_CELL (self));
    g_return_if_fail (item != NULL);
    file = nautilus_view_item_get_file (item);
    for (guint i = 0; i < NAUTILUS_GRID_CELL_N_CAPTIONS; i++)
    {
        GQuark attribute_q = self->caption_attributes[i];
        gboolean show_caption;

        show_caption = (attribute_q != 0);
        gtk_widget_set_visible (caption_labels[i], show_caption);
        if (show_caption)
        {
            g_autofree gchar *string = NULL;
            string = nautilus_file_get_string_attribute_q (file, attribute_q);
            gtk_label_set_text (GTK_LABEL (caption_labels[i]), string);
        }
    }
}

static void
update_emblems (NautilusGridCell *self)
{
    g_autoptr (NautilusViewItem) item = NULL;
    NautilusFile *file;
    GtkWidget *child;
    GtkIconTheme *theme;
    g_autolist (GIcon) emblems = NULL;
    g_autofree gchar *file_uri = NULL;

    item = nautilus_view_cell_get_item (NAUTILUS_VIEW_CELL (self));
    g_return_if_fail (item != NULL);
    file = nautilus_view_item_get_file (item);
    file_uri = nautilus_file_get_uri (file);

    /* Remove old emblems. */
    while ((child = gtk_widget_get_first_child (self->emblems_box)) != NULL)
    {
        gtk_box_remove (GTK_BOX (self->emblems_box), child);
    }

    if (nautilus_tag_manager_file_is_starred (nautilus_tag_manager_get (), file_uri))
    {
        gtk_box_append (GTK_BOX (self->emblems_box),
                        gtk_image_new_from_icon_name ("starred-symbolic"));
    }

    theme = gtk_icon_theme_get_for_display (gdk_display_get_default ());
    emblems = nautilus_file_get_emblem_icons (file);
    for (GList *l = emblems; l != NULL; l = l->next)
    {
        if (!gtk_icon_theme_has_gicon (theme, l->data))
        {
            g_autofree gchar *icon_string = g_icon_to_string (l->data);
            g_warning ("Failed to add emblem. “%s” not found in the icon theme",
                       icon_string);
            continue;
        }

        gtk_box_append (GTK_BOX (self->emblems_box),
                        gtk_image_new_from_gicon (l->data));
    }
}

static void
on_file_changed (NautilusGridCell *self)
{
    update_icon (self);
    update_emblems (self);
    update_captions (self);
}

static void
on_icon_size_changed (NautilusGridCell *self)
{
    g_autoptr (NautilusViewItem) item = nautilus_view_cell_get_item (NAUTILUS_VIEW_CELL (self));

    if (item == NULL)
    {
        /* Cell is not bound to an item yet. Do nothing. */
        return;
    }

    update_icon (self);
    update_captions (self);
}

static gboolean
on_label_query_tooltip (GtkWidget  *widget,
                        int         x,
                        int         y,
                        gboolean    keyboard_tip,
                        GtkTooltip *tooltip,
                        gpointer    user_data)
{
    GtkLabel *label = GTK_LABEL (widget);

    if (pango_layout_is_ellipsized (gtk_label_get_layout (label)))
    {
        gtk_tooltip_set_text (tooltip, gtk_label_get_text (label));
        return TRUE;
    }

    return FALSE;
}

static void
on_starred_changed (NautilusTagManager *tag_manager,
                    GList              *changed_files,
                    gpointer            user_data)
{
    NautilusGridCell *self = NAUTILUS_GRID_CELL (user_data);
    g_autoptr (NautilusViewItem) item = NULL;
    NautilusFile *file;

    item = nautilus_view_cell_get_item (NAUTILUS_VIEW_CELL (self));
    if (item == NULL)
    {
        return;
    }

    file = nautilus_view_item_get_file (item);
    if (g_list_find (changed_files, file))
    {
        update_emblems (self);
    }
}

static void
on_map_changed (GtkWidget *widget,
                gpointer   user_data)
{
    NautilusViewCell *cell = NAUTILUS_VIEW_CELL (widget);
    gboolean is_mapped = GPOINTER_TO_INT (user_data);
    g_autoptr (NautilusViewItem) item = nautilus_view_cell_get_item (cell);

    g_return_if_fail (item != NULL);

    NautilusFile *file = nautilus_view_item_get_file (item);

    if (nautilus_file_is_thumbnailing (file))
    {
        g_autofree char *uri = nautilus_file_get_uri (file);
        if (is_mapped)
        {
            nautilus_thumbnail_prioritize (uri);
        }
        else
        {
            nautilus_thumbnail_deprioritize (uri);
        }
    }
}

static void
nautilus_grid_cell_dispose (GObject *object)
{
    NautilusGridCell *self = (NautilusGridCell *) object;

    gtk_widget_dispose_template (GTK_WIDGET (self), NAUTILUS_TYPE_GRID_CELL);

    G_OBJECT_CLASS (nautilus_grid_cell_parent_class)->dispose (object);
}

static void
finalize (GObject *object)
{
    NautilusGridCell *self = (NautilusGridCell *) object;

    g_object_unref (self->item_signal_group);
    G_OBJECT_CLASS (nautilus_grid_cell_parent_class)->finalize (object);
}

static GskPath *
get_border_path (graphene_rect_t *rect,
                 float            radius)
{
    float width = rect->size.width, height = rect->size.height;

    GskPathBuilder *path_builder = gsk_path_builder_new ();

    gsk_path_builder_move_to (path_builder, rect->origin.x, rect->origin.y + radius);
    gsk_path_builder_rel_arc_to (path_builder, 0.0, -radius, radius, -radius);
    gsk_path_builder_rel_line_to (path_builder, width, 0.0);
    gsk_path_builder_rel_arc_to (path_builder, radius, 0.0, radius, radius);
    gsk_path_builder_rel_line_to (path_builder, 0.0, height);
    gsk_path_builder_rel_arc_to (path_builder, 0.0, radius, -radius, radius);
    gsk_path_builder_rel_line_to (path_builder, -width, 0.0);
    gsk_path_builder_rel_arc_to (path_builder, -radius, 0.0, -radius, -radius);
    gsk_path_builder_close (path_builder);

    return gsk_path_builder_free_to_path (path_builder);
}

static void
snapshot (GtkWidget   *widget,
          GtkSnapshot *snapshot)
{
    NautilusViewCell *self = (NautilusViewCell *) widget;
    gboolean is_cut;
    g_autoptr (NautilusViewItem) item = NULL;

    item = nautilus_view_cell_get_item (NAUTILUS_VIEW_CELL (self));
    g_object_get (item,
                  "is-cut", &is_cut,
                  NULL);

    if (is_cut)
    {
        /*
         * Draw scissor icon
        {
             GtkIconTheme *theme = gtk_icon_theme_get_for_display (gdk_display_get_default ());
            GtkIconPaintable *paintable = gtk_icon_theme_lookup_icon (theme, "edit-cut-symbolic",
                                                                      NULL, 16, 1, GTK_TEXT_DIR_NONE,
                                                                      GTK_ICON_LOOKUP_FORCE_SYMBOLIC |
                                                                      GTK_ICON_LOOKUP_PRELOAD);
            GtkSnapshot *icon_snapshot = gtk_snapshot_new ();
            GskRenderNode *node;
            graphene_point_t icon_point = GRAPHENE_POINT_INIT (1, radius);

            gtk_symbolic_paintable_snapshot_symbolic (GTK_SYMBOLIC_PAINTABLE (paintable), icon_snapshot, 16.0, 16.0, &color, 1);
            GskTransform *transform = gsk_transform_translate (gsk_transform_new (), &icon_point);
            transform = gsk_transform_rotate (transform, 180.0);

            node = gtk_snapshot_free_to_node (icon_snapshot);
            node = gsk_transform_node_new (node, transform);

            gtk_snapshot_append_node (snapshot, node);
        }
        */

        /*
         * Draw dashed border
         */
        float width = gtk_widget_get_width (widget);
        float height = gtk_widget_get_height (widget);
        float radius = 12.0;
        graphene_rect_t border_rect = GRAPHENE_RECT_INIT (- radius / 2.0, - radius / 2.0,
                                                          width - radius, height - radius);
        g_autoptr (GskPath) path = get_border_path (&border_rect, radius);
        GdkRGBA color;
        GskStroke *stroke = gsk_stroke_new (1.5);
        const float dash[] = {10.0 , 8.0};
        const float dash_offset = 2.0;

        gtk_widget_get_color (widget, &color);

        gsk_stroke_set_dash (stroke, dash, 2.0);
        gsk_stroke_set_line_cap (stroke, GSK_LINE_CAP_ROUND);
        gsk_stroke_set_dash_offset (stroke, dash_offset);

        gtk_snapshot_append_stroke (snapshot, path, stroke, &color);
    }

    GTK_WIDGET_CLASS (nautilus_grid_cell_parent_class)->snapshot (widget, snapshot);
}

static void
nautilus_grid_cell_class_init (NautilusGridCellClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

    object_class->dispose = nautilus_grid_cell_dispose;
    object_class->finalize = finalize;

    widget_class->snapshot = snapshot;

    gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/nautilus/ui/nautilus-grid-cell.ui");

    gtk_widget_class_bind_template_child (widget_class, NautilusGridCell, fixed_height_box);
    gtk_widget_class_bind_template_child (widget_class, NautilusGridCell, icon);
    gtk_widget_class_bind_template_child (widget_class, NautilusGridCell, emblems_box);
    gtk_widget_class_bind_template_child (widget_class, NautilusGridCell, first_caption);
    gtk_widget_class_bind_template_child (widget_class, NautilusGridCell, second_caption);
    gtk_widget_class_bind_template_child (widget_class, NautilusGridCell, third_caption);

    gtk_widget_class_bind_template_callback (widget_class, on_label_query_tooltip);

    gtk_widget_class_set_accessible_role (widget_class, GTK_ACCESSIBLE_ROLE_GRID_CELL);
}

static void
nautilus_grid_cell_init (NautilusGridCell *self)
{
    gtk_widget_init_template (GTK_WIDGET (self));

    g_signal_connect (self, "map", G_CALLBACK (on_map_changed), GINT_TO_POINTER (TRUE));
    g_signal_connect (self, "unmap", G_CALLBACK (on_map_changed), GINT_TO_POINTER (FALSE));
    g_signal_connect (self, "notify::icon-size",
                      G_CALLBACK (on_icon_size_changed), NULL);

    g_signal_connect_object (nautilus_tag_manager_get (), "starred-changed",
                             G_CALLBACK (on_starred_changed), self, G_CONNECT_DEFAULT);

    g_signal_connect_object (nautilus_preferences, "changed::" NAUTILUS_PREFERENCES_DATE_TIME_FORMAT,
                             G_CALLBACK (update_captions), self,
                             G_CONNECT_SWAPPED);

    /* Connect automatically to an item. */
    self->item_signal_group = g_signal_group_new (NAUTILUS_TYPE_VIEW_ITEM);
    g_signal_group_connect_swapped (self->item_signal_group, "notify::is-cut",
                                    (GCallback) gtk_widget_queue_draw, self);
    g_signal_group_connect_swapped (self->item_signal_group, "file-changed",
                                    (GCallback) on_file_changed, self);
    g_signal_connect_object (self->item_signal_group, "bind",
                             (GCallback) on_file_changed, self,
                             G_CONNECT_SWAPPED);

    g_object_bind_property (self, "item",
                            self->item_signal_group, "target",
                            G_BINDING_SYNC_CREATE);
}

NautilusGridCell *
nautilus_grid_cell_new (NautilusListBase *view)
{
    return g_object_new (NAUTILUS_TYPE_GRID_CELL,
                         "view", view,
                         NULL);
}

void
nautilus_grid_cell_set_caption_attributes (NautilusGridCell *self,
                                           GQuark           *attrs)
{
    self->caption_attributes = attrs;
}
