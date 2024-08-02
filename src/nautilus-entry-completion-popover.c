/*
 * Copyright © 2024 Adrien Plazas <aplazas@gnome.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "nautilus-entry-completion-popover.h"

#define PAGE_STEP 14

struct _NautilusEntryCompletionPopover
{
    GtkPopover parent_instance;

    GtkEventController *parent_key_controller;

    /* Template widgets */
    GtkWidget *list_view;
    GtkWidget *scrolled_window;

    /* Template objects */
    GtkSingleSelection *selection_model;
};

G_DEFINE_FINAL_TYPE (NautilusEntryCompletionPopover, nautilus_entry_completion_popover, GTK_TYPE_POPOVER)

enum
{
    PROP_0,
    PROP_MODEL,
    PROP_FACTORY,
    PROP_SELECTED,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

NautilusEntryCompletionPopover *
nautilus_entry_completion_popover_new (void)
{
    return g_object_new (NAUTILUS_TYPE_ENTRY_COMPLETION_POPOVER, NULL);
}

GListModel *
nautilus_entry_completion_popover_get_model (NautilusEntryCompletionPopover *self)
{
    g_return_val_if_fail (NAUTILUS_IS_ENTRY_COMPLETION_POPOVER (self), NULL);

    return gtk_single_selection_get_model (self->selection_model);
}

void
nautilus_entry_completion_popover_set_model (NautilusEntryCompletionPopover *self,
                                             GListModel                     *model)
{
    g_return_if_fail (NAUTILUS_IS_ENTRY_COMPLETION_POPOVER (self));
    g_return_if_fail (model == NULL || G_IS_LIST_MODEL (model));

    gtk_single_selection_set_model (self->selection_model, model);
}

GtkListItemFactory *
nautilus_entry_completion_popover_get_factory (NautilusEntryCompletionPopover *self)
{
    g_return_val_if_fail (NAUTILUS_IS_ENTRY_COMPLETION_POPOVER (self), NULL);

    return gtk_list_view_get_factory (GTK_LIST_VIEW (self->list_view));
}

void
nautilus_entry_completion_popover_set_factory (NautilusEntryCompletionPopover *self,
                                               GtkListItemFactory             *factory)
{
    g_return_if_fail (NAUTILUS_IS_ENTRY_COMPLETION_POPOVER (self));
    g_return_if_fail (factory == NULL || GTK_IS_LIST_ITEM_FACTORY (factory));

    gtk_list_view_set_factory (GTK_LIST_VIEW (self->list_view), factory);
}

guint
nautilus_entry_completion_popover_get_selected (NautilusEntryCompletionPopover *self)
{
    g_return_val_if_fail (NAUTILUS_IS_ENTRY_COMPLETION_POPOVER (self), GTK_INVALID_LIST_POSITION);

    return gtk_single_selection_get_selected (self->selection_model);
}

void
nautilus_entry_completion_popover_set_selected (NautilusEntryCompletionPopover *self,
                                                guint                           position)
{
    g_return_if_fail (NAUTILUS_IS_ENTRY_COMPLETION_POPOVER (self));

    gtk_single_selection_set_selected (self->selection_model, position);
}

static gboolean
parent_key_controller_key_pressed (GtkEventControllerKey *controller,
                                   unsigned int           keyval,
                                   unsigned int           keycode,
                                   GdkModifierType        state,
                                   gpointer               user_data)
{
    NautilusEntryCompletionPopover *self = NAUTILUS_ENTRY_COMPLETION_POPOVER (user_data);
    guint selected, matches;

    if (state & (GDK_SHIFT_MASK | GDK_ALT_MASK | GDK_CONTROL_MASK))
    {
        return GDK_EVENT_PROPAGATE;
    }

    if (keyval != GDK_KEY_Up && keyval != GDK_KEY_KP_Up &&
        keyval != GDK_KEY_Down && keyval != GDK_KEY_KP_Down &&
        keyval != GDK_KEY_Page_Up && keyval != GDK_KEY_KP_Page_Up &&
        keyval != GDK_KEY_Page_Down && keyval != GDK_KEY_KP_Page_Down)
    {
        return GDK_EVENT_PROPAGATE;
    }

    if (!gtk_widget_get_visible (GTK_WIDGET (self)))
    {
        return GDK_EVENT_PROPAGATE;
    }

    gtk_widget_grab_focus (GTK_WIDGET (self));

    matches = g_list_model_get_n_items (G_LIST_MODEL (self->selection_model));
    selected = gtk_single_selection_get_selected (self->selection_model);

    if (keyval == GDK_KEY_Up || keyval == GDK_KEY_KP_Up)
    {
        if (selected == GTK_INVALID_LIST_POSITION)
        {
            selected = matches - 1;
        }
        else if (selected == 0)
        {
            selected = GTK_INVALID_LIST_POSITION;
        }
        else
        {
            selected--;
        }
    }

    if (keyval == GDK_KEY_Down || keyval == GDK_KEY_KP_Down)
    {
        if (selected == GTK_INVALID_LIST_POSITION)
        {
            selected = 0;
        }
        else if (selected == matches - 1)
        {
            selected = GTK_INVALID_LIST_POSITION;
        }
        else
        {
            selected++;
        }
    }

    if (keyval == GDK_KEY_Page_Up || keyval == GDK_KEY_KP_Page_Up)
    {
        if (selected == GTK_INVALID_LIST_POSITION)
        {
            selected = matches - 1;
        }
        else if (selected == 0)
        {
            selected = GTK_INVALID_LIST_POSITION;
        }
        else if (selected < PAGE_STEP)
        {
            selected = 0;
        }
        else
        {
            selected -= PAGE_STEP;
        }
    }

    if (keyval == GDK_KEY_Page_Down || keyval == GDK_KEY_KP_Page_Down)
    {
        if (selected == GTK_INVALID_LIST_POSITION)
        {
            selected = 0;
        }
        else if (selected == matches - 1)
        {
            selected = GTK_INVALID_LIST_POSITION;
        }
        else if (selected + PAGE_STEP > matches - 1)
        {
            selected = matches - 1;
        }
        else
        {
            selected += PAGE_STEP;
        }
    }

    if (selected == GTK_INVALID_LIST_POSITION)
    {
        gtk_single_selection_set_selected (self->selection_model, GTK_INVALID_LIST_POSITION);
    }
    else
    {
        gtk_list_view_scroll_to (GTK_LIST_VIEW (self->list_view), selected, GTK_LIST_SCROLL_SELECT, NULL);
    }

    return GDK_EVENT_STOP;
}

static void
list_view_notify_factory_cb (GObject      *object,
                             GAsyncResult *result,
                             gpointer      user_data)
{
    g_object_notify_by_pspec (object, properties[PROP_FACTORY]);
}

static void
list_view_notify_model_cb (GObject      *object,
                           GAsyncResult *result,
                           gpointer      user_data)
{
    g_object_notify_by_pspec (object, properties[PROP_MODEL]);
}

static void
notify_parent_cb (GObject      *object,
                  GAsyncResult *result,
                  gpointer      user_data)
{
    NautilusEntryCompletionPopover *self = NAUTILUS_ENTRY_COMPLETION_POPOVER (user_data);
    GtkWidget *parent = gtk_widget_get_parent (GTK_WIDGET (self));

    if (self->parent_key_controller != NULL)
    {
        g_signal_handlers_disconnect_by_func (self->parent_key_controller, parent_key_controller_key_pressed, self);
        g_clear_weak_pointer (&self->parent_key_controller);
    }

    if (parent != NULL)
    {
        g_set_weak_pointer (&self->parent_key_controller, gtk_event_controller_key_new ());
        gtk_widget_add_controller (parent, self->parent_key_controller);
        gtk_event_controller_set_propagation_phase (self->parent_key_controller, GTK_PHASE_BUBBLE);
        g_signal_connect (self->parent_key_controller, "key-pressed",
                          G_CALLBACK (parent_key_controller_key_pressed), self);
    }
}

static void
selection_model_items_changed_cb (gpointer    user_data,
                                  guint       position,
                                  guint       removed,
                                  guint       added,
                                  GListModel *list_model)
{
    NautilusEntryCompletionPopover *self = NAUTILUS_ENTRY_COMPLETION_POPOVER (user_data);

    gtk_widget_set_visible (GTK_WIDGET (self), g_list_model_get_n_items (list_model) > 0);
}

static void
selection_model_notify_model_cb (GObject      *object,
                                 GAsyncResult *result,
                                 gpointer      user_data)
{
    g_object_notify_by_pspec (object, properties[PROP_MODEL]);
}

static void
selection_model_notify_selected_cb (GObject      *object,
                                    GAsyncResult *result,
                                    gpointer      user_data)
{
    g_object_notify_by_pspec (object, properties[PROP_SELECTED]);
}

static void
nautilus_entry_completion_popover_dispose (GObject *object)
{
    NautilusEntryCompletionPopover *self = NAUTILUS_ENTRY_COMPLETION_POPOVER (object);

    g_clear_weak_pointer (&self->parent_key_controller);

    G_OBJECT_CLASS (nautilus_entry_completion_popover_parent_class)->dispose (object);
}

static void
nautilus_entry_completion_popover_get_property (GObject    *object,
                                                guint       prop_id,
                                                GValue     *value,
                                                GParamSpec *pspec)
{
    NautilusEntryCompletionPopover *self = NAUTILUS_ENTRY_COMPLETION_POPOVER (object);

    switch (prop_id)
    {
        case PROP_MODEL:
        {
            g_value_set_object (value, nautilus_entry_completion_popover_get_model (self));
        }
        break;

        case PROP_FACTORY:
        {
            g_value_set_object (value, nautilus_entry_completion_popover_get_factory (self));
        }
        break;

        case PROP_SELECTED:
        {
            g_value_set_uint (value, nautilus_entry_completion_popover_get_selected (self));
        }
        break;

        default:
        {
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        }
    }
}

static void
nautilus_entry_completion_popover_set_property (GObject      *object,
                                                guint         prop_id,
                                                const GValue *value,
                                                GParamSpec   *pspec)
{
    NautilusEntryCompletionPopover *self = NAUTILUS_ENTRY_COMPLETION_POPOVER (object);

    switch (prop_id)
    {
        case PROP_MODEL:
        {
            nautilus_entry_completion_popover_set_model (self, g_value_get_object (value));
        }
        break;

        case PROP_FACTORY:
        {
            nautilus_entry_completion_popover_set_factory (self, g_value_get_object (value));
        }
        break;

        case PROP_SELECTED:
        {
            nautilus_entry_completion_popover_set_selected (self, g_value_get_uint (value));
        }
        break;

        default:
        {
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        }
    }
}

static void
nautilus_entry_completion_popover_size_allocate (GtkWidget *widget,
                                                 int        width,
                                                 int        height,
                                                 int        baseline)
{
    NautilusEntryCompletionPopover *self = NAUTILUS_ENTRY_COMPLETION_POPOVER (widget);
    GtkWidget *row;
    int row_height = 0;

    GTK_WIDGET_CLASS (nautilus_entry_completion_popover_parent_class)->size_allocate (widget, width, height, baseline);

    row = gtk_widget_get_first_child (self->list_view);

    if (row != NULL)
    {
        gtk_widget_measure (row, GTK_ORIENTATION_VERTICAL, -1, &row_height, NULL, NULL, NULL);
    }

    gtk_scrolled_window_set_max_content_height (GTK_SCROLLED_WINDOW (self->scrolled_window), row_height * 10);
}

static void
nautilus_entry_completion_popover_class_init (NautilusEntryCompletionPopoverClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

    object_class->dispose = nautilus_entry_completion_popover_dispose;
    object_class->get_property = nautilus_entry_completion_popover_get_property;
    object_class->set_property = nautilus_entry_completion_popover_set_property;

    widget_class->size_allocate = nautilus_entry_completion_popover_size_allocate;

    properties[PROP_MODEL] =
        g_param_spec_object ("model", NULL, NULL,
                             G_TYPE_LIST_MODEL,
                             G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    properties[PROP_FACTORY] =
        g_param_spec_object ("factory", NULL, NULL,
                             GTK_TYPE_LIST_ITEM_FACTORY,
                             G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    properties[PROP_SELECTED] =
        g_param_spec_uint ("selected", NULL, NULL,
                           0, G_MAXUINT, GTK_INVALID_LIST_POSITION,
                           G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/nautilus/ui/nautilus-entry-completion-popover.ui");

    gtk_widget_class_bind_template_child (widget_class, NautilusEntryCompletionPopover, list_view);
    gtk_widget_class_bind_template_child (widget_class, NautilusEntryCompletionPopover, scrolled_window);
    gtk_widget_class_bind_template_child (widget_class, NautilusEntryCompletionPopover, selection_model);

    gtk_widget_class_bind_template_callback (widget_class, list_view_notify_factory_cb);
    gtk_widget_class_bind_template_callback (widget_class, list_view_notify_model_cb);
    gtk_widget_class_bind_template_callback (widget_class, notify_parent_cb);
    gtk_widget_class_bind_template_callback (widget_class, selection_model_items_changed_cb);
    gtk_widget_class_bind_template_callback (widget_class, selection_model_notify_model_cb);
    gtk_widget_class_bind_template_callback (widget_class, selection_model_notify_selected_cb);
}

static void
nautilus_entry_completion_popover_init (NautilusEntryCompletionPopover *self)
{
    gtk_widget_init_template (GTK_WIDGET (self));
}
