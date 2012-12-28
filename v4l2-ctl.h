/* gst-httpd.c - Simple HTTP server for multipart/jpeg via gstreamer
 *
 * Copyright (C) 2012 Tim Harvey <harvey.tim at gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 */
#ifndef _V4L2_CTL_H_
#define _V4L2_CTL_H_

#include <gst/gst.h>

struct MediaURL;
struct GstHTTPClient;

gboolean v4l2_config(MediaURL *url, GstHTTPClient *client, gpointer data);
gboolean v4l2_config_device(const gchar *devname, GstHTTPMediaMapping *mapping, const gchar *inputdev);

#endif // #ifndef _V4L2_CTL_H_
