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
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>

#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <gst/app/gstappbuffer.h>

#include "media.h"

#define DEFAULT_SHARED          FALSE

enum
{
	PROP_0,
	PROP_SHARED,

	PROP_LAST
};

G_DEFINE_TYPE (GstHTTPMedia, gst_http_media, G_TYPE_OBJECT);

GST_DEBUG_CATEGORY_STATIC (http_media_debug);
#define GST_CAT_DEFAULT http_media_debug

static void gst_http_media_get_property (GObject * object, guint propid,
    GValue * value, GParamSpec * pspec);
static void gst_http_media_set_property (GObject * object, guint propid,
    const GValue * value, GParamSpec * pspec);
static void gst_http_media_finalize (GObject * object);

static void
gst_http_media_class_init (GstHTTPMediaClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->get_property = gst_http_media_get_property;
  gobject_class->set_property = gst_http_media_set_property;
  gobject_class->finalize = gst_http_media_finalize;

  g_object_class_install_property (gobject_class, PROP_SHARED,
      g_param_spec_string ("shared", "Shared",
          "If this media pipeline can be shared", DEFAULT_SHARED,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  GST_DEBUG_CATEGORY_INIT (http_media_debug, "httpmedia", 0, "GstHTTPMedia");
}

static void
gst_http_media_init (GstHTTPMedia * media)
{
	media->lock = g_mutex_new ();

	media->shared = DEFAULT_SHARED;

	GST_INFO ("media created %p", media);
}

static void
gst_http_media_finalize (GObject * object)
{
	GstHTTPMedia *media;

 	media = GST_HTTP_MEDIA (object);

	GST_INFO ("finalize media %s %p", media->path, media);

	if (media->pipeline) {
		gst_element_set_state (media->pipeline, GST_STATE_NULL);
		gst_object_unref (media->pipeline);
	}

	g_list_free (media->clients);

	g_free(media->path);
	g_free(media->desc);
	g_free(media->pipeline_desc);
	g_free(media->v4l2srcdev);
	g_free(media->mimetype);
	g_free(media->capture);
	g_mutex_free (media->lock);

	G_OBJECT_CLASS (gst_http_media_parent_class)->finalize (object);
}

/**
 * gst_http_media_new_pipeline:
 * @pipeline: a string describing a Gstreamer pipeline (see gst-launch)
 * @desc: a text description of the stream
 *
 * Create a new #GstHTTPMedia instance.
 */
GstHTTPMedia *
gst_http_media_new_pipeline (const gchar *desc, const gchar *pipeline)
{
	GstHTTPMedia *result;
	gchar **elems;

	result = g_object_new (GST_TYPE_HTTP_MEDIA, NULL);

	GST_INFO ("Creating '%s' '%s'", desc, pipeline);

	result->desc = g_strdup(desc);
	result->pipeline_desc = g_strdup(pipeline);
	result->mimetype = g_strdup("multipart/x-mixed-replace");
	//result->mimetype = g_strdup("image/jpeg");
	elems = g_strsplit(pipeline, "!", 0);
	if (elems[0] && strstr(elems[0], "v4l2src")) {
		char *p = strstr(elems[0], "device=");
		if (p) {
			result->v4l2srcdev = g_strstrip(g_strdup(p + 7));
		} else {
			result->v4l2srcdev = g_strdup("/dev/video0");
		}
	}
	g_strfreev(elems);

	return result;
}


/**
 * gst_http_media_new_handler:
 * @handler: function for handling requests
 * @data: private data passed to handler
 *
 * Create a new #GstHTTPMedia instance.
 */
GstHTTPMedia *
gst_http_media_new_handler (const gchar *desc, MediaHandlerFunc func,
	gpointer data)
{
	GstHTTPMedia *result;

	result = g_object_new (GST_TYPE_HTTP_MEDIA, NULL);

	result->func = func;
	result->data = data;
	result->desc = g_strdup(desc);

	return result;
}


/**
 * gst_http_media_set_shared:
 * @media: a #GstHTTPMedia
 * @shared: the new value 
 *
 * Set or unset if the pipeline for @media can be shared will multiple clients.
 * When @shared is %TRUE, client requests for this media will share the media
 * pipeline.
 */
void
gst_http_media_set_shared (GstHTTPMedia * media, gboolean shared)
{
  g_return_if_fail (GST_IS_HTTP_MEDIA (media));

  media->shared = shared;
}

/**
 * gst_http_media_is_shared:
 * @media: a #GstHTTPMedia
 *
 * Check if a pipeline for @media can be shared bewteen multiple clients
 *
 * Returns: TRUE if the media can be shared between clients.
 */
gboolean
gst_http_media_is_shared (GstHTTPMedia * media)
{
  g_return_val_if_fail (GST_IS_HTTP_MEDIA (media), FALSE);

  return media->shared;
}

static void
gst_http_media_get_property (GObject * object, guint propid,
    GValue * value, GParamSpec * pspec)
{
  GstHTTPMedia *media = GST_HTTP_MEDIA (object);

  switch (propid) {
    case PROP_SHARED:
      g_value_set_boolean (value, gst_http_media_is_shared (media));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, propid, pspec);
  }
}

static void
gst_http_media_set_property (GObject * object, guint propid,
    const GValue * value, GParamSpec * pspec)
{
  GstHTTPMedia *media = GST_HTTP_MEDIA (object);

  switch (propid) {
    case PROP_SHARED:
      gst_http_media_set_shared (media, g_value_get_boolean (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, propid, pspec);
  }
}

/** gst_bus_callback - called when a message appears on the bus
 * @param bus
 * @param message
 * @param data - media media pointer
 */
static gboolean
gst_bus_callback (GstBus *bus, GstMessage *message, gpointer user_data)
{
	GList *walk;
	GstHTTPMedia *media = (GstHTTPMedia *) user_data;

	//GST_DEBUG_OBJECT(media, "Got %s message", GST_MESSAGE_TYPE_NAME (message));
	switch (GST_MESSAGE_TYPE (message)) {
		case GST_MESSAGE_ERROR: {
			GError *err;
			gchar *debug;

			gst_message_parse_error (message, &err, &debug);
			GST_ERROR ("Pipeline Error for %s: %s", media->path, err->message);

#if 1
			GST_HTTP_MEDIA_LOCK (media);
			for (walk = media->clients; walk; walk = g_list_next (walk)) {
				GstHTTPClient *client = (GstHTTPClient *) walk->data;
				gst_http_client_writeln(client, "Stream Error: %s", err->message);
				gst_http_client_write  (client, "\r\n");
				close(client->sock);
			}
			GST_HTTP_MEDIA_UNLOCK (media);
#endif

			g_error_free (err);
			g_free (debug);

			gst_http_media_stop (media, NULL);
		}	break;

		case GST_MESSAGE_STATE_CHANGED: {
			GstState old, new;

			gst_message_parse_state_changed(message, &old, &new, NULL);
			GST_DEBUG("%s: %s => %s",
				GST_OBJECT_NAME(message->src),
				gst_element_state_get_name(old),
				gst_element_state_get_name(new));
		}	break;

		default:
			break;
	}
	return TRUE;
}


/** gst_buffer_available - callback when frame buffer available to sink
 * @param elt - target element
 * @param media - media media
 *
 * Called when the Media has a frame available for the clients
 */
static GstFlowReturn
gst_buffer_available(GstAppSink * sink, gpointer user_data)
{
	GList *walk;
	GstBuffer *buffer;
	GstHTTPMedia *media;

	/* get the buffer from appsink */
	buffer = gst_app_sink_pull_buffer (sink);
	if (!buffer)
		return GST_FLOW_OK;

 	media = (GstHTTPMedia *) user_data;

	GST_DEBUG ("%s frame available: %d bytes", media->path, buffer->size);

	/* get width/height of stream */
	if (0 == media->width) {
		GstCaps *caps = gst_buffer_get_caps(buffer);
		const GstStructure *str = gst_caps_get_structure (caps, 0);
		if (!gst_structure_get_int (str, "width", (int*)&media->width) ||
		    !gst_structure_get_int (str, "height", (int*)&media->height)) {
			GST_ERROR("No width/height available");
		}
		GST_INFO("framesize=%dx%d", media->width, media->height);
	}

	/* push buffer to clients*/
	GST_HTTP_MEDIA_LOCK (media);
	for (walk = media->clients; walk; walk = g_list_next (walk)) {
		GstHTTPClient *c = (GstHTTPClient *) walk->data;

		if (strcmp(media->mimetype, "multipart/x-mixed-replace") == 0)
		{
			gst_http_client_write  (c, "\r\n");
			gst_http_client_writeln(c, "--%s", MULTIPART_BOUNDARY);
			gst_http_client_writeln(c, "Content-Type: image/jpeg");
			gst_http_client_writeln(c, "Content-Length: %d", buffer->size);
			gst_http_client_write  (c, "\r\n");
		}
		if (strcmp(media->mimetype, "image/jpeg") == 0)
		{
			gst_http_client_writeln(c, "Content-Length: %d", buffer->size);
			gst_http_client_write  (c, "\r\n");
		}

		c->ewma_framesize = c->ewma_framesize ?
				(((c->ewma_framesize * (2 /*weight*/ - 1)) +
					(buffer->size * 1 /*factor*/)) / 2 /*weight*/) :
				(buffer->size * 1 /*factor*/);
		avg_add_samples(&c->avg_frames, 1);
		avg_add_samples(&c->avg_bytes, buffer->size);
		if (media->capture)
		{
			gchar *fname = g_strdup_printf(media->capture,
				c->avg_frames.total);
			g_file_set_contents(fname, (char*)buffer->data,
				buffer->size, NULL);
			g_free(fname);
		}
		write (c->sock, buffer->data, buffer->size);	

		// if we are serving just an image, close the socket
		if (strcmp(media->mimetype, "image/jpeg") == 0) {
			close(c->sock);
		}
	}
	GST_HTTP_MEDIA_UNLOCK (media);

	/* we don't need the buffer anymore */
	gst_buffer_unref(buffer);

	return GST_FLOW_OK;
}

/**
 * gst_http_media_create_pipeline:
 * @media: a #GstHTTPMedia to play
 
 * Launch the gstreamer pipeline
 *
 * Returns error code (0 = success) 
 *
 */
static int
gst_http_media_create_pipeline(GstHTTPMedia *media)
{
	GstElement *sink;
	GstBus *bus;
	gchar *desc;
	GError *err = NULL;

	if (media->pipeline)
		return -1;

	GST_INFO ("Creating new multipart/jpeg pipeline for '%s'", media->path);

	desc = g_strdup_printf("%s ! appsink name=sink", media->pipeline_desc);
	GST_DEBUG ("launching pipeline '%s'", desc);
	if (!(media->pipeline = gst_parse_launch(desc, &err))) {
		GST_ERROR ("Failed to create pipeline from '%s':%s",
			desc, err->message);
		return 1;
	}

	// add bus callback
	bus = gst_pipeline_get_bus(GST_PIPELINE(media->pipeline));
	gst_bus_add_watch(bus, gst_bus_callback, media);
	gst_object_unref(bus);

	// attach signal to sink
	sink = gst_bin_get_by_name (GST_BIN(media->pipeline), "sink");
	//g_object_set (G_OBJECT (sink), "emit-signals", TRUE, "sync", FALSE, NULL);
	g_object_set (G_OBJECT (sink), "emit-signals", TRUE, FALSE, NULL);
	g_signal_connect (sink, "new-buffer",
		G_CALLBACK(gst_buffer_available), media);
	gst_object_unref(sink);

	// set pipeline to playing state
	gst_element_set_state (media->pipeline, GST_STATE_PLAYING);

	g_free(desc);
	return 0;
}


/**
 * gst_http_media_play:
 * @media: a #GstHTTPMedia to play
 * @client: Client to stream to
 * Launch the gstreamer pipeline
 *
 * Returns error code (0 = success) 
 */
gint
gst_http_media_play (GstHTTPMedia *media, GstHTTPClient *client)
{
	if (!media->pipeline) {
		if (gst_http_media_create_pipeline(media))
			return 1;
	}

	g_object_ref(client);

	// add client to client list of media
	GST_INFO ("%s: Adding client to pipeline serving %d clients",
		media->path, g_list_length(media->clients));
	GST_HTTP_MEDIA_LOCK (media);
	media->clients = g_list_append(media->clients, client);
	GST_HTTP_MEDIA_UNLOCK (media);

	return 0;
}

/**
 * gst_http_media_stop:
 * @media: a #GstHTTPMedia
 * @client: Client to stop streaming to
 
 * Stop the gstreamer pipeline (client=NULL for all clients)
 *
 * Returns error code (0 = success) 
 *
 */
gint
gst_http_media_stop (GstHTTPMedia *media, GstHTTPClient *client)
{
	if (!media->pipeline) {
		return -1;
	}

	GST_INFO ("stopping stream %s client=%p", media->path, client);

	if (client) {
		GList *found;

		GST_HTTP_MEDIA_LOCK (media);
		found = g_list_find(media->clients, client);
		if (found)
			media->clients = g_list_remove (media->clients, client);
		GST_HTTP_MEDIA_UNLOCK (media);

		if (found) {
			gst_http_client_close(client, "stopping");
			g_object_unref (client);
		} else
			return -2;
	}

	// close all clients being served this stream
	else {
		GST_HTTP_MEDIA_LOCK (media);
		while (g_list_length(media->clients))
		{
			GstHTTPClient *client = g_list_nth_data(media->clients, 0);

			media->clients = g_list_remove (media->clients, client);
			GST_DEBUG_OBJECT (media, "now managing %d clients",
				g_list_length(media->clients));
			g_object_unref (client);
		}
		GST_HTTP_MEDIA_UNLOCK (media);
	}

	// if no more clients shut down the pipeline
	if (g_list_length(media->clients) == 0) {	
		GST_DEBUG_OBJECT (media, "Shutting down pipeline for %s", media->path);
		// set pipeline to NULL state
		gst_element_set_state (media->pipeline, GST_STATE_NULL);
		g_object_unref (media->pipeline);
		media->pipeline = NULL;
	}

	return 0;
}
