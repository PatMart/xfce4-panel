/*  $Id$
 *
 *  Copyright (C) 2002-2004 Jasper Huijsmans (jasper@xfce.org)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Library General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
*/

/*  item_dialog.c
 *  -------------
 *  There are two types of configuration dialogs: for panel items and for 
 *  menu items.
 *
 *  1) Dialog for changing items on the panel. This is now defined in 
 *  controls_dialog.c. Only icon items use code from this file to 
 *  present their options. This code is partially shared with menu item
 *  dialogs.
 *  
 *  2) Dialogs for changing or adding menu items.
 *  Basically the same as for icon panel items. Addtional options for 
 *  caption and position in menu.
 *
*/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gmodule.h>
#include <libxfce4util/libxfce4util.h>

#include "xfce.h"
#include "item.h"
#include "item-control.h"
#include "item_dialog.h"
#include "settings.h"

#define BORDER          6
#define PREVIEW_SIZE    32

typedef struct _ItemDialog ItemDialog;

struct _ItemDialog
{
    Control *control;
    Item *item;

    GtkContainer *container;
    GtkWidget *close;

    /* command */
    CommandOptions *cmd_opts;

    /* icon */
    IconOptions *icon_opts;

    /* Name and Tooltip */
    GtkWidget *caption_entry;
    GtkWidget *tip_entry;

    /* menu */
    GtkWidget *menu_checkbutton;
};

/* globals */
static GtkWidget *menudialog = NULL;	/* keep track of this for signal 
					   handling */

static GtkTargetEntry target_entry[] = {
    {"text/uri-list", 0, 0},
    {"UTF8_STRING", 0, 1},
    {"STRING", 0, 2}
};

static const char *keys[] = {
    "Name",
    "GenericName",
    "Comment",
    "Icon",
    "Categories",
    "OnlyShowIn",
    "Exec",
    "Terminal"
};

/* useful widgets */

static void
add_spacer (GtkBox * box, int size)
{
    GtkWidget *align;

    align = gtk_alignment_new (0, 0, 0, 0);
    gtk_widget_set_size_request (align, size, size);
    gtk_widget_show (align);
    gtk_box_pack_start (box, align, FALSE, FALSE, 0);
}


/* CommandOptions 
 * --------------
*/

static void
command_browse_cb (GtkWidget * w, CommandOptions * opts)
{
    char *file;
    const char *text;

    text = gtk_entry_get_text (GTK_ENTRY (opts->command_entry));

    file = select_file_name (_("Select command"), text,
			     gtk_widget_get_toplevel (opts->base));

    if (file)
    {
        gtk_entry_set_text (GTK_ENTRY (opts->command_entry), file);

	gtk_editable_set_position (GTK_EDITABLE (opts->command_entry), -1);

	if (opts->on_change)
	{
	    GtkToggleButton *tb;
	    gboolean in_term, use_sn = FALSE;

	    tb = GTK_TOGGLE_BUTTON (opts->term_checkbutton);
	    in_term = gtk_toggle_button_get_active (tb);

	    if (opts->sn_checkbutton)
	    {
		tb = GTK_TOGGLE_BUTTON (opts->sn_checkbutton);
		use_sn = gtk_toggle_button_get_active (tb);
	    }


	    opts->on_change (file, in_term, use_sn, opts->data);
	}

	g_free (file);
    }
}

static void
command_toggle_cb (GtkWidget * w, CommandOptions * opts)
{
    if (opts->on_change)
    {
	GtkToggleButton *tb;
	gboolean in_term, use_sn = FALSE;
	const char *cmd;

	cmd = gtk_entry_get_text (GTK_ENTRY (opts->command_entry));

	if (!cmd || !strlen (cmd))
	    cmd = NULL;

	tb = GTK_TOGGLE_BUTTON (opts->term_checkbutton);
	in_term = gtk_toggle_button_get_active (tb);

	if (opts->sn_checkbutton)
	{
	    tb = GTK_TOGGLE_BUTTON (opts->sn_checkbutton);
	    use_sn = gtk_toggle_button_get_active (tb);
	}

	opts->on_change (cmd, in_term, use_sn, opts->data);
    }
}

/* Drag and drop URI callback (Appfinder' stuff) */
/* TODO: 
 * - also add icon terminal checkbutton and tooltip (Comment) from 
 *   .desktop file 
 * - allow drop on window (no way to find window currently though...) 
 */
static void
drag_drop_cb (GtkWidget * widget, GdkDragContext * context, gint x,
	      gint y, GtkSelectionData * sd, guint info,
	      guint time, CommandOptions * opts)
{
    if (sd->data)
    {
        char *exec = NULL;
        char *buf, *s;
        XfceDesktopEntry *dentry;

        s = (char *) sd->data;

        if (!strncmp (s, "file", 5))
        {
            s += 5;

            if (!strncmp (s, "//", 2))
                s += 2;
        }

        buf = g_strdup (s);

        if ((s = strchr (buf, '\n')))
            *s = '\0';
        
	if (g_file_test (buf, G_FILE_TEST_EXISTS) &&
	    (dentry = xfce_desktop_entry_new (buf, keys, G_N_ELEMENTS (keys))))
	{ 
            xfce_desktop_entry_get_string (dentry, "Exec", FALSE, &exec);
            g_object_unref (dentry);
            g_free (buf);
	}
        else
        {
            exec = buf;
        }

        if (exec)
        {
            if ((s = g_strrstr (exec, " %")) != NULL)
            {
                s[0] = '\0';
            }
            
            command_options_set_command (opts, exec, FALSE, FALSE);
            g_free (exec);
        }
    }

    gtk_drag_finish (context, TRUE, FALSE, time);
}

G_MODULE_EXPORT /* EXPORT:create_command_options */
CommandOptions *
create_command_options (GtkSizeGroup * sg)
{
    GtkWidget *w, *vbox, *hbox, *image;
    CommandOptions *opts;

    if (!sg)
	sg = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);

    opts = g_new0 (CommandOptions, 1);

    opts->base = vbox = gtk_vbox_new (FALSE, 0);
    gtk_widget_show (vbox);

    /* entry */
    hbox = gtk_hbox_new (FALSE, BORDER);
    gtk_widget_show (hbox);
    gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, TRUE, 0);

    w = gtk_label_new (_("Command:"));
    gtk_misc_set_alignment (GTK_MISC (w), 0, 0.5);
    gtk_widget_show (w);
    gtk_box_pack_start (GTK_BOX (hbox), w, FALSE, FALSE, 0);

    gtk_size_group_add_widget (sg, w);

    opts->command_entry = w = gtk_entry_new ();

    gtk_widget_show (w);
    gtk_box_pack_start (GTK_BOX (hbox), w, TRUE, TRUE, 0);

    w = gtk_button_new ();
    gtk_widget_show (w);
    gtk_box_pack_start (GTK_BOX (hbox), w, FALSE, FALSE, 0);

    image = gtk_image_new_from_stock (GTK_STOCK_OPEN, GTK_ICON_SIZE_BUTTON);
    gtk_widget_show (image);
    gtk_container_add (GTK_CONTAINER (w), image);

    g_signal_connect (w, "clicked", G_CALLBACK (command_browse_cb), opts);

    /* xfce4-appfinder support (desktop files / menu spec) */
    gtk_drag_dest_set (opts->command_entry, GTK_DEST_DEFAULT_ALL, 
                       target_entry, G_N_ELEMENTS (target_entry), 
                       GDK_ACTION_COPY);
    g_signal_connect (opts->command_entry, "drag-data-received",
		      G_CALLBACK (drag_drop_cb), opts);

    gtk_drag_dest_set (opts->base, GTK_DEST_DEFAULT_ALL, target_entry, 
                       G_N_ELEMENTS (target_entry), GDK_ACTION_COPY);
    g_signal_connect (opts->base, "drag-data-received",
		      G_CALLBACK (drag_drop_cb), opts);

    hbox = gtk_hbox_new (FALSE, BORDER);
    gtk_widget_show (hbox);
    gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, TRUE, 0);

    w = gtk_label_new ("");
    gtk_widget_show (w);
    gtk_box_pack_start (GTK_BOX (hbox), w, FALSE, FALSE, 0);

    gtk_size_group_add_widget (sg, w);

    /* terminal */
    opts->term_checkbutton = w =
	gtk_check_button_new_with_mnemonic (_("Run in _terminal"));
    gtk_widget_show (w);
    gtk_box_pack_start (GTK_BOX (hbox), w, FALSE, FALSE, 0);

    g_signal_connect (w, "toggled", G_CALLBACK (command_toggle_cb), opts);

#ifdef HAVE_LIBSTARTUP_NOTIFICATION
    hbox = gtk_hbox_new (FALSE, BORDER);
    gtk_widget_show (hbox);
    gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, TRUE, 0);

    w = gtk_label_new ("");
    gtk_widget_show (w);
    gtk_box_pack_start (GTK_BOX (hbox), w, FALSE, FALSE, 0);

    gtk_size_group_add_widget (sg, w);

    opts->sn_checkbutton = w =
	gtk_check_button_new_with_mnemonic (_("Use startup _notification"));
    gtk_widget_show (w);
    gtk_box_pack_start (GTK_BOX (hbox), w, FALSE, FALSE, 0);

    g_signal_connect (w, "toggled", G_CALLBACK (command_toggle_cb), opts);
#endif

    g_signal_connect_swapped (opts->base, "destroy",
			      G_CALLBACK (destroy_command_options), opts);

    return opts;
}

G_MODULE_EXPORT /* EXPORT:destroy_command_options */
void
destroy_command_options (CommandOptions * opts)
{
    if (opts->on_change)
    {
	GtkToggleButton *tb;
	gboolean in_term, use_sn = FALSE;
	const char *cmd;

	cmd = gtk_entry_get_text (GTK_ENTRY (opts->command_entry));

	if (!cmd || !strlen (cmd))
	    cmd = NULL;

	tb = GTK_TOGGLE_BUTTON (opts->term_checkbutton);
	in_term = gtk_toggle_button_get_active (tb);

	if (opts->sn_checkbutton)
	{
	    tb = GTK_TOGGLE_BUTTON (opts->sn_checkbutton);
	    use_sn = gtk_toggle_button_get_active (tb);
	}

	opts->on_change (cmd, in_term, use_sn, opts->data);
    }

    g_free (opts);
}

G_MODULE_EXPORT /* EXPORT:command_options_set_command */
void
command_options_set_command (CommandOptions * opts, const char *command,
			     gboolean in_term, gboolean use_sn)
{
    const char *cmd = (command != NULL) ? command : "";

    gtk_entry_set_text (GTK_ENTRY (opts->command_entry), cmd);

    gtk_editable_set_position (GTK_EDITABLE (opts->command_entry), -1);

    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (opts->term_checkbutton),
				  in_term);

    if (opts->sn_checkbutton)
    {
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON
				      (opts->sn_checkbutton), use_sn);
    }

    if (opts->on_change)
	opts->on_change (command, in_term, use_sn, opts->data);
}

G_MODULE_EXPORT /* EXPORT:command_options_set_callback */
void
command_options_set_callback (CommandOptions * opts,
			      void (*callback) (const char *, gboolean,
						gboolean, gpointer),
			      gpointer data)
{
    opts->on_change = callback;
    opts->data = data;
}

G_MODULE_EXPORT /* EXPORT:command_options_get_command */
void
command_options_get_command (CommandOptions * opts, char **command,
			     gboolean * in_term, gboolean * use_sn)
{
    const char *tmp;
    GtkToggleButton *tb;

    tmp = gtk_entry_get_text (GTK_ENTRY (opts->command_entry));

    if (tmp && strlen (tmp))
	*command = g_strdup (tmp);
    else
	*command = NULL;

    tb = GTK_TOGGLE_BUTTON (opts->term_checkbutton);
    *in_term = gtk_toggle_button_get_active (tb);

    if (opts->sn_checkbutton)
    {
	tb = GTK_TOGGLE_BUTTON (opts->sn_checkbutton);
	*use_sn = gtk_toggle_button_get_active (tb);
    }
    else
    {
	*use_sn = FALSE;
    }
}


/* IconOptions
 * -----------
*/

static void
update_icon_preview (int id, const char *path, IconOptions * opts)
{
    int w, h;
    GdkPixbuf *pb = NULL;

    if (id == EXTERN_ICON)
    {
        if (path)
            pb = xfce_themed_icon_load (path, PREVIEW_SIZE);
    }
    else
    {
	pb = get_pixbuf_by_id (id);
    }

    if (!pb)
	pb = get_pixbuf_by_id (UNKNOWN_ICON);

    w = gdk_pixbuf_get_width (pb);
    h = gdk_pixbuf_get_height (pb);

    if (w > PREVIEW_SIZE || h > PREVIEW_SIZE)
    {
	GdkPixbuf *newpb;

	if (w > h)
	{
	    h = (int) (((double) PREVIEW_SIZE / (double) w) * (double) h);
	    w = PREVIEW_SIZE;
	}
	else
	{
	    w = (int) (((double) PREVIEW_SIZE / (double) h) * (double) w);
	    h = PREVIEW_SIZE;
	}

	newpb = gdk_pixbuf_scale_simple (pb, w, h, GDK_INTERP_BILINEAR);
	g_object_unref (pb);
	pb = newpb;
    }

    gtk_image_set_from_pixbuf (GTK_IMAGE (opts->image), pb);
    g_object_unref (pb);
}

static void
icon_id_changed (GtkOptionMenu * om, IconOptions * opts)
{
    int id;
    char *path = NULL;

    id = gtk_option_menu_get_history (om);

    if (id == 0)
	id = EXTERN_ICON;

    if (id == opts->icon_id)
	return;

    if (id == EXTERN_ICON && opts->saved_path)
	path = opts->saved_path;

    icon_options_set_icon (opts, id, path);
}

static GtkWidget *
create_icon_option_menu (void)
{
    GtkWidget *om, *menu, *mi;
    int i;

    menu = gtk_menu_new ();

    mi = gtk_menu_item_new_with_label (_("Other Icon"));
    gtk_widget_show (mi);
    gtk_menu_shell_append (GTK_MENU_SHELL (menu), mi);

    for (i = 1; i < NUM_ICONS; i++)
    {
	mi = gtk_menu_item_new_with_label (icon_names[i]);
	gtk_widget_show (mi);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), mi);
    }

    om = gtk_option_menu_new ();
    gtk_option_menu_set_menu (GTK_OPTION_MENU (om), menu);

    return om;
}

G_MODULE_EXPORT /* EXPORT:icon_entry_lost_focus */
gboolean
icon_entry_lost_focus (GtkEntry * entry, GdkEventFocus * event,
		       IconOptions * opts)
{
    const char *temp = gtk_entry_get_text (entry);

    if (temp)
	icon_options_set_icon (opts, EXTERN_ICON, temp);

    /* we must return FALSE or gtk will crash :-( */
    return FALSE;
}

static void
icon_browse_cb (GtkWidget * w, IconOptions * opts)
{
    char *file;
    const char *text;
    GdkPixbuf *test = NULL;

    text = gtk_entry_get_text (GTK_ENTRY (opts->icon_entry));

    file = select_file_with_preview (NULL, text,
				     gtk_widget_get_toplevel (opts->base));

    if (file && g_file_test (file, G_FILE_TEST_EXISTS) &&
	!g_file_test (file, G_FILE_TEST_IS_DIR))
    {
	test = gdk_pixbuf_new_from_file (file, NULL);

	if (test)
	{
	    g_object_unref (test);

	    icon_options_set_icon (opts, EXTERN_ICON, file);
	}
    }

    g_free (file);
}

static void
xtm_cb (GtkWidget * b, GtkEntry * entry)
{
    gchar *argv[2] = { "xfmime-edit", NULL };

    g_spawn_async (NULL, argv, NULL, G_SPAWN_SEARCH_PATH,
		   NULL, NULL, NULL, NULL);
}

static void
icon_drop_cb (GtkWidget * widget, GdkDragContext * context,
	      gint x, gint y, GtkSelectionData * data,
	      guint info, guint time, IconOptions * opts)
{
    GList *fnames;
    guint count;

    fnames = gnome_uri_list_extract_filenames ((char *) data->data);
    count = g_list_length (fnames);

    if (count > 0)
    {
	char *icon;
	GdkPixbuf *test;

	icon = (char *) fnames->data;

	test = gdk_pixbuf_new_from_file (icon, NULL);

	if (test)
	{
	    icon_options_set_icon (opts, EXTERN_ICON, icon);
	    g_object_unref (test);
	}
    }

    gnome_uri_list_free_strings (fnames);
    gtk_drag_finish (context, (count > 0),
		     (context->action == GDK_ACTION_MOVE), time);
}

static GtkWidget *
create_icon_preview_frame (IconOptions * opts)
{
    GtkWidget *frame;
    GtkWidget *eventbox;

    frame = gtk_frame_new (NULL);
    gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_OUT);
    gtk_widget_show (frame);

    eventbox = gtk_event_box_new ();
    add_tooltip (eventbox, _("Drag file onto this frame to change the icon"));
    gtk_widget_show (eventbox);
    gtk_container_add (GTK_CONTAINER (frame), eventbox);

    opts->image = gtk_image_new ();
    gtk_widget_show (opts->image);
    gtk_container_add (GTK_CONTAINER (eventbox), opts->image);

    /* signals */
    dnd_set_drag_dest (eventbox);

    g_signal_connect (eventbox, "drag_data_received",
		      G_CALLBACK (icon_drop_cb), opts);

    return frame;
}

G_MODULE_EXPORT /* EXPORT:create_icon_options */
IconOptions *
create_icon_options (GtkSizeGroup * sg, gboolean use_builtins)
{
    GtkWidget *w, *vbox, *hbox, *image;
    IconOptions *opts;

    opts = g_new0 (IconOptions, 1);

    opts->base = hbox = gtk_hbox_new (FALSE, BORDER);
    gtk_widget_show (hbox);

    /* image preview */
    w = create_icon_preview_frame (opts);
    gtk_widget_show (w);
    gtk_box_pack_start (GTK_BOX (hbox), w, FALSE, TRUE, 0);

    if (sg)
	gtk_size_group_add_widget (sg, w);

    vbox = gtk_vbox_new (FALSE, BORDER);
    gtk_widget_show (vbox);
    gtk_box_pack_start (GTK_BOX (hbox), vbox, TRUE, TRUE, 0);

    /* icon option menu */
    if (use_builtins)
    {
	opts->icon_menu = w = create_icon_option_menu ();
	gtk_widget_show (w);
	gtk_box_pack_start (GTK_BOX (vbox), w, TRUE, TRUE, 0);

	opts->id_sig = g_signal_connect (w, "changed",
					 G_CALLBACK (icon_id_changed), opts);
    }

    /* icon entry */
    opts->icon_entry = w = gtk_entry_new ();
    gtk_widget_show (w);

    if (use_builtins)
    {
	hbox = gtk_hbox_new (FALSE, BORDER);
	gtk_widget_show (hbox);
	gtk_box_pack_start (GTK_BOX (vbox), hbox, TRUE, TRUE, 0);

	gtk_box_pack_start (GTK_BOX (hbox), w, TRUE, TRUE, 0);
    }
    else
    {
	gtk_box_pack_start (GTK_BOX (vbox), w, TRUE, TRUE, 0);

	hbox = gtk_hbox_new (FALSE, BORDER);
	gtk_widget_show (hbox);
	gtk_box_pack_start (GTK_BOX (vbox), hbox, TRUE, TRUE, 0);
    }

    g_signal_connect (w, "focus-out-event",
		      G_CALLBACK (icon_entry_lost_focus), opts);

    w = gtk_button_new ();
    gtk_widget_show (w);
    gtk_box_pack_start (GTK_BOX (hbox), w, FALSE, FALSE, 0);

    image = gtk_image_new_from_stock (GTK_STOCK_OPEN, GTK_ICON_SIZE_BUTTON);
    gtk_widget_show (image);
    gtk_container_add (GTK_CONTAINER (w), image);

    g_signal_connect (w, "clicked", G_CALLBACK (icon_browse_cb), opts);

    /* use xfmime-edit when available */
    {
	gchar *g = g_find_program_in_path ("xfmime-edit");

	if (g)
	{
	    GtkWidget *xtm_button = gtk_button_new ();

	    gtk_box_pack_start (GTK_BOX (hbox), xtm_button, FALSE, FALSE, 0);
	    gtk_widget_show (xtm_button);

	    image =
		gtk_image_new_from_stock (GTK_STOCK_SELECT_COLOR,
					  GTK_ICON_SIZE_BUTTON);
	    gtk_widget_show (image);
	    gtk_container_add (GTK_CONTAINER (xtm_button), image);

	    g_signal_connect (xtm_button, "clicked",
			      G_CALLBACK (xtm_cb), opts);
	    g_free (g);
	}
    }

    g_signal_connect_swapped (opts->base, "destroy",
			      G_CALLBACK (destroy_icon_options), opts);

    return opts;
}

G_MODULE_EXPORT /* EXPORT:destroy_icon_options */
void
destroy_icon_options (IconOptions * opts)
{
    if (opts->on_change)
    {
	int id;
	const char *icon_path = NULL;

	id = gtk_option_menu_get_history (GTK_OPTION_MENU (opts->icon_menu));

	if (id == 0)
	{
	    id = EXTERN_ICON;

	    icon_path = gtk_entry_get_text (GTK_ENTRY (opts->icon_entry));

	    if (!icon_path || !strlen (icon_path))
		icon_path = NULL;
	}

	opts->on_change (id, icon_path, opts->data);
    }

    g_free (opts->saved_path);

    g_free (opts);
}

G_MODULE_EXPORT /* EXPORT:icon_options_set_icon */
void
icon_options_set_icon (IconOptions * opts, int id, const char *path)
{
    const char *icon_path = NULL;

    if (opts->icon_id == EXTERN_ICON)
        icon_path = gtk_entry_get_text (GTK_ENTRY (opts->icon_entry));

    if (id == opts->icon_id)
    {
        if (id != EXTERN_ICON)
        {
            return;
        }
        else if (path && icon_path && 
                 strlen(icon_path) && !strcmp(path,icon_path))
        {
            return;
        }
    }
    
    g_signal_handler_block (opts->icon_menu, opts->id_sig);
    gtk_option_menu_set_history (GTK_OPTION_MENU (opts->icon_menu),
				 (id == EXTERN_ICON) ? 0 : id);
    g_signal_handler_unblock (opts->icon_menu, opts->id_sig);

    if (id == EXTERN_ICON || id == UNKNOWN_ICON)
    {
        if (path)
        {
            gtk_entry_set_text (GTK_ENTRY (opts->icon_entry), path);
            gtk_editable_set_position (GTK_EDITABLE (opts->icon_entry), -1);
        }

	gtk_widget_set_sensitive (opts->icon_entry, TRUE);
    }
    else
    {
	if (icon_path && strlen (icon_path))
	{
	    g_free (opts->saved_path);
	    opts->saved_path = g_strdup (icon_path);
	}

	gtk_entry_set_text (GTK_ENTRY (opts->icon_entry), "");
	gtk_widget_set_sensitive (opts->icon_entry, FALSE);
    }

    opts->icon_id = id;

    update_icon_preview (id, path, opts);

    if (opts->on_change)
	opts->on_change (id, path, opts->data);
}

G_MODULE_EXPORT /* EXPORT:icon_options_set_callback */
void
icon_options_set_callback (IconOptions * opts,
			   void (*callback) (int, const char *, gpointer),
			   gpointer data)
{
    opts->on_change = callback;
    opts->data = data;
}

G_MODULE_EXPORT /* EXPORT:icon_options_get_icon */
void
icon_options_get_icon (IconOptions * opts, int *id, char **path)
{
    *id = gtk_option_menu_get_history (GTK_OPTION_MENU (opts->icon_menu));

    *path = NULL;

    if (*id == 0)
    {
	const char *icon_path;

	*id = EXTERN_ICON;

	icon_path = gtk_entry_get_text (GTK_ENTRY (opts->icon_entry));

	if (icon_path && strlen (icon_path))
	    *path = g_strdup (icon_path);
    }
}

/* ItemDialog 
 * ----------
*/

static void
icon_options_changed (int id, const char *path, gpointer data)
{
    ItemDialog *idlg = data;
    Item *item = idlg->item;

    item->icon_id = id;

    g_free (item->icon_path);

    if (path)
	item->icon_path = g_strdup (path);
    else
	item->icon_path = NULL;

    item_apply_config (item);
}

static inline void
add_command_options (GtkBox * box, ItemDialog * idlg, GtkSizeGroup * sg)
{
    idlg->cmd_opts = create_command_options (sg);

    command_options_set_command (idlg->cmd_opts, idlg->item->command,
				 idlg->item->in_terminal, idlg->item->use_sn);

    gtk_box_pack_start (box, idlg->cmd_opts->base, FALSE, TRUE, 0);
}

static inline void
add_icon_options (GtkBox * box, ItemDialog * idlg, GtkSizeGroup * sg)
{
    idlg->icon_opts = create_icon_options (sg, TRUE);
    
    /* TODO: This check should probably be in item.c */
    if (idlg->item->icon_id == 0)
        idlg->item->icon_id = EXTERN_ICON;

    icon_options_set_icon (idlg->icon_opts, idlg->item->icon_id,
			   idlg->item->icon_path);

    icon_options_set_callback (idlg->icon_opts, icon_options_changed, idlg);

    gtk_box_pack_start (box, idlg->icon_opts->base, FALSE, TRUE, 0);
}

static gboolean
caption_entry_lost_focus (GtkWidget * entry, GdkEventFocus * event,
			  Item * item)
{
    const char *tmp;

    tmp = gtk_entry_get_text (GTK_ENTRY (entry));

    if (tmp && strlen (tmp))
    {
	g_free (item->caption);
	item->caption = g_strdup (tmp);

	item_apply_config (item);
    }

    /* we must return FALSE or gtk will crash :-( */
    return FALSE;
}

static inline void
add_caption_option (GtkBox * box, ItemDialog * idlg, GtkSizeGroup * sg)
{
    GtkWidget *hbox;
    GtkWidget *label;

    hbox = gtk_hbox_new (FALSE, BORDER);
    gtk_widget_show (hbox);
    gtk_box_pack_start (box, hbox, FALSE, TRUE, 0);

    label = gtk_label_new (_("Caption:"));
    gtk_misc_set_alignment (GTK_MISC (label), 0, 0.5);
    gtk_size_group_add_widget (sg, label);
    gtk_widget_show (label);
    gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, FALSE, 0);

    idlg->caption_entry = gtk_entry_new ();
    gtk_widget_show (idlg->caption_entry);
    gtk_box_pack_start (GTK_BOX (hbox), idlg->caption_entry, TRUE, TRUE, 0);

    if (idlg->item->caption)
	gtk_entry_set_text (GTK_ENTRY (idlg->caption_entry),
			    idlg->item->caption);

    /* only set label on focus out */
    g_signal_connect (idlg->caption_entry, "focus-out-event",
		      G_CALLBACK (caption_entry_lost_focus), idlg->item);
}

static inline void
add_tooltip_option (GtkBox * box, ItemDialog * idlg, GtkSizeGroup * sg)
{
    GtkWidget *hbox;
    GtkWidget *label;

    hbox = gtk_hbox_new (FALSE, BORDER);
    gtk_widget_show (hbox);
    gtk_box_pack_start (box, hbox, FALSE, TRUE, 0);

    label = gtk_label_new (_("Tooltip:"));
    gtk_misc_set_alignment (GTK_MISC (label), 0, 0.5);
    gtk_size_group_add_widget (sg, label);
    gtk_widget_show (label);
    gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, FALSE, 0);

    idlg->tip_entry = gtk_entry_new ();
    gtk_widget_show (idlg->tip_entry);
    gtk_box_pack_start (GTK_BOX (hbox), idlg->tip_entry, TRUE, TRUE, 0);

    if (idlg->item->tooltip)
	gtk_entry_set_text (GTK_ENTRY (idlg->tip_entry), idlg->item->tooltip);
}

static void
popup_menu_changed (GtkToggleButton * tb, Control * control)
{
    control->with_popup = gtk_toggle_button_get_active (tb);

    item_control_show_popup (control, control->with_popup);
}

static inline void
add_menu_option (GtkBox * box, ItemDialog * idlg, GtkSizeGroup * sg)
{
    idlg->menu_checkbutton =
	gtk_check_button_new_with_label (_("Attach menu to launcher"));
    gtk_widget_show (idlg->menu_checkbutton);
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (idlg->menu_checkbutton),
				  idlg->item->with_popup);
    gtk_box_pack_start (box, idlg->menu_checkbutton, FALSE, FALSE, 0);

    g_signal_connect (idlg->menu_checkbutton, "toggled",
		      G_CALLBACK (popup_menu_changed), idlg->control);
}

static void
item_dialog_closed (GtkWidget * w, ItemDialog * idlg)
{
    Item *item;
    const char *temp;

    item = idlg->item;

    /* command */
    g_free (item->command);
    item->command = NULL;

    command_options_get_command (idlg->cmd_opts, &(item->command),
				 &(item->in_terminal), &(item->use_sn));

    /* icon */
    g_free (item->icon_path);
    item->icon_path = NULL;

    icon_options_get_icon (idlg->icon_opts, &(item->icon_id),
			   &(item->icon_path));

    /* tooltip */
    g_free (item->tooltip);
    item->tooltip = NULL;

    temp = gtk_entry_get_text (GTK_ENTRY (idlg->tip_entry));

    if (temp && *temp)
	item->tooltip = g_strdup (temp);

    if (item->type == MENUITEM)
    {
	/* caption */
	g_free (item->caption);
	item->caption = NULL;

	temp = gtk_entry_get_text (GTK_ENTRY (idlg->caption_entry));

	if (temp && *temp)
	    item->caption = g_strdup (temp);
    }

    item_apply_config (item);
}

static void
destroy_item_dialog (ItemDialog * idlg)
{
    /* TODO: check if these are still here */
    command_options_set_callback (idlg->cmd_opts, NULL, NULL);
    icon_options_set_callback (idlg->icon_opts, NULL, NULL);

    g_free (idlg);
}

G_MODULE_EXPORT /* EXPORT:reate_item_dialog */
void
create_item_dialog (Control *control, Item * item, GtkContainer * container, 
                    GtkWidget * close)
{
    ItemDialog *idlg;
    GtkWidget *vbox;
    GtkSizeGroup *sg;

    xfce_textdomain (GETTEXT_PACKAGE, LOCALEDIR, "UTF-8");

    idlg = g_new0 (ItemDialog, 1);

    idlg->control = control;
    idlg->item = item;
    
    vbox = gtk_vbox_new (FALSE, BORDER);
    gtk_widget_show (vbox);
    gtk_container_add (container, vbox);

    sg = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);

    add_icon_options (GTK_BOX (vbox), idlg, sg);
    add_spacer (GTK_BOX (vbox), BORDER);

    add_command_options (GTK_BOX (vbox), idlg, sg);
    add_spacer (GTK_BOX (vbox), BORDER);

    if (!control)
    {
	add_caption_option (GTK_BOX (vbox), idlg, sg);
    }

    add_tooltip_option (GTK_BOX (vbox), idlg, sg);

    if (control)
    {
	add_spacer (GTK_BOX (vbox), BORDER);
	add_menu_option (GTK_BOX (vbox), idlg, sg);
    }

    add_spacer (GTK_BOX (vbox), BORDER);

    g_signal_connect (close, "clicked",
		      G_CALLBACK (item_dialog_closed), idlg);

    g_signal_connect_swapped (container, "destroy",
			      G_CALLBACK (destroy_item_dialog), idlg);
}

/* panel and menu item API 
 * -----------------------
*/

/* menu item */

static void
pos_changed (GtkSpinButton * spin, Item * item)
{
    int n = gtk_spin_button_get_value_as_int (spin) - 1;
    PanelPopup *pp = item->parent;

    if (n == item->pos)
	return;

    panel_popup_move_item (pp, item, n);
}

static void
add_position_option (GtkBox * box, Item * item, int num_items)
{
    GtkWidget *hbox;
    GtkWidget *label;
    GtkWidget *pos_spin;

    g_return_if_fail (num_items > 1);

    hbox = gtk_hbox_new (FALSE, BORDER);
    gtk_container_set_border_width (GTK_CONTAINER (hbox), BORDER);
    gtk_widget_show (hbox);
    gtk_box_pack_start (box, hbox, FALSE, FALSE, 0);

    label = gtk_label_new (_("Position:"));
    gtk_misc_set_alignment (GTK_MISC (label), 0, 0.5);
    gtk_widget_show (label);
    gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, FALSE, 0);

    pos_spin = gtk_spin_button_new_with_range (1, num_items, 1);
    gtk_widget_show (pos_spin);
    gtk_box_pack_start (GTK_BOX (hbox), pos_spin, FALSE, FALSE, 0);

    gtk_spin_button_set_value (GTK_SPIN_BUTTON (pos_spin),
			       (gfloat) item->pos + 1);

    g_signal_connect (pos_spin, "value-changed", G_CALLBACK (pos_changed),
		      item);
}

static gboolean
menu_dialog_delete (GtkWidget * dlg)
{
    gtk_dialog_response (GTK_DIALOG (dlg), GTK_RESPONSE_OK);

    return TRUE;
}

G_MODULE_EXPORT /* EXPORT:edit_menu_item_dialog */
void
edit_menu_item_dialog (Item * mi)
{
    GtkWidget *remove, *close, *header, *vbox;
    GtkDialog *dlg;
    int response, num_items;

    xfce_textdomain (GETTEXT_PACKAGE, LOCALEDIR, "UTF-8");

    menudialog = gtk_dialog_new ();
    dlg = GTK_DIALOG (menudialog);

    gtk_window_set_title (GTK_WINDOW (dlg), _("Change menu item"));
    gtk_window_set_position (GTK_WINDOW (dlg), GTK_WIN_POS_CENTER);

    gtk_dialog_set_has_separator (dlg, FALSE);

    /* add buttons */
    remove = xfce_create_mixed_button (GTK_STOCK_REMOVE, _("_Remove"));
    gtk_widget_show (remove);
    gtk_dialog_add_action_widget (dlg, remove, GTK_RESPONSE_CANCEL);

    close = gtk_button_new_from_stock (GTK_STOCK_CLOSE);
    GTK_WIDGET_SET_FLAGS (close, GTK_CAN_DEFAULT);
    gtk_widget_show (close);
    gtk_dialog_add_action_widget (dlg, close, GTK_RESPONSE_OK);

    gtk_button_box_set_child_secondary (GTK_BUTTON_BOX (dlg->action_area),
					remove, TRUE);

    header = xfce_create_header (NULL, _("Launcher"));
    gtk_container_set_border_width (GTK_CONTAINER (GTK_BIN (header)->child),
				    BORDER);
    gtk_widget_set_size_request (header, -1, 32);
    gtk_widget_show (header);
    gtk_box_pack_start (GTK_BOX (dlg->vbox), header, FALSE, TRUE, 0);

    add_spacer (GTK_BOX (dlg->vbox), BORDER);

    /* position */
    num_items = panel_popup_get_n_items (mi->parent);

    if (num_items > 1)
    {
	add_position_option (GTK_BOX (dlg->vbox), mi, num_items);

	add_spacer (GTK_BOX (dlg->vbox), BORDER);
    }

    vbox = gtk_vbox_new (FALSE, BORDER);
    gtk_widget_show (vbox);
    gtk_container_set_border_width (GTK_CONTAINER (vbox), BORDER - 1);
    gtk_box_pack_start (GTK_BOX (dlg->vbox), vbox, FALSE, FALSE, 0);
    
    /* add the other options */
    create_item_dialog (NULL, mi, GTK_CONTAINER (vbox), close);

    add_spacer (GTK_BOX (dlg->vbox), BORDER);

    g_signal_connect (dlg, "delete-event", G_CALLBACK (menu_dialog_delete),
		      NULL);

    gtk_widget_grab_focus (close);
    gtk_widget_grab_default (close);

    response = GTK_RESPONSE_NONE;
    response = gtk_dialog_run (dlg);

    if (response == GTK_RESPONSE_CANCEL)
    {
	PanelPopup *pp = mi->parent;

        panel_popup_remove_item (pp, mi);
    }

    gtk_widget_destroy (menudialog);

    menudialog = NULL;

    write_panel_config ();
}

G_MODULE_EXPORT /* EXPORT:add_menu_item_dialog */
void
add_menu_item_dialog (PanelPopup * pp)
{
    Item *mi = menu_item_new (pp);

    create_menu_item (mi);
    mi->pos = 0;

    panel_popup_add_item (pp, mi);

    edit_menu_item_dialog (mi);
}

G_MODULE_EXPORT /* EXPORT:destroy_menu_dialog */
void
destroy_menu_dialog (void)
{
    if (menudialog)
	gtk_dialog_response (GTK_DIALOG (menudialog), GTK_RESPONSE_OK);
}
