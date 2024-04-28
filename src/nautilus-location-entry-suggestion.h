/*
 * Copyright © 2024 Adrien Plazas <aplazas@gnome.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define NAUTILUS_TYPE_LOCATION_ENTRY_SUGGESTION (nautilus_location_entry_suggestion_get_type())

G_DECLARE_FINAL_TYPE (NautilusLocationEntrySuggestion, nautilus_location_entry_suggestion, NAUTILUS, LOCATION_ENTRY_SUGGESTION, GObject)

NautilusLocationEntrySuggestion *nautilus_location_entry_suggestion_new (GtkStringObject *prefix,
                                                                         const char      *suggestion);

GtkStringObject *nautilus_location_entry_suggestion_get_prefix (NautilusLocationEntrySuggestion *self);
void             nautilus_location_entry_suggestion_set_prefix (NautilusLocationEntrySuggestion *self,
                                                                GtkStringObject                 *prefix);

const char *nautilus_location_entry_suggestion_get_suggestion (NautilusLocationEntrySuggestion *self);
void        nautilus_location_entry_suggestion_set_suggestion (NautilusLocationEntrySuggestion *self,
                                                               const char                      *suggestion);

const char *nautilus_location_entry_suggestion_get_suffix (NautilusLocationEntrySuggestion *self);

G_END_DECLS
