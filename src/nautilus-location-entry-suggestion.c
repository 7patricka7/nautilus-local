/*
 * Copyright © 2024 Adrien Plazas <aplazas@gnome.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "nautilus-location-entry-suggestion.h"

struct _NautilusLocationEntrySuggestion
{
    GObject parent_instance;

    /* Properties */
    GtkStringObject *prefix;
    const char *suffix;
    char *suggestion;
};

G_DEFINE_FINAL_TYPE (NautilusLocationEntrySuggestion, nautilus_location_entry_suggestion, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_PREFIX,
    PROP_SUFFIX,
    PROP_SUGGESTION,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

static void
update_suffix (NautilusLocationEntrySuggestion *self)
{
    const char *prefix = gtk_string_object_get_string (self->prefix);
    const char *suffix;

    if (self->suggestion == NULL)
    {
        return;
    }

    if (G_UNLIKELY (!g_str_has_prefix (self->suggestion, prefix)))
    {
        g_critical ("Expected %s to have prefix %s", self->suggestion, prefix);
        return;
    }

    suffix = self->suggestion + strlen (prefix);
    if (self->suffix == suffix || g_strcmp0 (self->suffix, suffix) == 0)
    {
        return;
    }

    self->suffix = suffix;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SUFFIX]);
}

NautilusLocationEntrySuggestion *
nautilus_location_entry_suggestion_new (GtkStringObject *prefix,
                                        const char      *suggestion)
{
    return g_object_new (NAUTILUS_TYPE_LOCATION_ENTRY_SUGGESTION,
                         "prefix", prefix,
                         "suggestion", suggestion,
                         NULL);
}

GtkStringObject *
nautilus_location_entry_suggestion_get_prefix (NautilusLocationEntrySuggestion *self)
{
    g_return_val_if_fail (NAUTILUS_IS_LOCATION_ENTRY_SUGGESTION (self), NULL);

    return self->prefix;
}

void
nautilus_location_entry_suggestion_set_prefix (NautilusLocationEntrySuggestion *self,
                                               GtkStringObject                 *prefix)
{
    g_return_if_fail (NAUTILUS_IS_LOCATION_ENTRY_SUGGESTION (self));
    g_return_if_fail (prefix == NULL || GTK_IS_STRING_OBJECT (prefix));

    if (!g_set_object (&self->prefix, prefix))
    {
        return;
    }

    g_object_freeze_notify (G_OBJECT (self));
    update_suffix (self);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PREFIX]);
    g_object_thaw_notify (G_OBJECT (self));
}

const char *
nautilus_location_entry_suggestion_get_suffix (NautilusLocationEntrySuggestion *self)
{
    g_return_val_if_fail (NAUTILUS_IS_LOCATION_ENTRY_SUGGESTION (self), NULL);

    return self->suffix;
}

const char *
nautilus_location_entry_suggestion_get_suggestion (NautilusLocationEntrySuggestion *self)
{
    g_return_val_if_fail (NAUTILUS_IS_LOCATION_ENTRY_SUGGESTION (self), NULL);

    return self->suggestion;
}

void
nautilus_location_entry_suggestion_set_suggestion (NautilusLocationEntrySuggestion *self,
                                                   const char                      *suggestion)
{
    g_return_if_fail (NAUTILUS_IS_LOCATION_ENTRY_SUGGESTION (self));

    if (!g_set_str (&self->suggestion, suggestion))
    {
        return;
    }

    g_object_freeze_notify (G_OBJECT (self));
    update_suffix (self);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SUGGESTION]);
    g_object_thaw_notify (G_OBJECT (self));
}

static void
nautilus_location_entry_suggestion_dispose (GObject *object)
{
    NautilusLocationEntrySuggestion *self = NAUTILUS_LOCATION_ENTRY_SUGGESTION (object);

    g_clear_object (&self->prefix);
    g_clear_pointer (&self->suggestion, g_free);
    self->suffix = NULL;

    G_OBJECT_CLASS (nautilus_location_entry_suggestion_parent_class)->dispose (object);
}

static void
nautilus_location_entry_suggestion_get_property (GObject    *object,
                                                 guint       prop_id,
                                                 GValue     *value,
                                                 GParamSpec *pspec)
{
    NautilusLocationEntrySuggestion *self = NAUTILUS_LOCATION_ENTRY_SUGGESTION (object);

    switch (prop_id)
    {
        case PROP_PREFIX:
        {
            g_value_set_object (value, nautilus_location_entry_suggestion_get_prefix (self));
        }
        break;

        case PROP_SUFFIX:
        {
            g_value_set_string (value, nautilus_location_entry_suggestion_get_suffix (self));
        }
        break;

        case PROP_SUGGESTION:
        {
            g_value_set_string (value, nautilus_location_entry_suggestion_get_suggestion (self));
        }
        break;

        default:
        {
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        }
    }
}

static void
nautilus_location_entry_suggestion_set_property (GObject      *object,
                                                 guint         prop_id,
                                                 const GValue *value,
                                                 GParamSpec   *pspec)
{
    NautilusLocationEntrySuggestion *self = NAUTILUS_LOCATION_ENTRY_SUGGESTION (object);

    switch (prop_id)
    {
        case PROP_PREFIX:
        {
            nautilus_location_entry_suggestion_set_prefix (self, g_value_get_object (value));
        }
        break;

        case PROP_SUGGESTION:
        {
            nautilus_location_entry_suggestion_set_suggestion (self, g_value_get_string (value));
        }
        break;

        default:
        {
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        }
    }
}

static void
nautilus_location_entry_suggestion_class_init (NautilusLocationEntrySuggestionClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = nautilus_location_entry_suggestion_dispose;
    object_class->get_property = nautilus_location_entry_suggestion_get_property;
    object_class->set_property = nautilus_location_entry_suggestion_set_property;

    properties[PROP_PREFIX] =
        g_param_spec_object ("prefix", NULL, NULL,
                             GTK_TYPE_STRING_OBJECT,
                             G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    properties[PROP_SUFFIX] =
        g_param_spec_string ("suffix", NULL, NULL,
                             NULL,
                             G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    properties[PROP_SUGGESTION] =
        g_param_spec_string ("suggestion", NULL, NULL,
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
nautilus_location_entry_suggestion_init (NautilusLocationEntrySuggestion *self)
{
}
