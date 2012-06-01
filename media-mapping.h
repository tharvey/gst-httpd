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

G_BEGIN_DECLS

#define GST_TYPE_HTTP_MEDIA_MAPPING              (gst_http_media_mapping_get_type ())
#define GST_IS_HTTP_MEDIA_MAPPING(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_HTTP_MEDIA_MAPPING))
#define GST_IS_HTTP_MEDIA_MAPPING_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_HTTP_MEDIA_MAPPING))
#define GST_HTTP_MEDIA_MAPPING_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_HTTP_MEDIA_MAPPING, GstHTTPMediaMappingClass))
#define GST_HTTP_MEDIA_MAPPING(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_HTTP_MEDIA_MAPPING, GstHTTPMediaMapping))
#define GST_HTTP_MEDIA_MAPPING_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_HTTP_MEDIA_MAPPING, GstHTTPMediaMappingClass))
#define GST_HTTP_MEDIA_MAPPING_CAST(obj)         ((GstHTTPMediaMapping*)(obj))
#define GST_HTTP_MEDIA_MAPPING_CLASS_CAST(klass) ((GstHTTPMediaMappingClass*)(klass))

#define GST_HTTP_MEDIA_MAPPING_GET_LOCK(mapping)  (GST_HTTP_MEDIA_MAPPING_CAST(mapping)->lock)
#define GST_HTTP_MEDIA_MAPPING_LOCK(mapping)      (g_mutex_lock(GST_HTTP_MEDIA_MAPPING_GET_LOCK(mapping)))
#define GST_HTTP_MEDIA_MAPPING_UNLOCK(mapping)    (g_mutex_unlock(GST_HTTP_MEDIA_MAPPING_GET_LOCK(mapping)))


typedef struct _GstHTTPMediaMapping GstHTTPMediaMapping;
typedef struct _GstHTTPMediaMappingClass GstHTTPMediaMappingClass;
typedef struct _MediaURL MediaURL;

#include "http-client.h"
#include "media.h"

struct _MediaURL {
	gchar *method;
	gchar *path;    // path portion of the URL (following prot/server and preceeding '?')
	gchar *query;   // full query string (text following a '?' in URL
	gchar **querys; // query string split by '&'
};

typedef gboolean (*MappingFunc)(MediaURL *url, GstHTTPClient *client, gpointer data);

gchar *get_query_field(MediaURL *url, const char* name);

/** GstHTTPMediaMapping - A mapping of a unique URL path to a resource
 *
 * There are two types of mappings:
 *   stream - a gstreamer based pipeline
 *   callback - a custom handler   
 */
struct _GstHTTPMediaMapping {
	GObject       parent;

	GMutex        *lock;
	GList         *mappings;
};

/**
 * GstHTTPMediaMappingClass:
 * @find_media: Create or return a previously cached #GstHTTPMedia object
 *        for the given url. the default implementation will use the mappings
 *        added with gst_http_media_mapping_add_factory().
 *
 * The class for the media mapping object.
 */
struct _GstHTTPMediaMappingClass {
	GObjectClass  parent_class;
};

GType                 gst_http_media_mapping_get_type     (void);

/* creating a mapping */
GstHTTPMediaMapping * gst_http_media_mapping_new          (void);

/* number of mappings */
guint gst_http_media_mapping_num_mappings (GstHTTPMediaMapping *mapping);

/* finding a media */
GstHTTPMedia * gst_http_media_mapping_find (GstHTTPMediaMapping *mapping,
	const gchar *path);

/* managing media to a path */
void gst_http_media_mapping_add (GstHTTPMediaMapping *mapping,
	const gchar *path, GstHTTPMedia *media);
void gst_http_media_mapping_remove (GstHTTPMediaMapping *mapping, const gchar *path);

G_END_DECLS

#endif /* _MEDIA_MAPPING_H_ */
