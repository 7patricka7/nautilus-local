/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* 
 * Copyright (C) 2000 Eazel, Inc
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Authors: Eskil Heyn Olsen <eskil@eazel.com>
 *
 */

#include <config.h>
#include <gnome.h>
#include "eazel-package-system-private.h"
#include <libtrilobite/trilobite-core-distribution.h>

enum {
	START,
	END,
	PROGRESS,
	FAILED,
	LAST_SIGNAL
};

/* FIXME bugzilla.eazel.com 4852 
   This extern is to be removed when 4582 is fixed */
extern EazelPackageSystemConstructorFunc eazel_package_system_implementation;

/* The signal array, used for building the signal bindings */
static guint signals[LAST_SIGNAL] = { 0 };
/* This is the parent class pointer */
static GtkObjectClass *eazel_package_system_parent_class;

/*****************************************/

EazelPackageSystemId 
eazel_package_system_suggest_id ()
{
	EazelPackageSystemId  result = EAZEL_PACKAGE_SYSTEM_UNSUPPORTED;
	DistributionInfo dist = trilobite_get_distribution ();

	switch (dist.name) {
	case DISTRO_REDHAT: 
		if (dist.version_major == 6) {
			result = EAZEL_PACKAGE_SYSTEM_RPM_3;
		} else {
			result = EAZEL_PACKAGE_SYSTEM_RPM_4;
		}
		break;
	case DISTRO_MANDRAKE: 
	case DISTRO_YELLOWDOG:
	case DISTRO_TURBOLINUX: 
		result = EAZEL_PACKAGE_SYSTEM_RPM_3;
		break;
/* FIXME bugzilla.eazel.com 4853
   straighten out which distro uses which package system,
   and what version uses which rpm_x ? */
	case DISTRO_CALDERA: 
	case DISTRO_SUSE: 
	case DISTRO_LINUXPPC: 
	case DISTRO_COREL: 
	case DISTRO_DEBIAN: 
		result = EAZEL_PACKAGE_SYSTEM_DEB;
		break;
	case DISTRO_UNKNOWN:
		result = EAZEL_PACKAGE_SYSTEM_UNSUPPORTED;
		break;
	}
	return result;		
}

static EazelPackageSystem*
eazel_package_system_load_implementation (EazelPackageSystemId id, GList *roots)
{
	EazelPackageSystem *result;
	EazelPackageSystemConstructorFunc const_func;

	/* FIXME bugzilla.eazel.com 4852
	   - id to string
	   - lookup library using string (in some config file)
           - g_module_open library and get the const_func
	   - call const_func to get the object */

	/* The constructor function will be a function called
	   eazel_package_system_implementation in the .so file.
	   So for now, I link against the rpm implementation and
	   simply assign this function to const_func and call it */

	const_func = (EazelPackageSystemConstructorFunc)&eazel_package_system_implementation;

	result = (*const_func)(roots);

	return result;
}

gboolean             
eazel_package_system_is_installed (EazelPackageSystem *package_system,
				   const char *dbpath,
				   const char *name,
				   const char *version,
				   const char *minor)
{
	GList *matches;
	gboolean result = FALSE;
	
	matches = eazel_package_system_query (package_system,
					      dbpath,
					      (const gpointer)name,
					      EAZEL_PACKAGE_SYSTEM_QUERY_MATCHES,
					      PACKAGE_FILL_MINIMAL);

	if (matches) {
		if (version || minor) {
			GList *iterator;

			for (iterator = matches; iterator && !result; iterator = g_list_next (iterator)) {
				PackageData *pack = (PackageData*)iterator->data;
				if (eazel_install_package_matches_versioning (pack, version, minor)) {
					result = TRUE;
				}
			}
		} else {
			result = TRUE;
		}
		g_list_foreach (matches, (GFunc)packagedata_destroy, GINT_TO_POINTER (TRUE));
	}
	g_list_free (matches);

	return result;
}

PackageData*
eazel_package_system_load_package (EazelPackageSystem *system,
				   PackageData *in_package,
				   const char *filename,
				   unsigned long detail_level)
{
	PackageData *result = NULL;
	EPS_SANE_VAL (system, NULL);
	g_assert (system->private->load_package);
	result = (*system->private->load_package) (system, in_package, filename, detail_level);
	return result;
}

GList*               
eazel_package_system_query (EazelPackageSystem *system,
			    const char *root,
			    const gpointer key,
			    EazelPackageSystemQueryEnum flag,
			    unsigned long detail_level)
{
	GList *result = NULL;
	EPS_SANE_VAL (system, NULL);
	g_assert (system->private->query);
	result = (*system->private->query) (system, root, key, flag, detail_level);
	return result;
}

void                 
eazel_package_system_install (EazelPackageSystem *system, 
			      const char *root,
			      GList* packages,
			      unsigned long flags)
{
	EPS_SANE (system);
	g_assert (system->private->install);
	(*system->private->install) (system, root, packages, flags);
}

void                 
eazel_package_system_uninstall (EazelPackageSystem *system, 
				const char *root,
				GList* packages,
				unsigned long flags)
{
	EPS_SANE (system);
	g_assert (system->private->uninstall);
	(*system->private->uninstall) (system, root, packages, flags);
}

void                 
eazel_package_system_verify (EazelPackageSystem *system, 
			     const char *root,
			     GList* packages,
			     unsigned long flags)
{
	EPS_SANE (system);
	g_assert (system->private->verify);
	(*system->private->verify) (system, root, packages, flags);
}

/******************************************
 The private emitter functions
*******************************************/

gboolean 
eazel_package_system_emit_start (EazelPackageSystem *system, 
				 EazelPackageSystemOperation op, 
				 const PackageData *package)
{
	gboolean result = TRUE;
	EPS_API (system);
	gtk_signal_emit (GTK_OBJECT (system),
			 signals [START],
			 op, 
			 package,
			 &result);
	return result;
}

gboolean 
eazel_package_system_emit_progress (EazelPackageSystem *system, 
				    EazelPackageSystemOperation op, 
				    unsigned long info[EAZEL_PACKAGE_SYSTEM_PROGRESS_LONGS],
				    const PackageData *package)
{
	gboolean result = TRUE;
	int infos;
	unsigned long *infoblock;

	EPS_API (system);
	infoblock = g_new0 (unsigned long, EAZEL_PACKAGE_SYSTEM_PROGRESS_LONGS+1);
	for (infos = 0; infos < EAZEL_PACKAGE_SYSTEM_PROGRESS_LONGS; infos++) {
		infoblock[infos] = info[infos];
	}

	gtk_signal_emit (GTK_OBJECT (system),
			 signals [PROGRESS],
			 op, 
			 infoblock,
			 package,
			 &result);

	g_free (infoblock);
	return result;
}

gboolean 
eazel_package_system_emit_failed (EazelPackageSystem *system, 
				  EazelPackageSystemOperation op, 
				  const PackageData *package)
{
	gboolean result = TRUE;
	EPS_API (system);

	gtk_signal_emit (GTK_OBJECT (system),
			 signals [FAILED],
			 op, 
			 package,
			 &result);

	return result;
}

gboolean 
eazel_package_system_emit_end (EazelPackageSystem *system, 
			       EazelPackageSystemOperation op, 
			       const PackageData *package)
{
	gboolean result = TRUE;
	EPS_API (system);

	gtk_signal_emit (GTK_OBJECT (system),
			 signals [END],
			 op, 
			 package,
			 &result);

	return result;
}

EazelPackageSystemDebug 
eazel_package_system_get_debug (EazelPackageSystem *system)
{
	if (system->private) {
		return system->private->debug;
	} else {
		return 0;
	}
}

void                    
eazel_package_system_set_debug (EazelPackageSystem *system, 
				EazelPackageSystemDebug d)
{
	EPS_API (system);
	system->private->debug = d;
}

/*****************************************
  GTK+ object stuff
*****************************************/

static void
eazel_package_system_finalize (GtkObject *object)
{
	EazelPackageSystem *system;

	g_return_if_fail (object != NULL);
	g_return_if_fail (EAZEL_PACKAGE_SYSTEM (object));

	system = EAZEL_PACKAGE_SYSTEM (object);

	if (GTK_OBJECT_CLASS (eazel_package_system_parent_class)->finalize) {
		GTK_OBJECT_CLASS (eazel_package_system_parent_class)->finalize (object);
	}
}

static void
eazel_package_system_class_initialize (EazelPackageSystemClass *klass) 
{
	GtkObjectClass *object_class;

	object_class = (GtkObjectClass*)klass;
	object_class->finalize = eazel_package_system_finalize;
	
	eazel_package_system_parent_class = gtk_type_class (gtk_object_get_type ());

	signals[START] = 
		gtk_signal_new ("start",
				GTK_RUN_LAST,
				object_class->type,
				GTK_SIGNAL_OFFSET (EazelPackageSystemClass, start),
				eazel_package_system_marshal_BOOL__ENUM_POINTER,
				GTK_TYPE_BOOL, 2, 
				GTK_TYPE_ENUM, GTK_TYPE_POINTER);	
	signals[END] = 
		gtk_signal_new ("end",
				GTK_RUN_LAST,
				object_class->type,
				GTK_SIGNAL_OFFSET (EazelPackageSystemClass, end),
				eazel_package_system_marshal_BOOL__ENUM_POINTER,
				GTK_TYPE_BOOL, 2, 
				GTK_TYPE_ENUM, GTK_TYPE_POINTER);	
	signals[PROGRESS] = 
		gtk_signal_new ("progress",
				GTK_RUN_LAST,
				object_class->type,
				GTK_SIGNAL_OFFSET (EazelPackageSystemClass, progress),
				eazel_package_system_marshal_BOOL__ENUM_POINTER_POINTER,
				GTK_TYPE_BOOL, 3, 
				GTK_TYPE_ENUM, GTK_TYPE_POINTER, GTK_TYPE_POINTER);
	signals[FAILED] = 
		gtk_signal_new ("failed",
				GTK_RUN_LAST,
				object_class->type,
				GTK_SIGNAL_OFFSET (EazelPackageSystemClass, failed),
				eazel_package_system_marshal_BOOL__ENUM_POINTER,
				GTK_TYPE_BOOL, 2, 
				GTK_TYPE_ENUM, GTK_TYPE_POINTER);	
	gtk_object_class_add_signals (object_class, signals, LAST_SIGNAL);

	klass->start = NULL;
	klass->progress = NULL;
	klass->failed = NULL;
	klass->end = NULL;
}

static void
eazel_package_system_initialize (EazelPackageSystem *system) {
	g_assert (system!=NULL); 
	g_assert (IS_EAZEL_PACKAGE_SYSTEM (system));
	
	system->private = g_new0 (EazelPackageSystemPrivate, 1);
}

GtkType
eazel_package_system_get_type() {
	static GtkType system_type = 0;

	/* First time it's called ? */
	if (!system_type)
	{
		static const GtkTypeInfo system_info =
		{
			"EazelPackageSystem",
			sizeof (EazelPackageSystem),
			sizeof (EazelPackageSystemClass),
			(GtkClassInitFunc) eazel_package_system_class_initialize,
			(GtkObjectInitFunc) eazel_package_system_initialize,
			/* reserved_1 */ NULL,
			/* reserved_2 */ NULL,
			(GtkClassInitFunc) NULL,
		};

		system_type = gtk_type_unique (gtk_object_get_type (), &system_info);
	}

	return system_type;
}

/* 
   This is the real constructor 
*/
EazelPackageSystem *
eazel_package_system_new_real ()
{
	EazelPackageSystem *system;

	system = EAZEL_PACKAGE_SYSTEM (gtk_object_new (TYPE_EAZEL_PACKAGE_SYSTEM, NULL));

	gtk_object_ref (GTK_OBJECT (system));
	gtk_object_sink (GTK_OBJECT (system));

	return system;
}

/* 
   This lets the user create a packagesystem with a specific 
   id 
*/
EazelPackageSystem *
eazel_package_system_new_with_id (EazelPackageSystemId id, GList *roots)
{
	return eazel_package_system_load_implementation (id, roots);
}

/*
  Autodetect distribution and creates
  an instance of a EazelPackageSystem with the appropriate
  type
 */
EazelPackageSystem *
eazel_package_system_new (GList *roots) 
{
	return eazel_package_system_new_with_id (eazel_package_system_suggest_id (), roots);
}

/* Marshal functions */

typedef gboolean (*GtkSignal_BOOL__ENUM_POINTER_POINTER) (GtkObject *object,
							  gint arg1,
							  gpointer arg2,
							  gpointer arg3,
							  gpointer user_data);

void eazel_package_system_marshal_BOOL__ENUM_POINTER_POINTER (GtkObject *object,
							      GtkSignalFunc func,
							      gpointer func_data, 
							      GtkArg *args)
{
	GtkSignal_BOOL__ENUM_POINTER_POINTER rfunc;
	gboolean *result;

	result = GTK_RETLOC_BOOL (args[3]);
	rfunc = (GtkSignal_BOOL__ENUM_POINTER_POINTER)func;
	(*result) = (*rfunc) (object,
			      GTK_VALUE_ENUM (args[0]),
			      GTK_VALUE_POINTER (args[1]),
			      GTK_VALUE_POINTER (args[2]),
			      func_data);
}

typedef gboolean (*GtkSignal_BOOL__ENUM_POINTER) (GtkObject *object,
						  gint arg1,
						  gpointer arg2,
						  gpointer user_data);

void eazel_package_system_marshal_BOOL__ENUM_POINTER (GtkObject *object,
						      GtkSignalFunc func,
						      gpointer func_data, 
						      GtkArg *args)
{
	GtkSignal_BOOL__ENUM_POINTER rfunc;
	gboolean *result;
	
	rfunc = (GtkSignal_BOOL__ENUM_POINTER)func;
	result = GTK_RETLOC_BOOL (args[2]);
	(*result) = (*rfunc) (object,
			      GTK_VALUE_ENUM (args[0]),
			      GTK_VALUE_POINTER (args[1]),
			      func_data);
}
