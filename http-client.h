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

#ifndef __GST_HTTP_CLIENT_H__
#define __GST_HTTP_CLIENT_H__

#include <gst/gst.h>

G_BEGIN_DECLS

typedef struct _GstHTTPClient GstHTTPClient;
typedef struct _GstHTTPClientClass GstHTTPClientClass;

#include "http-server.h"
#include "media-mapping.h"
#include "media.h"
#include "rate.h"

#define GST_TYPE_HTTP_CLIENT              (gst_http_client_get_type ())
#define GST_IS_HTTP_CLIENT(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_HTTP_CLIENT))
#define GST_IS_HTTP_CLIENT_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_HTTP_CLIENT))
#define GST_HTTP_CLIENT_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_HTTP_CLIENT, GstHTTPClientClass))
#define GST_HTTP_CLIENT(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_HTTP_CLIENT, GstHTTPClient))
#define GST_HTTP_CLIENT_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_HTTP_CLIENT, GstHTTPClientClass))
#define GST_HTTP_CLIENT_CAST(obj)         ((GstHTTPClient*)(obj))
#define GST_HTTP_CLIENT_CLASS_CAST(klass) ((GstHTTPClientClass*)(klass))

/**
 * GstHTTPClient:
 *
 * @connection: the connection object handling the client request.
 * @watch: watch for the connection
 * @watchid: id of the watch
 * @ip: ip address used by the client to connect to us
 * @session_pool: handle to the session pool used by the client.
 * @media_mapping: handle to the media mapping used by the client.
 * @uri: cached uri
 * @media: cached media
 * @streams: a list of streams using @connection.
 * @sessions: a list of sessions managed by @connection.
 *
 * The client structure.
 */
struct _GstHTTPClient {
	GObject       parent;

	GstHTTPServer *server;
	GSource       *watch;
	gchar         *serv_ip;
	gchar         *peer_ip;
	int            sock;
	guint          port;
	gchar         **headers;
	GstHTTPMediaMapping  *media_mapping;
	GstHTTPMedia  *media;

	/* counters */
	struct avg avg_frames;
	struct avg avg_bytes;
	unsigned long ewma_framesize;
};

struct _GstHTTPClientClass {
	GObjectClass  parent_class;

	/* signals */
	void     (*closed)        (GstHTTPClient *client);
};

GType          gst_http_client_get_type  (void);
GstHTTPClient *gst_http_client_new       (void);
void           gst_http_client_set_server(GstHTTPClient *client,
                                          GstHTTPServer *server);
GstHTTPServer *gst_http_client_get_server(GstHTTPClient *client);
gboolean       gst_http_client_accept    (GstHTTPClient *client,
                                          GIOChannel *channel);
void           gst_http_client_close     (GstHTTPClient *client,
                                          const char *msg);
gint           gst_http_client_write     (GstHTTPClient *client,
                                          const char *fmt, ...)
                                          __attribute__ ((format(printf,2,3))); 
gint           gst_http_client_writeln   (GstHTTPClient *client,
                                          const char *fmt, ...)
                                          __attribute__ ((format(printf,2,3))); 
void           gst_http_client_set_media_mapping (GstHTTPClient *client,
                                                  GstHTTPMediaMapping *mapping);
GstHTTPMediaMapping * gst_http_client_get_media_mapping (GstHTTPClient *client);

G_END_DECLS

#endif /* __GST_HTTP_CLIENT_H__ */
