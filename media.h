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

#ifndef __GST_HTTP_MEDIA_H__
#define __GST_HTTP_MEDIA_H__

#include <linux/input.h>

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_HTTP_MEDIA              (gst_http_media_get_type ())
#define GST_IS_HTTP_MEDIA(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_HTTP_MEDIA))
#define GST_IS_HTTP_MEDIA_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_HTTP_MEDIA))
#define GST_HTTP_MEDIA_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_HTTP_MEDIA, GstHTTPMediaClass))
#define GST_HTTP_MEDIA(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_HTTP_MEDIA, GstHTTPMedia))
#define GST_HTTP_MEDIA_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_HTTP_MEDIA, GstHTTPMediaClass))
#define GST_HTTP_MEDIA_CAST(obj)         ((GstHTTPMedia*)(obj))
#define GST_HTTP_MEDIA_CLASS_CAST(klass) ((GstHTTPMediaClass*)(klass))

#define GST_HTTP_MEDIA_GET_LOCK(mapping)  (GST_HTTP_MEDIA_CAST(mapping)->lock)
#define GST_HTTP_MEDIA_LOCK(mapping)      (g_mutex_lock(GST_HTTP_MEDIA_GET_LOCK(mapping)))
#define GST_HTTP_MEDIA_UNLOCK(mapping)    (g_mutex_unlock(GST_HTTP_MEDIA_GET_LOCK(mapping)))

typedef struct _GstHTTPMedia GstHTTPMedia;
typedef struct _GstHTTPMediaClass GstHTTPMediaClass;

#include "http-client.h"
#include "media-mapping.h"

typedef gboolean (*MediaHandlerFunc)(MediaURL *url, GstHTTPClient *client, gpointer data);

/** GstHTTPMedia - A mapping of a unique URL path to a resource
 *
 * There are two types of mappings:
 *   stream - a gstreamer based pipeline
 *   callback - a custom handler   
 */
struct _GstHTTPMedia {
	GObject       parent;

	GMutex       *lock;

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
	GstElement    *pipeline;
	guint         width;          // width of stream frame
	guint         height;         // height of stream frame
	time_t        starttime;			// time stream playback started
	gboolean      shared;

	/* input device handling */
	gchar         *input_dev;			// input device filename
	int           input_fd;
	pthread_t     input_thread;
	time_t        ev_press;
	GMutex        *ev_lock;
	
	/* Media Handler resources */
	MediaHandlerFunc   func;
	gpointer      *data;
};

/**
 * GstHTTPMediaClass:
 * @find_media: Create or return a previously cached #GstHTTPMediaFactory object
 *        for the given url. the default implementation will use the mappings
 *        added with gst_http_media_add_factory().
 *
 * The class for the media mapping object.
 */
struct _GstHTTPMediaClass {
	GObjectClass  parent_class;
};

GType gst_http_media_get_type (void);

/* creating a media */
GstHTTPMedia * gst_http_media_new_pipeline (const gchar *desc,
	const gchar *pipeline, const gchar *inputdev);
GstHTTPMedia * gst_http_media_new_handler (const gchar *desc,
	MediaHandlerFunc, gpointer);

/* media playback/control */
gint gst_http_media_play (GstHTTPMedia *, GstHTTPClient *);
gint gst_http_media_stop (GstHTTPMedia *, GstHTTPClient *);

G_END_DECLS

#endif /* __GST_HTTP_MEDIA_H__ */
