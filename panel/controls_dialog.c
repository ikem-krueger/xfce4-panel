/*  controls_dialog.h
 *  
 *  Copyright (C) 2002 Jasper Huijsmans (huysmans@users.sourceforge.net)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
*/

/*  panel control dialog
 *  --------------------
 *  The dialog consists of three parts:
 *  - Option menu to choose the type of control (icon or one of the available
 *    modules);
 *  - Notebook containing the options that can be changed. This is provided
 *    by the panel controls. Changes must auto-apply if possible. 
 *  - Buttons: 'Revert' and 'Done'
 *    
 *  Important data are kept as global variables for easy access.
*/

#include <config.h>
#include <my_gettext.h>

#include "xfce.h"
#include "controls_dialog.h"
#include "groups.h"
#include "settings.h"

static GSList *control_list = NULL;       /* list of control controles */

static GtkWidget *container;            /* container on the panel to hold the 
				           panel control */
static Control *old_control = NULL;  	/* original panel control */
static Control *current_control = NULL; /* current control == old_control, 
					   if type is not changed */
static GtkWidget *type_option_menu;
static GtkWidget *pos_spin;
static GtkWidget *notebook;
static GtkWidget *done;
static GtkWidget *revert;

static int backup_index;

/* control control list */
static void create_control_list(Control * control)
{
    int i;
    GSList *li, *class_list;
    
    class_list = get_control_class_list();

    /* first the original control */
    control_list = g_slist_append(control_list, control);

    /* then one for each other control class */
    for (i = 0, li = class_list;  li; li = li->next, i++)
    {
	ControlClass *cc = li->data;
	Control *new_control;

	if (cc == control->cclass)
	    continue;

	new_control = control_new(control->index);
	new_control->cclass = cc;
	cc->create_control(new_control);

	control_attach_callbacks(new_control);
	control_set_settings(new_control);

	control_list = g_slist_append(control_list, new_control);
    }
}

static void clear_control_list(void)
{
    GSList *li;
    
    /* first remove the current control */
    control_list = g_slist_remove(control_list, current_control);

    for (li = control_list; li; li = li->next)
    {
	Control *control = li->data;

	control_free(control);
    }

    g_slist_free(control_list);    
    control_list = NULL;
}

/*  Type options menu
 *  -----------------
*/
static void type_option_changed(GtkOptionMenu * om)
{
    int n = gtk_option_menu_get_history(om);
    GSList *li;
    Control *control = NULL;

    li = g_slist_nth(control_list, n);
    control = li->data;

    if (control == current_control)
	return;
    
    control_unpack(current_control);
    control_pack(control, GTK_BOX(container));
    
    control->index = current_control->index;
    groups_register_control(control); 
    
    container = control->base->parent;
    current_control = control;

    gtk_notebook_set_current_page(GTK_NOTEBOOK(notebook), n);

    gtk_widget_set_sensitive(revert, TRUE);
}

static GtkWidget *create_type_option_menu(void)
{
    GtkWidget *om;
    GtkWidget *menu, *mi;
    GSList *li;
    Control *control;

    om = gtk_option_menu_new();
    menu = gtk_menu_new();

    for(li = control_list; li; li = li->next)
    {
        control = li->data;

        mi = gtk_menu_item_new_with_label(control->cclass->caption);
        gtk_widget_show(mi);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi);
    }

    gtk_option_menu_set_menu(GTK_OPTION_MENU(om), menu);
    
    g_signal_connect(om, "changed", G_CALLBACK(type_option_changed), NULL);

    return om;
}

static void add_notebook(GtkBox * box)
{
    GSList *li;
    GtkWidget *frame;

    notebook = gtk_notebook_new();
    gtk_widget_show(notebook);

    gtk_notebook_set_show_tabs(GTK_NOTEBOOK(notebook), FALSE);
    gtk_notebook_set_show_border(GTK_NOTEBOOK(notebook), FALSE);

    /* add page for every control in control_list */
    for (li = control_list; li; li = li->next)
    {
	Control *control = li->data;

	frame = gtk_frame_new(NULL);
	gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_NONE);
	gtk_container_set_border_width(GTK_CONTAINER(frame), 4);
	gtk_widget_show(frame);
	gtk_notebook_append_page(GTK_NOTEBOOK(notebook), frame, NULL);

	control_add_options(control, GTK_CONTAINER(frame), 
			    revert, done);
    }

    gtk_box_pack_start(box, notebook, TRUE, TRUE, 0);
}

/*  The main dialog
 *  ---------------
*/
static void pos_changed(GtkSpinButton *spin)
{
    int n;
    gboolean changed = FALSE;

    n = gtk_spin_button_get_value_as_int(spin) - 1;
    
    if (n != current_control->index)
    {
	groups_move(current_control->index, n);
	current_control->index = n;

	changed = TRUE;
    }

    if (changed)
	gtk_widget_set_sensitive(revert, TRUE);
}

static void controls_dialog_revert(void)
{
    gtk_option_menu_set_history(GTK_OPTION_MENU(type_option_menu), 0);

    if (backup_index != current_control->index)
    {
	groups_move(current_control->index, backup_index);

	gtk_spin_button_set_value(GTK_SPIN_BUTTON(pos_spin), 
				  backup_index+1);
    }
}

enum { RESPONSE_DONE, RESPONSE_REVERT, RESPONSE_REMOVE };

void controls_dialog(Control * control)
{
    GtkWidget *dlg;
    GtkWidget *button;
    GtkWidget *vbox;
    GtkWidget *hbox;
    GtkWidget *label;
    GtkWidget *separator;
    GtkWidget *main_vbox;
    int response;
    GtkSizeGroup *sg = gtk_size_group_new(GTK_SIZE_GROUP_HORIZONTAL);

    old_control = current_control = control;
    backup_index = control->index;

    /* Keep track of the panel container */
    container = control->base->parent;

    dlg = gtk_dialog_new_with_buttons(_("Change item"), GTK_WINDOW(toplevel),
                                      GTK_DIALOG_MODAL, NULL);

    gtk_window_set_position(GTK_WINDOW(dlg), GTK_WIN_POS_CENTER);

    button = gtk_button_new_from_stock(GTK_STOCK_REMOVE);
    gtk_widget_show(button);
    gtk_dialog_add_action_widget(GTK_DIALOG(dlg), button, RESPONSE_REMOVE);

    revert = button = mixed_button_new(GTK_STOCK_UNDO, _("_Revert"));
    gtk_widget_show(button);
    gtk_dialog_add_action_widget(GTK_DIALOG(dlg), button, RESPONSE_REVERT);

    done = button = mixed_button_new(GTK_STOCK_OK, _("_Done"));
    gtk_widget_show(button);
    gtk_dialog_add_action_widget(GTK_DIALOG(dlg), button, RESPONSE_DONE);
    GTK_WIDGET_SET_FLAGS(button, GTK_CAN_DEFAULT);
    
    g_signal_connect(revert, "clicked", 
	    	     G_CALLBACK(controls_dialog_revert), NULL);
    
    main_vbox = GTK_DIALOG(dlg)->vbox;

    vbox = gtk_vbox_new(FALSE, 7);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 8);
    gtk_widget_show(vbox);
    gtk_box_pack_start(GTK_BOX(main_vbox), vbox, FALSE, FALSE, 0);
    
    /* find all available controls */
    create_control_list(control);

    /* option menu */
    hbox = gtk_hbox_new(FALSE, 8);
    gtk_widget_show(hbox);

    label = gtk_label_new(_("Type:"));
    gtk_misc_set_alignment(GTK_MISC(label), 0, 0.5);
    gtk_widget_show(label);
    gtk_size_group_add_widget(sg, label);
    gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);

    type_option_menu = create_type_option_menu();
    gtk_widget_show(type_option_menu);
    gtk_box_pack_start(GTK_BOX(hbox), type_option_menu, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);
    
    /* position */
    hbox = gtk_hbox_new(FALSE, 8);
    gtk_widget_show(hbox);

    label = gtk_label_new(_("Position:"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.1, 0.5);
    gtk_widget_show(label);
    gtk_size_group_add_widget(sg, label);
    gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);

    pos_spin = gtk_spin_button_new_with_range(1, settings.num_groups, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(pos_spin), backup_index+1);
    gtk_widget_show(pos_spin);
    gtk_box_pack_start(GTK_BOX(hbox), pos_spin, FALSE, FALSE, 0);
    
    g_signal_connect(pos_spin, "value-changed", G_CALLBACK(pos_changed), NULL);

    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);
    
    /* separator */
    separator = gtk_hseparator_new();
    gtk_widget_show(separator);
    gtk_box_pack_start(GTK_BOX(main_vbox), separator, FALSE, FALSE, 0);

    /* notebook */
    add_notebook(GTK_BOX(main_vbox));

    /* run dialog until 'Done' */
    while(1)
    {
        response = GTK_RESPONSE_NONE;

        gtk_widget_set_sensitive(revert, FALSE);
	gtk_widget_grab_default(done);
	gtk_widget_grab_focus(done);

        response = gtk_dialog_run(GTK_DIALOG(dlg));

	if (response == RESPONSE_REMOVE)
	{
	    gtk_widget_hide(dlg);
	    
	    if (!(control->with_popup) || 
		confirm(_("Removing an item will also remove its popup menu.\n\n"
			  "Do you want to remove the item?"), 
			GTK_STOCK_REMOVE, NULL))
	    {
		break;
	    }
	    
	    gtk_widget_show(dlg);
	}
	else if (response != RESPONSE_REVERT)
	{
	    break;
	}
    }
    
    gtk_widget_destroy(dlg);

    clear_control_list();

    if (response == RESPONSE_REMOVE)
    {
	groups_remove(current_control->index);
    }

    write_panel_config();
}

