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

#ifndef __GST_HTTP_SERVER_H__
#define __GST_HTTP_SERVER_H__

#include <gst/gst.h>

G_BEGIN_DECLS

typedef struct _GstHTTPServer GstHTTPServer;
typedef struct _GstHTTPServerClass GstHTTPServerClass;

#include "media-mapping.h"
#include "http-client.h"

//#define MULTIPART_BOUNDARY "--gst-mjpg-ns-boundary--may-not-work-with-ie--"
#define MULTIPART_BOUNDARY "--gst-mjpg-ns-boundary--"

#define GST_TYPE_HTTP_SERVER              (gst_http_server_get_type ())
#define GST_IS_HTTP_SERVER(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_HTTP_SERVER))
#define GST_IS_HTTP_SERVER_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_HTTP_SERVER))
#define GST_HTTP_SERVER_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_HTTP_SERVER, GstHTTPServerClass))
#define GST_HTTP_SERVER(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_HTTP_SERVER, GstHTTPServer))
#define GST_HTTP_SERVER_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_HTTP_SERVER, GstHTTPServerClass))
#define GST_HTTP_SERVER_CAST(obj)         ((GstHTTPServer*)(obj))
#define GST_HTTP_SERVER_CLASS_CAST(klass) ((GstHTTPServerClass*)(klass))

#define GST_HTTP_SERVER_GET_LOCK(server)  (GST_HTTP_SERVER_CAST(server)->lock)
#define GST_HTTP_SERVER_LOCK(server)      (g_mutex_lock(GST_HTTP_SERVER_GET_LOCK(server)))
#define GST_HTTP_SERVER_UNLOCK(server)    (g_mutex_unlock(GST_HTTP_SERVER_GET_LOCK(server)))

/**
 * GstHTTPServer:
 *
 * This object listens on a port, creates and manages the clients connected to
 * it.
 */
struct _GstHTTPServer {
	GObject      parent;

	GMutex      *lock;

	/* server information */
	gchar       *address;
	gchar       *service;
	gchar       *name;
	gint         backlog;

	/* the clients that are connected */
	GList       *clients;

	/* media mappings */
	GstHTTPMediaMapping *media_mapping;

	GSource     *source;
};

/**
 * GstHTTPServerClass:
 *
 * @create_client: Create, configure a new GstHTTPClient
 *          object that handles the new connection on @channel.
 * @accept_client: accept a new GstHTTPClient
 *
 * The HTTP server class structure
 */
struct _GstHTTPServerClass {
  GObjectClass  parent_class;

  GstHTTPClient * (*create_client) (GstHTTPServer *server);
  gboolean        (*accept_client) (GstHTTPServer *server, GstHTTPClient *client, GIOChannel *channel);
};

GType          gst_http_server_get_type           (void);
GstHTTPServer *gst_http_server_new                (void);
void           gst_http_server_set_address        (GstHTTPServer *server, const gchar *address);
gchar *        gst_http_server_get_address        (GstHTTPServer *server);
void           gst_http_server_set_service        (GstHTTPServer *server, const gchar *service);
gchar *        gst_http_server_get_service        (GstHTTPServer *server);
void           gst_http_server_set_backlog        (GstHTTPServer *server, gint backlog);
gint           gst_http_server_get_backlog        (GstHTTPServer *server);
void           gst_http_server_set_servername     (GstHTTPServer *server, const gchar *name);
gchar *        gst_http_server_get_servername     (GstHTTPServer *server);
void           gst_http_server_set_media_mapping        (GstHTTPServer *server,
                                                         GstHTTPMediaMapping*);
GstHTTPMediaMapping*  gst_http_server_get_media_mapping (GstHTTPServer *server);
guint          gst_http_server_attach             (GstHTTPServer * server,
                                                   GMainContext * context);
void           gst_http_server_detach             (GstHTTPServer * server);




G_END_DECLS

#endif /* __GST_HTTP_SERVER_H__ */
