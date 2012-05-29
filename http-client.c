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
#include <netinet/in.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <stdarg.h>

#include "http-client.h"
#include "http-server.h"
#include "media-mapping.h"

enum
{
  SIGNAL_CLOSED,
  SIGNAL_LAST
};

GST_DEBUG_CATEGORY_STATIC (http_client_debug);
#define GST_CAT_DEFAULT http_client_debug

static guint gst_http_client_signals[SIGNAL_LAST] = { 0 };

static void gst_http_client_get_property (GObject * object, guint propid,
    GValue * value, GParamSpec * pspec);
static void gst_http_client_set_property (GObject * object, guint propid,
    const GValue * value, GParamSpec * pspec);
static void gst_http_client_finalize (GObject * obj);


G_DEFINE_TYPE (GstHTTPClient, gst_http_client, G_TYPE_OBJECT);

static void
gst_http_client_class_init (GstHTTPClientClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->get_property = gst_http_client_get_property;
  gobject_class->set_property = gst_http_client_set_property;
  gobject_class->finalize = gst_http_client_finalize;

  gst_http_client_signals[SIGNAL_CLOSED] =
  g_signal_new ("closed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
  G_STRUCT_OFFSET (GstHTTPClientClass, closed), NULL, NULL,
  g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0, G_TYPE_NONE);

  GST_DEBUG_CATEGORY_INIT (http_client_debug, "httpclient", 0, "GstHTTPClient");
}

static void
gst_http_client_init (GstHTTPClient * client)
{
}

/* A client is finalized when the connection is broken */
static void
gst_http_client_finalize (GObject * obj)
{
	GstHTTPClient *client = GST_HTTP_CLIENT (obj);

	GST_DEBUG_OBJECT (client, "finalize client %p", client);
	close(client->sock);

	g_free (client->peer_ip);
	g_free (client->serv_ip);
	g_strfreev (client->headers);
	g_source_unref(client->watch);
	g_source_destroy(client->watch);
//	g_object_unref (client->server); // we don't ref it

	G_OBJECT_CLASS (gst_http_client_parent_class)->finalize (obj);
}

static void
gst_http_client_get_property (GObject * object, guint propid,
    GValue * value, GParamSpec * pspec)
{
  GstHTTPClient *client = GST_HTTP_CLIENT (object);

  switch (propid) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, propid, pspec);
  }
}

static void
gst_http_client_set_property (GObject * object, guint propid,
    const GValue * value, GParamSpec * pspec)
{
  GstHTTPClient *client = GST_HTTP_CLIENT (object);

  switch (propid) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, propid, pspec);
  }
}

/**
 * gst_http_client_new:
 *
 * Create a new #GstHTTPClient instance.
 *
 * Returns: a new #GstHTTPClient
 */
GstHTTPClient *
gst_http_client_new (void)
{
  GstHTTPClient *result;

  result = g_object_new (GST_TYPE_HTTP_CLIENT, NULL);

  return result;
}

/**
 * gst_http_client_set_server:
 * @client: a #GstHTTPClient
 * @server: a #GstHTTPServer
 *
 * Set @server as the server that created @client.
 */
void
gst_http_client_set_server (GstHTTPClient * client, GstHTTPServer * server)
{
  GstHTTPServer *old;

  old = client->server;
  if (old != server) {
    if (server)
      g_object_ref (server);
    client->server = server;
    if (old)
      g_object_unref (old);
  }
}

/**
 * gst_http_client_get_server:
 * @client: a #GstHTTPClient
 *
 * Get the #GstHTTPServer object that @client was created from.
 *
 * Returns: a #GstHTTPServer, unref after usage.
 */
GstHTTPServer *
gst_http_client_get_server (GstHTTPClient * client)
{
  GstHTTPServer *result;

  if ((result = client->server))
    g_object_ref (result);

  return result;
}

gint
gst_http_client_write(GstHTTPClient *client, const char* fmt, ...)
{
	va_list args;
	gint ret = 0;

	va_start(args, fmt);
	ret = vdprintf(client->sock, fmt, args);
	va_end(args);

	return ret;
}

gint
gst_http_client_writeln(GstHTTPClient *client, const char* fmt, ...)
{
	va_list args;
	gint ret = 0;

	va_start(args, fmt);
	ret = vdprintf(client->sock, fmt, args);
	ret += write(client->sock, "\r\n", 2);
	va_end(args);

	return ret;
}

void
gst_http_client_close(GstHTTPClient *client, int code)
{
	GST_DEBUG_OBJECT (client, "client %s:%d finished:%d",
		client->peer_ip, client->port, code);

	close(client->sock);
	g_signal_emit (client, gst_http_client_signals[SIGNAL_CLOSED], 0, NULL);
}

/* parse string to next space/CR/null */
static char * 
parse_string(char **src) {
	char *p = *src;

	while (**src && !isspace(**src)) (*src)++;
	if (**src) {
		*(*src)++ = 0;
	}
	return p;
}

// HTTP header - see http://www.w3.org/Protocols/HTTP/1.0/draft-ietf-http-spec.html#Message-Headers
static void
client_header(GstHTTPClient *client)
{
	gchar *name = gst_http_server_get_servername(client->server);
	gst_http_client_writeln(client, "HTTP/1.0 200 OK");
	gst_http_client_writeln(client, "Server: %s", name);
	g_free(name);
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

static MediaURL *
create_url(char *str)
{
	char *http, *method, *page, *query = NULL, *p;
	p = str;
	MediaURL *url = NULL;

	method = parse_string(&p);
	page = parse_string(&p);
	http = parse_string(&p);
	if (method && page) {
		p = page;
		while (*p && *p != '?') p++;
		if (*p) {
			*p++ = 0;
			query = p;
		}

		url = (MediaURL *) malloc(sizeof(MediaURL));
		memset(url, 0, sizeof(MediaURL));
		url->method = g_strdup(method);
		url->path = g_strdup(page);
		if (query) {
			url->query = g_strdup(query);
			url->querys = g_strsplit(query, "&", 0);
		}
	}

	return url;
}

static gboolean
handle_request(GstHTTPClient *client)
{
	GstHTTPServer *server = client->server;
	char header[4096];
	int bytes, i;
	MediaURL *url = NULL;

	header[0] = 0;
 	bytes = read(client->sock, header, sizeof(header)-1);
	header[sizeof(header)] = 0;
	GST_DEBUG("read %d bytes from %s:%d (%d)", bytes, client->peer_ip, client->port, client->sock);
	if (bytes < 0) {
		GST_ERROR("read error %d from %s:%d:%d:%p\n", bytes,
			client->peer_ip, client->port, client->sock, client);
		gst_http_client_close(client, -1);
		return FALSE;
	}
	// this happens when client closes their end
	if (bytes == 0) {
		gst_http_client_close(client, -2);
		return FALSE;
	}
	client->headers = g_strsplit(header, "\r\n", 0);
	if (*client->headers) {
		url = create_url(client->headers[0]);
		GST_INFO ("client=%s:%d path='%s' query='%s'", client->peer_ip,
			client->port, url->path, url->query);

		if (strcmp(url->method, "GET") == 0) {
			client->mapping = gst_http_server_get_mapping(server,
				url->path);
		}
	}

	if (client->mapping) {
		MediaMapping *m = client->mapping;

		if (m->pipeline_desc) {
			char rfc1123[64];
			time_t gmt;

			GST_DEBUG_OBJECT(client, "pipeline mapping");
			gmt = time (NULL);
			strftime (rfc1123, 64, "%a, %d %b %Y %H:%M:%S GMT", gmtime (&gmt));

			client_header(client);
/*
			if (m->stream) {
				gst_http_client_writeln(client,
					"Content-Type: multipart/x-mixed-replace;boundary=%s",
					m->boundary);
			} else {
				gst_http_client_writeln(client, "Content-Type: image/jpeg");
			}
			gst_http_client_writeln(client, "Expires: %s", rfc1123);
			gst_http_client_writeln(client, "");
*/
			if (strcmp(m->mimetype, "image/jpeg") == 0) {
				gst_http_client_writeln(client, "Content-Type: %s", m->mimetype);
				gst_http_client_writeln(client, "");
			}
			else if (strcmp(m->mimetype, "multipart/x-mixed-replace") == 0)
			{
				gst_http_client_writeln(client, "Content-Type: %s;boundary=%s",
					m->mimetype, MULTIPART_BOUNDARY);
				gst_http_client_writeln(client, "Expires: %s", rfc1123);
				gst_http_client_writeln(client, "");
			}

			if (gst_http_server_play_media(server, m, client)) {
				gst_http_client_writeln(client, "415 Unsupported Media Type");
				gst_http_client_close(client, 1);
			}

			goto out;
		}

		else if (m->func) {
			GST_DEBUG_OBJECT(client, "got function mapping");
			client_header(client);
			if (client->mapping->func(url, client, client->mapping->data))
				gst_http_client_close(client, 0);

			goto out;
		}
	}

	gst_http_client_writeln(client, "404 Not Found");
	gst_http_client_close(client, 1);

out:
	if (url) {
		g_free(url->method);
		g_free(url->path);
		g_free(url->query);
		g_strfreev(url->querys);
		free(url);
	}

	return TRUE;
}

/**
 * gst_http_client_io_func:
 * @channel: a #GIOChannel
 * @condition: the condition on @source
 *
 * Returns: TRUE if the source could be connected, FALSE if an error occured.
 */
gboolean
gst_http_client_io_func (GIOChannel *source,
	GIOCondition condition, GstHTTPClient *client)
{
	if (condition & G_IO_IN) {
		return handle_request(client);
	}

	if (condition & G_IO_ERR) {
		GST_WARNING_OBJECT (client, "G_IO_ERR %08x", condition);
	}
	if (condition & G_IO_NVAL) {
// normal for closed clients
//		GST_WARNING_OBJECT (client, "G_IO_NVAL %08x", condition);
	}
	if (condition & G_IO_HUP) {
		GST_WARNING_OBJECT (client, "G_IO_HUP %08x", condition);
	}
	return FALSE;
}

static void
client_watch_destroyed (GstHTTPClient * client)
{
	GST_DEBUG_OBJECT (client, "source destroyed for %s:%p (%d)", client->peer_ip,
		client->port, client->sock);
	g_object_unref (client);
}

union gst_sockaddr
{
  struct sockaddr sa;
  struct sockaddr_in sa_in;
  struct sockaddr_in6 sa_in6;
  struct sockaddr_storage sa_stor;
};

/** return static ascii string notation of an IPv4 or IPv6 addr
 */
const char *sa_straddr(void *sa) {
	static char str[INET6_ADDRSTRLEN];
	struct sockaddr_in *v4 = (struct sockaddr_in *)sa;
	struct sockaddr_in6 *v6 = (struct sockaddr_in6 *)sa;

	if( v4->sin_family == AF_INET )
		return inet_ntop(AF_INET, &(v4->sin_addr), str, sizeof(str));
	else   
		return inet_ntop(AF_INET6, &(v6->sin6_addr), str, sizeof(str));
}

/**
 * gst_http_client_attach:
 * @client: a #GstHTTPClient
 * @channel: a #GIOChannel
 *
 * Accept a new connection for @client on the socket in @channel. 
 *
 * This function should be called when the client properties and urls are fully
 * configured and the client is ready to start.
 *
 * Returns: %TRUE if the client could be accepted.
 */
gboolean
gst_http_client_accept (GstHTTPClient * client, GIOChannel * channel)
{
	int sock, fd;
	union gst_sockaddr sa;
	socklen_t slen = sizeof(sa);
	struct sockaddr_in6 peeraddr;
	struct sockaddr_in6 servaddr;
	GSource *source;
	GMainContext *context;

	/* a new client connected. */
	sock = g_io_channel_unix_get_fd (channel);

	memset (&sa, 0, slen);
	fd = accept (sock, &sa.sa, &slen);
	if (fd == -1)
		return FALSE;
	
	/* get remote endpoint addr */
	g_free (client->peer_ip);
	slen = sizeof(struct sockaddr_in6);	
	memset(&peeraddr, 0, slen);
	getpeername(fd, (struct sockaddr *) &peeraddr, &slen);
	client->peer_ip = g_strdup(sa_straddr(&peeraddr));
	client->port = peeraddr.sin6_port;

	/* get local endpoint addr */
	g_free (client->serv_ip);
	slen = sizeof(struct sockaddr_in6);	
	memset(&servaddr, 0, slen);
	getsockname(fd, (struct sockaddr *) &servaddr, &slen);
	client->serv_ip = g_strdup(sa_straddr(&servaddr));

	GST_DEBUG_OBJECT (client, "Accepted connection %s:%d on %s",
		client->peer_ip, client->port, client->serv_ip);

#if 0 // if we set NONBLOCK we need to check for EAGAIN on each read/write call
	/* set non-blocking mode so that we can cacel the communication */
	fcntl (fd, F_SETFL, O_NONBLOCK);
#endif
	client->sock = fd;

	/* find the context to add the watch */
	if ((source = g_main_current_source ()))
		context = g_source_get_context (source);
	else
		context = NULL;

	/* create watch for the connection and attach */
	channel = g_io_channel_unix_new (fd);
	client->watch = g_io_create_watch (channel, G_IO_IN |
			G_IO_ERR | G_IO_HUP | G_IO_NVAL); 
	g_source_attach (client->watch, context);
	g_io_channel_unref (channel);

	/* configure the callback */
	g_source_set_callback (client->watch,
			(GSourceFunc) gst_http_client_io_func, g_object_ref (client),
			(GDestroyNotify) client_watch_destroyed);

	return TRUE;

	/* ERRORS */
getpeername_failed:
	{
		GST_ERROR ("getpeername failed: %s", g_strerror (errno));
		return FALSE;
	}
getnameinfo_failed:
	{
		GST_ERROR ("getnameinfo failed: %s", g_strerror (errno));
		return FALSE;
	}
}

