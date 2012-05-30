/* gst-httpd
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
 */

#ifndef _MEDIA_MAPPING_H_
#define _MEDIA_MAPPING_H_

#include <gst/gst.h>

typedef struct _MediaMapping MediaMapping;
typedef struct _MediaURL MediaURL;

#include "http-client.h"
#include "http-server.h"

#define GST_HTTP_MAPPING_LOCK(x)      (g_mutex_lock(x->lock))
#define GST_HTTP_MAPPING_UNLOCK(x)    (g_mutex_unlock(x->lock))

struct _MediaURL {
	gchar *method;
	gchar *path;    // path portion of the URL (following prot/server and preceeding '?')
	gchar *query;   // full query string (text following a '?' in URL
	gchar **querys; // query string split by '&'
};

typedef gboolean (*MappingFunc)(MediaURL *url, GstHTTPClient *client, gpointer data);

extern gchar *get_query_field(MediaURL *url, const char* name);

/** MediaMapping - A mapping of a unique URL path to a resource
 *
 * There are two types of mappings:
 *   stream - a gstreamer based pipeline
 *   callback - a custom handler   
 */
struct _MediaMapping {
	/* configuration */
	gchar         *path;
	gchar         *desc;

	/* stream resources */
	gchar         *pipeline_desc; // gst-launch text
	gchar         *mimetype;
	gchar         *v4l2srcdev;    // capture source device
	gchar         *capture;       // printf fmt string for capture fname
	guint          count;
	GList         *clients;
	GstHTTPServer *server;
	GstElement    *pipeline;
	guint         width;          // width of stream frame
  guint         height;         // height of stream frame

	/* callback resources */
	MappingFunc   func;
	gpointer      *data;

	/* misc */
	GMutex        *lock;
};

#endif /* _MEDIA_MAPPING_H_ */
