#include <errno.h>
#include <liboaf/liboaf.h>
#include <gnome.h>
#include <bonobo.h>
#include <zvt/zvtterm.h>

#include <popt.h>
extern struct poptOption oaf_popt_options[];

static BonoboObject *
shell_factory (BonoboGenericFactory *Factory, void *closure)
{
    BonoboControl	*control;
    GtkWidget		*zvtterm;
    int			pid;

    /* Create the control. */
    zvtterm = zvt_term_new_with_size (80, 24);
    gtk_widget_show (zvtterm);

    pid = zvt_term_forkpty (ZVT_TERM (zvtterm), 0);
    if (pid == 0) {
      execl ("/bin/bash", "-bash", NULL);
      fprintf (stderr, "ERROR: %s\n", strerror (errno));
      exit (1);
    }

    control = bonobo_control_new (zvtterm);

    return BONOBO_OBJECT (control);
}

void
shell_factory_init (void)
{
    static BonoboGenericFactory *xterm_factory = NULL;

    if (xterm_factory != NULL)
	return;

    xterm_factory = bonobo_generic_factory_new
	("OAFIID:shell_factory:10a7d344-c4cd-402f-9e05-bd591bbc5618",
	 shell_factory, NULL);

    if (xterm_factory == NULL)
	g_error ("I could not register a Factory.");
}

CORBA_Environment ev;
CORBA_ORB orb;

static void
init_bonobo (int argc, char **argv)
{
    gnome_init_with_popt_table ("xterm-control-factory", "0.0",
				argc, argv, oaf_popt_options, 0, NULL);
    orb = oaf_init (argc, argv);

    if (bonobo_init (orb, NULL, NULL) == FALSE)
	g_error (_("Could not initialize Bonobo"));
}

int
main (int argc, char **argv)
{
    CORBA_exception_init (&ev);

    init_bonobo (argc, argv);

    shell_factory_init ();

    bonobo_main ();

    return 0;
}
