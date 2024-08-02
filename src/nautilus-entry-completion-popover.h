/*
 * Copyright © 2024 Adrien Plazas <aplazas@gnome.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define NAUTILUS_TYPE_ENTRY_COMPLETION_POPOVER (nautilus_entry_completion_popover_get_type())

G_DECLARE_FINAL_TYPE (NautilusEntryCompletionPopover, nautilus_entry_completion_popover, NAUTILUS, ENTRY_COMPLETION_POPOVER, GtkPopover)

NautilusEntryCompletionPopover *nautilus_entry_completion_popover_new (void);

GListModel *nautilus_entry_completion_popover_get_model (NautilusEntryCompletionPopover *self);
void        nautilus_entry_completion_popover_set_model (NautilusEntryCompletionPopover *self,
                                                         GListModel                     *model);

GtkListItemFactory *nautilus_entry_completion_popover_get_factory (NautilusEntryCompletionPopover *self);
void                nautilus_entry_completion_popover_set_factory (NautilusEntryCompletionPopover *self,
                                                                   GtkListItemFactory             *model);

guint nautilus_entry_completion_popover_get_selected (NautilusEntryCompletionPopover *self);
void  nautilus_entry_completion_popover_set_selected (NautilusEntryCompletionPopover *self,
                                                      guint                           position);

G_END_DECLS
