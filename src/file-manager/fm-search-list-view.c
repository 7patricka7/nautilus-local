/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* fm-search-list-view.c - implementation of list view of a virtual directory,
   based on FMListView.

   Copyright (C) 2000 Eazel, Inc.

   The Gnome Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   The Gnome Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public
   License along with the Gnome Library; see the file COPYING.LIB.  If not,
   write to the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.

   Authors: Rebecca Schulman <rebecka@eazel.com>
*/

#include <config.h>
#include "fm-search-list-view.h"

#include "fm-directory-view.h"
#include "fm-list-view-private.h"
#include "nautilus-indexing-info.h"
#include <bonobo/bonobo-ui-util.h>
#include <gtk/gtksignal.h>
#include <libgnome/gnome-i18n.h>
#include <libgnomeui/gnome-uidefs.h>
#include <libgnomevfs/gnome-vfs-result.h>
#include <libgnomevfs/gnome-vfs-utils.h>
#include <libnautilus-extensions/nautilus-bonobo-extensions.h>
#include <libnautilus-extensions/nautilus-file-attributes.h>
#include <libnautilus-extensions/nautilus-file-utilities.h>
#include <libnautilus-extensions/nautilus-glib-extensions.h>
#include <libnautilus-extensions/nautilus-gnome-extensions.h>
#include <libnautilus-extensions/nautilus-gtk-macros.h>
#include <libnautilus-extensions/nautilus-search-uri.h>
#include <libnautilus-extensions/nautilus-stock-dialogs.h>
#include <libnautilus-extensions/nautilus-string.h>

/* FIXME bugzilla.eazel.com 2815: This code uses part of the
 * NautilusSearchBarCriterion class, which is really for complex
 * search bar user interface. We only need to do some non-UI
 * manipulations of the search URI, so we can refactor the code, put
 * more into nautilus-search-uri.[ch] and get rid of this terrible
 * include statement.
 */
#include "../nautilus-search-bar-criterion.h"

/* Paths to use when creating & referring to Bonobo menu items */
#define MENU_PATH_INDEXING_INFO			"/menu/File/General Status Placeholder/Indexing Info"
#define MENU_PATH_REVEAL_IN_NEW_WINDOW 		"/menu/File/Open Placeholder/Reveal"

#define COMMAND_REVEAL_IN_NEW_WINDOW 		"/commands/Reveal"

struct FMSearchListViewDetails {
	BonoboUIComponent *ui;
};

static void 	fm_search_list_view_initialize       	 (gpointer          object,
						      	  gpointer          klass);
static void 	fm_search_list_view_initialize_class 	 (gpointer          klass);
static void     real_destroy                             (GtkObject        *object);
static void 	real_add_file				 (FMDirectoryView  *view,
							  NautilusFile 	   *file);
static void	real_adding_file 			 (FMListView 	   *view, 
							  NautilusFile 	   *file);
static void	real_removing_file 			 (FMListView 	   *view, 
							  NautilusFile 	   *file);
static gboolean real_file_still_belongs 		 (FMListView 	   *view, 
							  NautilusFile 	   *file);
static int  	real_get_number_of_columns           	 (FMListView       *list_view);
static int  	real_get_link_column                 	 (FMListView       *list_view);
static char *   real_get_default_sort_attribute      	 (FMListView       *view);
static void 	real_get_column_specification        	 (FMListView       *list_view,
						      	  int               column_number,
						      	  FMListViewColumn *specification);
static NautilusStringList * real_get_emblem_names_to_exclude         (FMDirectoryView  *view);
static void	real_merge_menus 		     	 (FMDirectoryView  *view);
static gboolean real_supports_creating_files		 (FMDirectoryView  *view);
static gboolean real_accepts_dragged_files		 (FMDirectoryView  *view);
static gboolean real_supports_properties 	     	 (FMDirectoryView  *view);
static void 	load_location_callback               	 (NautilusView 	   *nautilus_view, 
						      	  char 		   *location);
static void	real_update_menus 		     	 (FMDirectoryView  *view);
static void	reveal_selected_items_callback 		 (BonoboUIComponent *component, 
							  gpointer 	    user_data, 
							  const char 	   *verb);


NAUTILUS_DEFINE_CLASS_BOILERPLATE (FMSearchListView,
				   fm_search_list_view,
				   FM_TYPE_LIST_VIEW)

static void
load_location_callback (NautilusView *nautilus_view, char *location)
{
	char *last_indexing_time, *status_string;
	
	nautilus_view_set_title (nautilus_view, "Search Results");

	last_indexing_time = nautilus_indexing_info_get_last_index_time ();
	status_string = g_strdup_printf ("Search results may not include items modified after %s, when your drive was last indexed.",
					 last_indexing_time);
	g_free (last_indexing_time);

	nautilus_view_report_status (nautilus_view, status_string);
	g_free (status_string);
}

/* FIXME: GnomeVFSResults may not be the
   best way to communicate an error code to
   a view */
static void
load_error_callback (FMDirectoryView *nautilus_view, 
		     GnomeVFSResult result,
		     gpointer callback_data)
{
	GnomeDialog *load_error_dialog;
	char *generic_error_string;
	

	switch (result) {
	case GNOME_VFS_ERROR_SERVICE_OBSOLETE:
		/* FIXME: Shoudl be two messages, one for each of whether
		   "slow complete search" turned on or not */
		load_error_dialog = nautilus_yes_no_dialog (_("The search you have selected "
							      "is newer than the index on your "
							      "system.  The search will return no "
							      "results right now.  Would you like "
							      "to create a new index now?"),
							    _("Search for items that are too new"),
							    _("Create a new index"),
							    _("Don't create index"),
							    NULL);
		gtk_signal_connect (GTK_OBJECT (nautilus_gnome_dialog_get_button_by_index
						(load_error_dialog, GNOME_OK)),
				    "clicked",
				    nautilus_indexing_info_request_reindex,
				    NULL);
		break;
	case GNOME_VFS_ERROR_TOO_BIG:
		generic_error_string = g_strdup_printf (_("Every indexed file on your computer "
							  "matches the criteria you selected. "
							  "You can check the spelling on your selections "
							  "or add more criteria to narrow your results."));
		load_error_dialog = nautilus_error_dialog (generic_error_string,
							   "Error during directory load",
							   NULL);
		break;

	default:
		generic_error_string = g_strdup_printf (_("An error occurred while loading "
							  "this search's contents: "
							  "%s"),
							gnome_vfs_result_to_string (result));
		load_error_dialog = nautilus_error_dialog (generic_error_string,
							   "Error during directory load",
							   NULL);
	}
}

static void
fm_search_list_view_initialize_class (gpointer klass)
{
	GtkObjectClass *object_class;
	FMDirectoryViewClass *fm_directory_view_class;
	FMListViewClass *fm_list_view_class;
	
	object_class = GTK_OBJECT_CLASS (klass);
	fm_directory_view_class = FM_DIRECTORY_VIEW_CLASS (klass);
	fm_list_view_class = FM_LIST_VIEW_CLASS (klass);

	object_class->destroy = real_destroy;

  	fm_directory_view_class->add_file = real_add_file;
	fm_directory_view_class->get_emblem_names_to_exclude = 
		real_get_emblem_names_to_exclude;
  	fm_directory_view_class->merge_menus = real_merge_menus;
	fm_directory_view_class->supports_creating_files = 
		real_supports_creating_files;
	fm_directory_view_class->accepts_dragged_files = 
		real_accepts_dragged_files;
	fm_directory_view_class->supports_properties = 
		real_supports_properties;
  	fm_directory_view_class->update_menus =	real_update_menus;

	fm_list_view_class->adding_file = real_adding_file;
	fm_list_view_class->removing_file = real_removing_file;
	fm_list_view_class->get_number_of_columns = real_get_number_of_columns;
	fm_list_view_class->get_link_column = real_get_link_column;
	fm_list_view_class->get_column_specification = real_get_column_specification;
	fm_list_view_class->get_default_sort_attribute = real_get_default_sort_attribute;
	fm_list_view_class->file_still_belongs = real_file_still_belongs;
}

static void
fm_search_list_view_initialize (gpointer object,
				gpointer klass)
{
	FMSearchListView *search_view;
	NautilusView *nautilus_view;
	FMDirectoryView *directory_view;
 
 	g_assert (GTK_BIN (object)->child == NULL);

	search_view = FM_SEARCH_LIST_VIEW (object);
  	directory_view = FM_DIRECTORY_VIEW (object);

	search_view->details = g_new0 (FMSearchListViewDetails, 1);

	nautilus_view = fm_directory_view_get_nautilus_view (directory_view);

	gtk_signal_connect (GTK_OBJECT (nautilus_view),
			    "load_location",
			    GTK_SIGNAL_FUNC (load_location_callback),
			    NULL);
	gtk_signal_connect (GTK_OBJECT (directory_view),
			    "load_error",
			    GTK_SIGNAL_FUNC (load_error_callback),
			    NULL);
}

static void
real_destroy (GtkObject *object)
{
	FMSearchListView *search_view;

	search_view = FM_SEARCH_LIST_VIEW (object);

	if (search_view->details->ui != NULL) {
		bonobo_ui_component_unset_container (search_view->details->ui);
		bonobo_object_unref (BONOBO_OBJECT (search_view->details->ui));
	}
	g_free (search_view->details);

	NAUTILUS_CALL_PARENT_CLASS (GTK_OBJECT_CLASS, destroy, (object));
}

static int
real_get_number_of_columns (FMListView *view)
{
	return 7;
}

static char *
get_sort_attribute_from_search_criterion (const char *criterion)
{
	if (criterion != NULL) {
		if (strcmp (criterion, NAUTILUS_SEARCH_URI_TEXT_NAME) == 0) {
			return g_strdup ("name");
		} else if (strcmp (criterion, NAUTILUS_SEARCH_URI_TEXT_TYPE) == 0) {
			return g_strdup ("type");
		} else if (strcmp (criterion, NAUTILUS_SEARCH_URI_TEXT_SIZE) == 0) {
			return g_strdup ("size");
		} else if (strcmp (criterion, NAUTILUS_SEARCH_URI_TEXT_EMBLEMS) == 0) {
			return g_strdup ("emblems");
		} else if (strcmp (criterion, NAUTILUS_SEARCH_URI_TEXT_DATE_MODIFIED) == 0) {
			return g_strdup ("date_modified");
		}
	}

	return NULL;
}

static char *
real_get_default_sort_attribute (FMListView *view)
{
	char *uri;
	char *criterion;
	char *sort_attribute;

	uri = fm_directory_view_get_uri (FM_DIRECTORY_VIEW (view));
	criterion = nautilus_search_uri_get_first_criterion (uri);
	g_free (uri);
	sort_attribute = get_sort_attribute_from_search_criterion (criterion);

	/* Default to "name" if we're using some unknown search criterion, or
	 * search criterion that doesn't correspond to any column.
	 */
	if (sort_attribute == NULL) {
		return g_strdup ("name");
	}

	return sort_attribute;
}

static NautilusStringList *
real_get_emblem_names_to_exclude (FMDirectoryView *view)
{
	g_assert (FM_IS_DIRECTORY_VIEW (view));

	/* Overridden to show even the trash emblem here */
	return NULL;
}

static int
real_get_link_column (FMListView *view)
{
	return 2;
}

static void
real_get_column_specification (FMListView *view,
			       int column_number,
			       FMListViewColumn *specification)
{
	switch (column_number) {
	case 0:
		fm_list_view_column_set (specification,
					 "icon", NULL,
					 NAUTILUS_FILE_SORT_BY_TYPE,
					 0, 0, 0, FALSE);
		break;
	case 1:
		fm_list_view_column_set (specification,
					 "emblems", NULL,
					 NAUTILUS_FILE_SORT_BY_EMBLEMS,
					 20, 20, 300, FALSE);
		break;
	case 2:
		fm_list_view_column_set (specification,
					 "name", _("Name"),
					 NAUTILUS_FILE_SORT_BY_NAME,
					 30, 150, 300, FALSE);
		break;
	case 3:
		fm_list_view_column_set (specification,
					 "parent_uri", _("Where"),
					 NAUTILUS_FILE_SORT_BY_DIRECTORY,
					 30, 120, 500, FALSE);
		break;
	case 4:
		fm_list_view_column_set (specification,
					 "size", _("Size"),
					 NAUTILUS_FILE_SORT_BY_SIZE,
					 20, 55, 80, TRUE);
		break;
	case 5:
		fm_list_view_column_set (specification,
					 "type", _("Type"),
					 NAUTILUS_FILE_SORT_BY_TYPE,
					 20, 75, 200, FALSE);
		break;
	case 6:
		fm_list_view_column_set (specification,
					 "date_modified", _("Date Modified"),
					 NAUTILUS_FILE_SORT_BY_MTIME,
					 30, 75, 200, FALSE);
		break;
	default:
		g_assert_not_reached ();
	}
}

static void
indexing_info_callback (BonoboUIComponent *component, gpointer data, const char *verb)
{
	nautilus_indexing_info_show_dialog ();
}

static void
compute_reveal_item_name_and_sensitivity (GList *selected_files,
					  char **return_name_with_underscore,
					  char **return_name_no_underscore,
					  gboolean *return_sensitivity)
{
	char *name_with_underscore;
	int count;

	g_assert (return_name_with_underscore != NULL || return_name_no_underscore != NULL);
	g_assert (return_sensitivity != NULL);

	count = g_list_length (selected_files);
	if (count <= 1) {
		/* "Reveal in New Window" means open the parent folder for the
		 * selected item in a new window, select the item in that window,
		 * and scroll as necessary to make that item visible (this comment
		 * is to inform translators of this tricky concept).
		 */
		name_with_underscore = g_strdup (_("_Reveal in New Window"));
	} else {
		/* "Reveal in n New Windows" means open the parent folder for each
		 * selected item in a separate new window, select each selected
		 * item in its new window, and scroll as necessary to make those 
		 * items visible (this comment is to inform translators of this 
		 * tricky concept).
		 */
		name_with_underscore = g_strdup_printf (_("Reveal in %d _New Windows"), count);
	}

	*return_sensitivity = selected_files != NULL;
	
        if (return_name_no_underscore != NULL) {
        	*return_name_no_underscore = nautilus_str_strip_chr (name_with_underscore, '_');
        }

        if (return_name_with_underscore != NULL) {
		*return_name_with_underscore = name_with_underscore;
        } else {
		g_free (name_with_underscore);
        }
}

static void
real_add_file (FMDirectoryView *view, NautilusFile *file)
{
	char *fake_file_name;
	char *real_file_uri;
	NautilusFile *real_file;
	
	g_return_if_fail (FM_IS_SEARCH_LIST_VIEW (view));

	/* Get the real file that the funky search symbolic link file 
	 * refers to. The real file name is hacked into the name of the
	 * search results virtual file.
	 */
	fake_file_name = nautilus_file_get_name (file);
	real_file_uri = gnome_vfs_unescape_string (fake_file_name, NULL);
	real_file = nautilus_file_get (real_file_uri);

	/* Tell the normal list-view code to add this file. It will add
	 * and ref it only if it's not already in the list.
	 */ 
	NAUTILUS_CALL_PARENT_CLASS 
		(FM_DIRECTORY_VIEW_CLASS, add_file, (view, real_file));

	g_free (fake_file_name);
	g_free (real_file_uri);
	nautilus_file_unref (real_file);
}

static void
real_adding_file (FMListView *view, NautilusFile *file)
{
	GList *attributes;

	g_assert (FM_IS_SEARCH_LIST_VIEW (view));
	g_assert (NAUTILUS_IS_FILE (file));

	NAUTILUS_CALL_PARENT_CLASS (FM_LIST_VIEW_CLASS, adding_file, (view, file));

	/* FIXME: this implies that positioning, custom icon, icon
	 * stretching, etc, will be based on the real directory the file is in,
	 * and won't be specific to the search directory. Is that OK? 
	 */

	gtk_signal_connect_object (GTK_OBJECT (file),
				   "changed",
				   fm_directory_view_queue_file_change,
				   GTK_OBJECT (view));
	/* Monitor the things needed to get the right
	 * icon. Also monitor a directory's item count because
	 * the "size" attribute is based on that, and the file's metadata.  */
	attributes = nautilus_icon_factory_get_required_file_attributes ();		
	attributes = g_list_prepend (attributes,
				     NAUTILUS_FILE_ATTRIBUTE_DIRECTORY_ITEM_COUNT);
	attributes = g_list_prepend (attributes,
				     NAUTILUS_FILE_ATTRIBUTE_METADATA);
	attributes = g_list_prepend (attributes, 
				     NAUTILUS_FILE_ATTRIBUTE_MIME_TYPE);
	nautilus_file_monitor_add (file, view, attributes);
	g_list_free (attributes);
}

static void
real_removing_file (FMListView *view, NautilusFile *file)
{
	g_assert (FM_IS_SEARCH_LIST_VIEW (view));
	g_assert (NAUTILUS_IS_FILE (file));

	nautilus_file_monitor_remove (file, view);
	gtk_signal_disconnect_by_func 
		(GTK_OBJECT (file), fm_directory_view_queue_file_change, view);
	NAUTILUS_CALL_PARENT_CLASS (FM_LIST_VIEW_CLASS, removing_file, (view, file));
}

static gboolean
real_file_still_belongs (FMListView *view, NautilusFile *file)
{
	g_assert (FM_IS_SEARCH_LIST_VIEW (view));
	g_assert (NAUTILUS_IS_FILE (file));

	return !nautilus_file_is_gone (file);
}

static void
update_reveal_item (FMSearchListView *view)
{
	GList *selected_files;
	char *label_with_underscore, *label_no_underscore;
	gboolean sensitive;

	g_assert (FM_IS_DIRECTORY_VIEW (view));

	selected_files = fm_directory_view_get_selection (FM_DIRECTORY_VIEW (view));

	compute_reveal_item_name_and_sensitivity 
		(selected_files, &label_with_underscore, &label_no_underscore, &sensitive);

	nautilus_bonobo_set_sensitive (view->details->ui, 
				       COMMAND_REVEAL_IN_NEW_WINDOW, 
				       sensitive);
	nautilus_bonobo_set_label (view->details->ui, COMMAND_REVEAL_IN_NEW_WINDOW, label_no_underscore);					       
	nautilus_bonobo_set_label (view->details->ui, MENU_PATH_REVEAL_IN_NEW_WINDOW, label_with_underscore);

	g_free (label_with_underscore);
	g_free (label_no_underscore);

        nautilus_file_list_free (selected_files);
}

static void
real_merge_menus (FMDirectoryView *view)
{
	FMSearchListView *search_view;
	BonoboUIVerb verbs [] = {
		BONOBO_UI_VERB ("Indexing Info", indexing_info_callback),
		BONOBO_UI_VERB ("Reveal", reveal_selected_items_callback),
		BONOBO_UI_VERB_END
	};

	NAUTILUS_CALL_PARENT_CLASS (FM_DIRECTORY_VIEW_CLASS, merge_menus, (view));

	search_view = FM_SEARCH_LIST_VIEW (view);

	search_view->details->ui = bonobo_ui_component_new ("Search List View");
	bonobo_ui_component_set_container (search_view->details->ui,
					   fm_directory_view_get_bonobo_ui_container (view));
	bonobo_ui_util_set_ui (search_view->details->ui,
			       DATADIR,
			       "nautilus-search-list-view-ui.xml",
			       "nautilus");
	bonobo_ui_component_add_verb_list_with_data (search_view->details->ui, verbs, view);
}

static gboolean
real_supports_creating_files (FMDirectoryView *view)
{
	/* The user is not allowed to modify the contents of a search
	 * results view.
	 */
	return FALSE;
}

static gboolean
real_accepts_dragged_files (FMDirectoryView *view)
{
	/* The user is not allowed to modify the contents of a search
	 * results view.
	 */
	return FALSE;
}

static gboolean
real_supports_properties (FMDirectoryView *view)
{
	/* Disable "Show Properties" menu item in this view, because changing
	 * properties could cause the item to no longer match the search
	 * criteria. Eventually we might want to solve this a different way,
	 * perhaps by showing items that don't match the search criteria any
	 * more a different way.
	 */
	return FALSE;
}

static void
real_update_menus (FMDirectoryView *view)
{
	g_assert (FM_IS_SEARCH_LIST_VIEW (view));

	NAUTILUS_CALL_PARENT_CLASS (FM_DIRECTORY_VIEW_CLASS, update_menus, (view));

	update_reveal_item (FM_SEARCH_LIST_VIEW (view));
}

static void
reveal_selected_items_callback (BonoboUIComponent *component, gpointer user_data, const char *verb)
{
	FMDirectoryView *directory_view;
	char *parent_uri;
	NautilusFile *file;
	GList *file_as_list;
	GList *selection;
	GList *node;

	g_assert (FM_IS_SEARCH_LIST_VIEW (user_data));

	directory_view = FM_DIRECTORY_VIEW (user_data);

	selection = fm_directory_view_get_selection (directory_view);

	if (fm_directory_view_confirm_multiple_windows (directory_view, g_list_length (selection))) {
		for (node = selection; node != NULL; node = node->next) {
			file = NAUTILUS_FILE (node->data);
			parent_uri = nautilus_file_get_parent_uri (file);
			if (parent_uri != NULL) {
				file_as_list = g_list_prepend (NULL, nautilus_file_get_uri (file));
				nautilus_view_open_location_in_new_window
					(fm_directory_view_get_nautilus_view (directory_view), 
					 parent_uri,
					 file_as_list);
				nautilus_g_list_free_deep (file_as_list);
			}
			g_free (parent_uri);
		}
	}	

	nautilus_file_list_free (selection);
}
