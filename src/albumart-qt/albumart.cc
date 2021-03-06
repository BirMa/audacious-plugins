/*
 * albumart.cc
 * Copyright 2014 William Pitcock
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions, and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions, and the following disclaimer in the documentation
 *    provided with the distribution.
 *
 * This software is provided "as is" and without any warranty, express or
 * implied. In no event shall the authors be liable for any damages arising from
 * the use of this software.
 */

#include <QLabel>
#include <QPixmap>

#include <libaudcore/drct.h>
#include <libaudcore/i18n.h>
#include <libaudcore/plugin.h>
#include <libaudcore/hook.h>

#include <libaudqt/libaudqt.h>

class AlbumArtQt : public GeneralPlugin {
public:
    static constexpr PluginInfo info = {
        N_("Album Art (Qt)"),
        PACKAGE
    };

    constexpr AlbumArtQt () : GeneralPlugin (info, false) {}
    void * get_qt_widget ();
};

static void update (void *, QLabel * widget)
{
    QSize size = widget->size ();
    widget->setPixmap (audqt::art_request_current (size.width (), size.height ()));
}

static void clear (void *, QLabel * widget)
{
    widget->setPixmap (QPixmap ());
}

static void widget_cleanup (QObject * widget)
{
    hook_dissociate ("playback ready", (HookFunction) update, widget);
    hook_dissociate ("playback stop", (HookFunction) clear, widget);
}

void * AlbumArtQt::get_qt_widget ()
{
    QLabel * widget = new QLabel;

    QObject::connect (widget, &QObject::destroyed, widget_cleanup);

    hook_associate ("playback ready", (HookFunction) update, widget);
    hook_associate ("playback stop", (HookFunction) clear, widget);

    widget->resize (96, 96);

    if (aud_drct_get_ready ())
        update (nullptr, widget);

    return widget;
}

EXPORT AlbumArtQt aud_plugin_instance;
