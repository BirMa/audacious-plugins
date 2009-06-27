/*
 * Audacious ALSA Plugin (-ng)
 * Copyright (c) 2009 William Pitcock <nenolod@dereferenced.org>
 * Portions copyright (C) 2001-2003 Matthieu Sozeau <mattam@altern.org>
 * Portions copyright (C) 2003-2005 Haavard Kvaalen
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include "alsa-stdinc.h"
#include <stdio.h>

#include <gtk/gtk.h>
#include <glib/gi18n.h>

static GtkWidget *configure_win = NULL;
static GtkWidget *devices_combo, *mixer_devices_combo;

static gint current_mixer_card;

gint alsaplug_mixer_new_for_card(snd_mixer_t **mixer, const gchar *card);

#define GET_TOGGLE(tb) gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(tb))
#define GET_CHARS(edit) gtk_editable_get_chars(GTK_EDITABLE(edit), 0, -1)

static void configure_win_ok_cb(GtkWidget * w, gpointer data)
{
	g_free(alsaplug_cfg.pcm_device);
	alsaplug_cfg.pcm_device = GET_CHARS(GTK_COMBO(devices_combo)->entry);
	alsaplug_cfg.mixer_card = g_strdup_printf("hw:%d", current_mixer_card);
	alsaplug_cfg.mixer_device = GET_CHARS(GTK_COMBO(mixer_devices_combo)->entry);

	gtk_widget_destroy(configure_win);

	/* Save configuration */
	mcs_handle_t *cfgfile = aud_cfg_db_open();
	aud_cfg_db_set_string(cfgfile, "alsaplug", "pcm_device", alsaplug_cfg.pcm_device);
	aud_cfg_db_set_string(cfgfile, "alsaplug", "mixer_card", alsaplug_cfg.mixer_card);
	aud_cfg_db_set_string(cfgfile, "alsaplug","mixer_device", alsaplug_cfg.mixer_device);
	aud_cfg_db_close(cfgfile);
}

void alsaplug_get_config(void)
{
	mcs_handle_t *cfgfile = aud_cfg_db_open();
	aud_cfg_db_get_string(cfgfile, "alsaplug", "pcm_device", &alsaplug_cfg.pcm_device);
	aud_cfg_db_get_string(cfgfile, "alsaplug", "mixer_card", &alsaplug_cfg.mixer_card);
	aud_cfg_db_get_string(cfgfile, "alsaplug","mixer_device", &alsaplug_cfg.mixer_device);
	aud_cfg_db_close(cfgfile);
}

static gint get_cards(GtkOptionMenu *omenu, GtkSignalFunc cb)
{
	GtkWidget *menu, *item;
	gint card = -1, err, set = 0, curr = -1;

	menu = gtk_menu_new();
	if ((err = snd_card_next(&card)) != 0)
		g_warning("snd_next_card() failed: %s", snd_strerror(err));

	while (card > -1)
	{
		gchar *label;

		curr++;
		if ((err = snd_card_get_name(card, &label)) != 0)
		{
			g_warning("snd_carg_get_name() failed: %s",
				  snd_strerror(err));
			break;
		}

		item = gtk_menu_item_new_with_label(label);
		gtk_signal_connect(GTK_OBJECT(item), "activate", cb,
				   GINT_TO_POINTER(card));
		gtk_widget_show(item);
		gtk_menu_append(GTK_MENU(menu), item);
		if ((err = snd_card_next(&card)) != 0)
		{
			g_warning("snd_next_card() failed: %s",
				  snd_strerror(err));
			break;
		}
	}

	gtk_option_menu_set_menu(omenu, menu);
	return set;
}

static gint get_mixer_devices(GtkCombo *combo, const gchar *card)
{
	GList *items = NULL;
	gint err;
	snd_mixer_t *mixer;
	snd_mixer_elem_t *current;

	if ((err = alsaplug_mixer_new_for_card(&mixer, card)) < 0)
		return err;

	current = snd_mixer_first_elem(mixer);

	while (current)
	{
		if (snd_mixer_selem_is_active(current) &&
		    snd_mixer_selem_has_playback_volume(current))
		{
			const gchar *sname = snd_mixer_selem_get_name(current);
			gint index = snd_mixer_selem_get_index(current);
			if (index)
				items = g_list_append(items, g_strdup_printf("%s,%d", sname, index));
			else
				items = g_list_append(items, g_strdup(sname));
		}
		current = snd_mixer_elem_next(current);
	}

	gtk_combo_set_popdown_strings(combo, items);

	return 0;
}

static void get_devices_for_card(GtkCombo *combo, gint card)
{
	GtkWidget *item;
	gint pcm_device = -1, err;
	snd_pcm_info_t *pcm_info = NULL;
	snd_ctl_t *ctl;
	gchar dev[64], *card_name;

	sprintf(dev, "hw:%i", card);

	if ((err = snd_ctl_open(&ctl, dev, 0)) < 0)
	{
		printf("snd_ctl_open() failed: %s", snd_strerror(err));
		return;
	}

	if ((err = snd_card_get_name(card, &card_name)) != 0)
	{
		g_warning("snd_card_get_name() failed: %s", snd_strerror(err));
		card_name = _("Unknown soundcard");
	}

	snd_pcm_info_alloca(&pcm_info);

	for (;;)
	{
		char *device, *descr;
		if ((err = snd_ctl_pcm_next_device(ctl, &pcm_device)) < 0)
		{
			g_warning("snd_ctl_pcm_next_device() failed: %s",
				  snd_strerror(err));
			pcm_device = -1;
		}
		if (pcm_device < 0)
			break;

		snd_pcm_info_set_device(pcm_info, pcm_device);
		snd_pcm_info_set_subdevice(pcm_info, 0);
		snd_pcm_info_set_stream(pcm_info, SND_PCM_STREAM_PLAYBACK);

		if ((err = snd_ctl_pcm_info(ctl, pcm_info)) < 0)
		{
			if (err != -ENOENT)
				g_warning("get_devices_for_card(): "
					  "snd_ctl_pcm_info() "
					  "failed (%d:%d): %s.", card,
					  pcm_device, snd_strerror(err));
			continue;
		}

		device = g_strdup_printf("hw:%d,%d", card, pcm_device);
		descr = g_strconcat(card_name, ": ",
				    snd_pcm_info_get_name(pcm_info),
				    " (", device, ")", NULL);
		item = gtk_list_item_new_with_label(descr);
		gtk_widget_show(item);
		g_free(descr);
		gtk_combo_set_item_string(combo, GTK_ITEM(item), device);
		g_free(device);
		gtk_container_add(GTK_CONTAINER(combo->list), item);
	}

	snd_ctl_close(ctl);
}



static void get_devices(GtkCombo *combo)
{
	GtkWidget *item;
	gint card = -1;
	gint err = 0;
	gchar *descr;

	descr = g_strdup_printf(_("Default PCM device (%s)"), "default");
	item = gtk_list_item_new_with_label(descr);
	gtk_widget_show(item);
	g_free(descr);
	gtk_combo_set_item_string(combo, GTK_ITEM(item), "default");
	gtk_container_add(GTK_CONTAINER(combo->list), item);

	if ((err = snd_card_next(&card)) != 0)
	{
		g_warning("snd_next_card() failed: %s", snd_strerror(err));
		return;
	}

	while (card > -1)
	{
		get_devices_for_card(combo, card);
		if ((err = snd_card_next(&card)) != 0)
		{
			g_warning("snd_next_card() failed: %s",
				  snd_strerror(err));
			break;
		}
	}
}

static void mixer_card_cb(GtkWidget * widget, gpointer card)
{
	gchar scratch[128];

	if (current_mixer_card == GPOINTER_TO_INT(card))
		return;
	current_mixer_card = GPOINTER_TO_INT(card);

	snprintf(scratch, 128, "hw:%d", current_mixer_card);
	get_mixer_devices(GTK_COMBO(mixer_devices_combo),
			  scratch);
}

void alsaplug_configure(void)
{
	GtkWidget *vbox, *notebook;
	GtkWidget *dev_vbox, *adevice_frame, *adevice_box;
	GtkWidget *mixer_frame, *mixer_box, *mixer_table, *mixer_card_om;
	GtkWidget *mixer_card_label, *mixer_device_label;
	GtkWidget *bbox, *ok, *cancel;

	gint mset;

	if (configure_win)
	{
                gtk_window_present(GTK_WINDOW(configure_win));
		return;
	}

	configure_win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_signal_connect(GTK_OBJECT(configure_win), "destroy",
			   GTK_SIGNAL_FUNC(gtk_widget_destroyed),
			   &configure_win);
	gtk_window_set_title(GTK_WINDOW(configure_win),
			     _("ALSA Driver configuration"));
	gtk_window_set_policy(GTK_WINDOW(configure_win),
			      FALSE, TRUE, FALSE);
	gtk_container_border_width(GTK_CONTAINER(configure_win), 10);

	vbox = gtk_vbox_new(FALSE, 10);
	gtk_container_add(GTK_CONTAINER(configure_win), vbox);

	notebook = gtk_notebook_new();
	gtk_box_pack_start(GTK_BOX(vbox), notebook, TRUE, TRUE, 0);

	dev_vbox = gtk_vbox_new(FALSE, 5);
	gtk_container_set_border_width(GTK_CONTAINER(dev_vbox), 5);

	adevice_frame = gtk_frame_new(_("Audio device:"));
	gtk_box_pack_start(GTK_BOX(dev_vbox), adevice_frame, FALSE, FALSE, 0);

	adevice_box = gtk_vbox_new(FALSE, 5);
	gtk_container_set_border_width(GTK_CONTAINER(adevice_box), 5);
	gtk_container_add(GTK_CONTAINER(adevice_frame), adevice_box);

	devices_combo = gtk_combo_new();
	gtk_box_pack_start(GTK_BOX(adevice_box), devices_combo,
			   FALSE, FALSE, 0);
	get_devices(GTK_COMBO(devices_combo));
	gtk_entry_set_text(GTK_ENTRY(GTK_COMBO(devices_combo)->entry),
			   alsaplug_cfg.pcm_device);

	mixer_frame = gtk_frame_new(_("Mixer:"));
	gtk_box_pack_start(GTK_BOX(dev_vbox), mixer_frame, FALSE, FALSE, 0);

	mixer_box = gtk_vbox_new(FALSE, 5);
	gtk_container_set_border_width(GTK_CONTAINER(mixer_box), 5);
	gtk_container_add(GTK_CONTAINER(mixer_frame), mixer_box);

	mixer_table = gtk_table_new(2, 2, FALSE);
	gtk_table_set_row_spacings(GTK_TABLE(mixer_table), 5);
	gtk_table_set_col_spacings(GTK_TABLE(mixer_table), 5);
	gtk_box_pack_start(GTK_BOX(mixer_box), mixer_table, FALSE, FALSE, 0);

	mixer_card_label = gtk_label_new(_("Mixer card:"));
	gtk_label_set_justify(GTK_LABEL(mixer_card_label), GTK_JUSTIFY_LEFT);
	gtk_misc_set_alignment(GTK_MISC(mixer_card_label), 0, 0.5);
	gtk_table_attach(GTK_TABLE(mixer_table), mixer_card_label,
			 0, 1, 0, 1, GTK_FILL, 0, 0, 0);

	mixer_card_om = gtk_option_menu_new();
	mset = get_cards(GTK_OPTION_MENU(mixer_card_om),
			 (GtkSignalFunc)mixer_card_cb);

	gtk_table_attach(GTK_TABLE(mixer_table), mixer_card_om,
			 1, 2, 0, 1, GTK_FILL | GTK_EXPAND, GTK_FILL, 0, 0);

	mixer_device_label = gtk_label_new(_("Mixer device:"));
	gtk_label_set_justify(GTK_LABEL(mixer_device_label), GTK_JUSTIFY_LEFT);
	gtk_misc_set_alignment(GTK_MISC(mixer_device_label), 0, 0.5);
	gtk_table_attach(GTK_TABLE(mixer_table), mixer_device_label,
			 0, 1, 1, 2, GTK_FILL, 0, 0, 0);
	mixer_devices_combo = gtk_combo_new();
	gtk_option_menu_set_history(GTK_OPTION_MENU(mixer_card_om), mset);
	get_mixer_devices(GTK_COMBO(mixer_devices_combo), alsaplug_cfg.mixer_card);
	gtk_entry_set_text(GTK_ENTRY(GTK_COMBO(mixer_devices_combo)->entry),
			   alsaplug_cfg.mixer_device);

	gtk_table_attach(GTK_TABLE(mixer_table), mixer_devices_combo,
			 1, 2, 1, 2, GTK_FILL | GTK_EXPAND, 0, 0, 0);

	gtk_notebook_append_page(GTK_NOTEBOOK(notebook), dev_vbox,
				 gtk_label_new(_("Device settings")));

	bbox = gtk_hbutton_box_new();
	gtk_button_box_set_layout(GTK_BUTTON_BOX(bbox), GTK_BUTTONBOX_END);
	gtk_button_box_set_spacing(GTK_BUTTON_BOX(bbox), 5);
	gtk_box_pack_start(GTK_BOX(vbox), bbox, FALSE, FALSE, 0);

	ok = gtk_button_new_with_label(_("OK"));
	gtk_signal_connect(GTK_OBJECT(ok), "clicked", (GCallback)configure_win_ok_cb, NULL);
	GTK_WIDGET_SET_FLAGS(ok, GTK_CAN_DEFAULT);
	gtk_box_pack_start(GTK_BOX(bbox), ok, TRUE, TRUE, 0);
	gtk_widget_grab_default(ok);

	cancel = gtk_button_new_with_label(_("Cancel"));
	gtk_signal_connect_object(GTK_OBJECT(cancel), "clicked",
				  (GCallback)gtk_widget_destroy, GTK_OBJECT(configure_win));
	GTK_WIDGET_SET_FLAGS(cancel, GTK_CAN_DEFAULT);
	gtk_box_pack_start(GTK_BOX(bbox), cancel, TRUE, TRUE, 0);

	gtk_widget_show_all(configure_win);
}
