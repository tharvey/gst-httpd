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
#include <string.h>

#include "media-mapping.h"

G_DEFINE_TYPE (GstHTTPMediaMapping, gst_http_media_mapping, G_TYPE_OBJECT);

GST_DEBUG_CATEGORY_STATIC (http_media_debug);
#define GST_CAT_DEFAULT http_media_debug

static void gst_http_media_mapping_finalize (GObject * obj);

static void
gst_http_media_mapping_class_init (GstHTTPMediaMappingClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->finalize = gst_http_media_mapping_finalize;

  GST_DEBUG_CATEGORY_INIT (http_media_debug, "httpmediamapping", 0,
      "GstHTTPMediaMapping");
}

static void
gst_http_media_mapping_init (GstHTTPMediaMapping * mapping)
{
	mapping->lock = g_mutex_new ();

	GST_DEBUG_OBJECT (mapping, "created %p", mapping);
}

static void
gst_http_media_mapping_finalize (GObject * obj)
{
	guint i;

	GstHTTPMediaMapping *mapping = GST_HTTP_MEDIA_MAPPING (obj);

	GST_DEBUG_OBJECT (mapping, "finalized %p", mapping);
	for (i = 0; i < g_list_length(mapping->mappings); i++) {
		GstHTTPMedia *media = g_list_nth_data(mapping->mappings, i);
		g_object_unref(media);
	}
	g_list_free (mapping->mappings);
	g_mutex_free (mapping->lock);

	G_OBJECT_CLASS (gst_http_media_mapping_parent_class)->finalize (obj);
}

GstHTTPMediaMapping *
gst_http_media_mapping_new (void)
{
  GstHTTPMediaMapping *result;

  result = g_object_new (GST_TYPE_HTTP_MEDIA_MAPPING, NULL);

  return result;
}


/**
 * gst_http_media_mapping_find:
 * @mapping: a #GstHTTPMediaMapping
 * @path: a url path
 *
 * Find the #GstHTTPMedia for @url.
 *
 * Returns: the #GstHTTPMedia for @url. g_object_unref() after usage.
 */
GstHTTPMedia *
gst_http_media_mapping_find (GstHTTPMediaMapping * mapping,
    const gchar *path)
{
	GstHTTPMedia *result = NULL;
	guint i;

	GST_HTTP_MEDIA_MAPPING_LOCK(mapping);
	for (i = 0; i < g_list_length(mapping->mappings); i++) {
		result = g_list_nth_data(mapping->mappings, i);

		if (strcmp(path, result->path) == 0) {
			break;
		}

		else if (strchr(result->path, '*')) {
			int l = strchr(result->path, '*') - result->path;

			if (strncmp(path, result->path, l) == 0) {
				break;
			}
		}
	}
	if (i == g_list_length(mapping->mappings))
		result = NULL;
	GST_HTTP_MEDIA_MAPPING_UNLOCK(mapping);

	if (result) {
		GST_INFO ("found media %p for url abspath %s", result, path);
	}

	return result;
}

/**
 * gst_http_media_mapping_num_mappings
 */
guint
gst_http_media_mapping_num_mappings (GstHTTPMediaMapping *mapping)
{
	guint result;

	GST_HTTP_MEDIA_MAPPING_LOCK(mapping);
	result = g_list_length(mapping->mappings);
	GST_HTTP_MEDIA_MAPPING_UNLOCK(mapping);

	return result;
}


/**
 * gst_http_media_mapping_add:
 * @mapping: a #GstHTTPMediaMapping
 * @path: a mount point
 * @media: a #GstHTTPMedia
 *
 * Attach @media to the mount point @path in @mapping.
 *
 * @path is of the form (/node)+. Any previous mapping will be freed.
 *
 * Ownership is taken of the reference on @media so that @media should not be
 * used after calling this function.
 */
void
gst_http_media_mapping_add ( GstHTTPMediaMapping *mapping,
	const gchar *path, GstHTTPMedia *media)
{
	g_return_if_fail (GST_IS_HTTP_MEDIA_MAPPING (mapping));
	g_return_if_fail (GST_IS_HTTP_MEDIA (media));
	g_return_if_fail (path != NULL);

	GST_INFO ("Adding %s - %s", path, media->desc);

	if (path[0] != '/')
		media->path = g_strconcat("/", path, NULL);
	else
		media->path = g_strdup(path);

	GST_HTTP_MEDIA_MAPPING_LOCK(mapping);
	mapping->mappings = g_list_append (mapping->mappings, media);
	GST_HTTP_MEDIA_MAPPING_UNLOCK(mapping);
}


/**
 * gst_http_media_mapping_remove:
 * @mapping: a #GstHTTPMediaMapping
 * @path: a mount point
 *
 * Remove the #GstHTTPMedia associated with @path in @mapping.
 */
void
gst_http_media_mapping_remove (GstHTTPMediaMapping * mapping,
    const gchar * path)
{
	GstHTTPMedia *media;

	g_return_if_fail (GST_IS_HTTP_MEDIA_MAPPING (mapping));
	g_return_if_fail (path != NULL);

	media = gst_http_media_mapping_find (mapping, path);
	g_return_if_fail (media != NULL);

	GST_HTTP_MEDIA_MAPPING_LOCK(mapping);
	mapping->mappings = g_list_remove (mapping->mappings, mapping);
	GST_HTTP_MEDIA_MAPPING_UNLOCK(mapping);
}


gchar *
get_query_field(MediaURL *url, const char *name)
{
	if (url && url->querys) {
		int i;

		for (i = 0; url->querys[i]; i++) {
			if (strncasecmp(name, url->querys[i], strlen(name)) == 0) {
				char *val = strstr(url->querys[i], "=");
				if (val)
					return g_strdup(val+1);
			}
		}
	}

	return NULL;
}
