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

#include <gst/gst.h>

#include "http-server.h"
#include "http-client.h"

#define DEFAULT_ADDRESS         "0.0.0.0"
#define DEFAULT_SERVICE         "8080"
#define DEFAULT_NAME            "gst-httpd"
#define DEFAULT_BACKLOG         15

/* Define to use the SO_LINGER option so that the server sockets can be resused
 * sooner. Disabled for now because it is not very well implemented by various
 * OSes and it causes clients to fail to read the TEARDOWN response. */
#undef USE_SOLINGER

static void unmanage_client (GstHTTPClient * client, GstHTTPServer * server);

enum
{
  PROP_0,
  PROP_ADDRESS,
  PROP_SERVICE,
  PROP_NAME,
  PROP_BACKLOG,
	PROP_MEDIA_MAPPING,

  PROP_LAST
};

G_DEFINE_TYPE (GstHTTPServer, gst_http_server, G_TYPE_OBJECT);

GST_DEBUG_CATEGORY_STATIC (http_server_debug);
#define GST_CAT_DEFAULT http_server_debug

static void gst_http_server_get_property (GObject * object, guint propid,
    GValue * value, GParamSpec * pspec);
static void gst_http_server_set_property (GObject * object, guint propid,
    const GValue * value, GParamSpec * pspec);
static void gst_http_server_finalize (GObject * object);

static GstHTTPClient *default_create_client (GstHTTPServer * server);
static gboolean default_accept_client (GstHTTPServer * server, GstHTTPClient * client, GIOChannel * channel);

static void
gst_http_server_class_init (GstHTTPServerClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->get_property = gst_http_server_get_property;
  gobject_class->set_property = gst_http_server_set_property;
  gobject_class->finalize = gst_http_server_finalize;

  /**
   * GstHTTPServer::address
   *
   * The address of the server. This is the address where the server will
   * listen on.
   */
  g_object_class_install_property (gobject_class, PROP_ADDRESS,
      g_param_spec_string ("address", "Address",
          "The address the server uses to listen on", DEFAULT_ADDRESS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  /**
   * GstHTTPServer::service
   *
   * The service of the server. This is either a string with the service name or
   * a port number (as a string) the server will listen on.
   */
  g_object_class_install_property (gobject_class, PROP_SERVICE,
      g_param_spec_string ("service", "Service",
          "The service or port number the server uses to listen on",
          DEFAULT_SERVICE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  /**
   * GstHTTPServer::servername
   *
   * The name of the server. This is the name returned via the 'server' header.
   */
  g_object_class_install_property (gobject_class, PROP_NAME,
      g_param_spec_string ("servername", "Name",
          "The name the server returns via HTTP headers", DEFAULT_NAME,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  /**
   * GstHTTPServer::backlog
   *
   * The backlog argument defines the maximum length to which the queue of
   * pending connections for the server may grow. If a connection request arrives
   * when the queue is full, the client may receive an error with an indication of
   * ECONNREFUSED or, if the underlying protocol supports retransmission, the
   * request may be ignored so that a later reattempt at  connection succeeds.
   */
  g_object_class_install_property (gobject_class, PROP_BACKLOG,
      g_param_spec_int ("backlog", "Backlog",
          "The maximum length to which the queue "
          "of pending connections may grow", 0, G_MAXINT, DEFAULT_BACKLOG,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

	/**
	 * GstHTTPServer::media-mapping
	 *
	 * The media mapping to use for this server. By default the server has no
	 * media mapping and thus cannot map urls to media streams.
	 */
	g_object_class_install_property (gobject_class, PROP_MEDIA_MAPPING,
	g_param_spec_object ("media-mapping", "Media Mapping",
			"The media mapping to use for client session",
			GST_TYPE_HTTP_MEDIA_MAPPING,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  klass->create_client = default_create_client;
  klass->accept_client = default_accept_client;

  GST_DEBUG_CATEGORY_INIT (http_server_debug, "httpserver", 0, "GstHTTPServer");
}

static void
gst_http_server_init (GstHTTPServer * server)
{
  server->lock = g_mutex_new ();
  server->address = g_strdup (DEFAULT_ADDRESS);
  server->service = g_strdup (DEFAULT_SERVICE);
  server->name = g_strdup (DEFAULT_NAME);
  server->backlog = DEFAULT_BACKLOG;
  server->media_mapping = gst_http_media_mapping_new ();
  server->clients = NULL;
}

static void
gst_http_server_finalize (GObject * object)
{
	GstHTTPServer *server = GST_HTTP_SERVER (object);
	int i;

	GST_DEBUG_OBJECT (server, "finalize server");

	g_free (server->address);
	g_free (server->service);
	g_free (server->name);

	if (server->media_mapping)
		g_object_unref (server->media_mapping);

	for (i = 0; i < g_list_length(server->clients); i++)
	{
		GstHTTPClient *client = g_list_nth_data(server->clients, i);
		unmanage_client(client, server);
	}
	g_list_free (server->clients);

	g_mutex_free (server->lock);

	G_OBJECT_CLASS (gst_http_server_parent_class)->finalize (object);
}

/**
 * gst_http_server_new:
 *
 * Create a new #GstHTTPServer instance.
 */
GstHTTPServer *
gst_http_server_new (void)
{
  GstHTTPServer *result;

  result = g_object_new (GST_TYPE_HTTP_SERVER, NULL);

  return result;
}

/**
 * gst_http_server_set_address:
 * @server: a #GstHTTPServer
 * @address: the address
 *
 * Configure @server to accept connections on the given address.
 *
 * This function must be called before the server is bound.
 */
void
gst_http_server_set_address (GstHTTPServer * server, const gchar * address)
{
  g_return_if_fail (GST_IS_HTTP_SERVER (server));
  g_return_if_fail (address != NULL);

  GST_HTTP_SERVER_LOCK (server);
  g_free (server->address);
  server->address = g_strdup (address);
  GST_HTTP_SERVER_UNLOCK (server);
}

/**
 * gst_http_server_get_address:
 * @server: a #GstHTTPServer
 *
 * Get the address on which the server will accept connections.
 *
 * Returns: the server address. g_free() after usage.
 */
gchar *
gst_http_server_get_address (GstHTTPServer * server)
{
  gchar *result;
  g_return_val_if_fail (GST_IS_HTTP_SERVER (server), NULL);

  GST_HTTP_SERVER_LOCK (server);
  result = g_strdup (server->address);
  GST_HTTP_SERVER_UNLOCK (server);

  return result;
}

/**
 * gst_http_server_set_servername:
 * @server: a #GstHTTPServer
 * @name: the server name 
 *
 * Configure @server name that is given to client via HTTP 'server' header.
 *
 */
void
gst_http_server_set_servername (GstHTTPServer * server, const gchar * name)
{
  g_return_if_fail (GST_IS_HTTP_SERVER (server));
  g_return_if_fail (name != NULL);

  GST_HTTP_SERVER_LOCK (server);
  g_free (server->name);
  server->name = g_strdup (name);
  GST_HTTP_SERVER_UNLOCK (server);
}

/**
 * gst_http_server_get_servername:
 * @server: a #GstHTTPServer
 *
 * Get the name that the server sends in 'server' header.
 *
 * Returns: the server name. g_free() after usage.
 */
gchar *
gst_http_server_get_servername (GstHTTPServer * server)
{
  gchar *result;
  g_return_val_if_fail (GST_IS_HTTP_SERVER (server), NULL);

  GST_HTTP_SERVER_LOCK (server);
  result = g_strdup (server->name);
  GST_HTTP_SERVER_UNLOCK (server);

  return result;
}

/**
 * gst_http_server_set_service:
 * @server: a #GstHTTPServer
 * @service: the service
 *
 * Configure @server to accept connections on the given service.
 * @service should be a string containing the service name (see services(5)) or
 * a string containing a port number between 1 and 65535.
 *
 * This function must be called before the server is bound.
 */
void
gst_http_server_set_service (GstHTTPServer * server, const gchar * service)
{
  g_return_if_fail (GST_IS_HTTP_SERVER (server));
  g_return_if_fail (service != NULL);

  GST_HTTP_SERVER_LOCK (server);
  g_free (server->service);
  server->service = g_strdup (service);
  GST_HTTP_SERVER_UNLOCK (server);
}

/**
 * gst_http_server_get_service:
 * @server: a #GstHTTPServer
 *
 * Get the service on which the server will accept connections.
 *
 * Returns: the service. use g_free() after usage.
 */
gchar *
gst_http_server_get_service (GstHTTPServer * server)
{
  gchar *result;

  g_return_val_if_fail (GST_IS_HTTP_SERVER (server), NULL);

  GST_HTTP_SERVER_LOCK (server);
  result = g_strdup (server->service);
  GST_HTTP_SERVER_UNLOCK (server);

  return result;
}

/**
 * gst_http_server_set_backlog:
 * @server: a #GstHTTPServer
 * @backlog: the backlog
 *
 * configure the maximum amount of requests that may be queued for the
 * server.
 *
 * This function must be called before the server is bound.
 */
void
gst_http_server_set_backlog (GstHTTPServer * server, gint backlog)
{
  g_return_if_fail (GST_IS_HTTP_SERVER (server));

  GST_HTTP_SERVER_LOCK (server);
  server->backlog = backlog;
  GST_HTTP_SERVER_UNLOCK (server);
}

/**
 * gst_http_server_get_backlog:
 * @server: a #GstHTTPServer
 *
 * The maximum amount of queued requests for the server.
 *
 * Returns: the server backlog.
 */
gint
gst_http_server_get_backlog (GstHTTPServer * server)
{
  gint result;

  g_return_val_if_fail (GST_IS_HTTP_SERVER (server), -1);

  GST_HTTP_SERVER_LOCK (server);
  result = server->backlog;
  GST_HTTP_SERVER_UNLOCK (server);

  return result;
}


/**
 * gst_http_server_set_media_mapping:
 * @server: a #GstHTTPServer
 * @mapping: a #GstHTTPMediaMapping
 *
 * configure @mapping to be used as the media mapping of @server.
 */
void
gst_http_server_set_media_mapping (GstHTTPServer * server,
    GstHTTPMediaMapping * mapping)
{
  GstHTTPMediaMapping *old;

  g_return_if_fail (GST_IS_HTTP_SERVER (server));

  if (mapping)
    g_object_ref (mapping);

  GST_HTTP_SERVER_LOCK (server);
  old = server->media_mapping;
  server->media_mapping = mapping;
  GST_HTTP_SERVER_UNLOCK (server);

  if (old)
    g_object_unref (old);
}


/**
 * gst_http_server_get_media_mapping:
 * @server: a #GstHTTPServer
 *
 * Get the #GstHTTPMediaMapping used as the media mapping of @server.
 *
 * Returns: the #GstHTTPMediaMapping of @server. g_object_unref() after
 * usage.
 */
GstHTTPMediaMapping *
gst_http_server_get_media_mapping (GstHTTPServer * server)
{
  GstHTTPMediaMapping *result;

  g_return_val_if_fail (GST_IS_HTTP_SERVER (server), NULL);

  GST_HTTP_SERVER_LOCK (server);
  if ((result = server->media_mapping))
    g_object_ref (result);
  GST_HTTP_SERVER_UNLOCK (server);

  return result;
}


static void
gst_http_server_get_property (GObject * object, guint propid,
    GValue * value, GParamSpec * pspec)
{
  GstHTTPServer *server = GST_HTTP_SERVER (object);

  switch (propid) {
    case PROP_ADDRESS:
      g_value_take_string (value, gst_http_server_get_address (server));
      break;
    case PROP_SERVICE:
      g_value_take_string (value, gst_http_server_get_service (server));
      break;
    case PROP_NAME:
      g_value_take_string (value, gst_http_server_get_servername (server));
      break;
    case PROP_BACKLOG:
      g_value_set_int (value, gst_http_server_get_backlog (server));
      break;
    case PROP_MEDIA_MAPPING:
      g_value_take_object (value, gst_http_server_get_media_mapping (server));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, propid, pspec);
  }
}

static void
gst_http_server_set_property (GObject * object, guint propid,
    const GValue * value, GParamSpec * pspec)
{
  GstHTTPServer *server = GST_HTTP_SERVER (object);

  switch (propid) {
    case PROP_ADDRESS:
      gst_http_server_set_address (server, g_value_get_string (value));
      break;
    case PROP_SERVICE:
      gst_http_server_set_service (server, g_value_get_string (value));
      break;
    case PROP_NAME:
      gst_http_server_set_servername (server, g_value_get_string (value));
      break;
    case PROP_BACKLOG:
      gst_http_server_set_backlog (server, g_value_get_int (value));
      break;
    case PROP_MEDIA_MAPPING:
      gst_http_server_set_media_mapping (server, g_value_get_object (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, propid, pspec);
  }
}

/**
 * gst_http_server_get_io_channel:
 * @server: a #GstHTTPServer
 *
 * Create a #GIOChannel for @server. The io channel will listen on the
 * configured service.
 *
 * Returns: the GIOChannel for @server or NULL when an error occured.
 */
GIOChannel *
gst_http_server_get_io_channel (GstHTTPServer * server)
{
  GIOChannel *channel;
  int ret, sockfd = -1;
  struct addrinfo hints;
  struct addrinfo *result, *rp;
#ifdef USE_SOLINGER
  struct linger linger;
#endif

  g_return_val_if_fail (GST_IS_HTTP_SERVER (server), NULL);

  memset (&hints, 0, sizeof (struct addrinfo));
  hints.ai_family = AF_UNSPEC;  /* Allow IPv4 or IPv6 */
  hints.ai_socktype = SOCK_STREAM;      /* stream socket */
  hints.ai_flags = AI_PASSIVE | AI_CANONNAME;   /* For wildcard IP address */
  hints.ai_protocol = 0;        /* Any protocol */
  hints.ai_canonname = NULL;
  hints.ai_addr = NULL;
  hints.ai_next = NULL;

  GST_DEBUG_OBJECT (server, "getting address info of %s/%s", server->address,
      server->service);

  GST_HTTP_SERVER_LOCK (server);
  /* resolve the server IP address */
  if ((ret =
          getaddrinfo (server->address, server->service, &hints, &result)) != 0)
    goto no_address;

  /* create server socket, we loop through all the addresses until we manage to
   * create a socket and bind. */
  for (rp = result; rp; rp = rp->ai_next) {
    sockfd = socket (rp->ai_family, rp->ai_socktype, rp->ai_protocol);
    if (sockfd == -1) {
      GST_DEBUG_OBJECT (server, "failed to make socket (%s), try next",
          g_strerror (errno));
      continue;
    }

    /* make address reusable */
    ret = 1;
    if (setsockopt (sockfd, SOL_SOCKET, SO_REUSEADDR,
            (void *) &ret, sizeof (ret)) < 0) {
      /* warn but try to bind anyway */
      GST_WARNING_OBJECT (server, "failed to reuse socker (%s)",
          g_strerror (errno));
    }

    if (bind (sockfd, rp->ai_addr, rp->ai_addrlen) == 0) {
      GST_DEBUG_OBJECT (server, "bind on %s", rp->ai_canonname);
      break;
    }

    GST_DEBUG_OBJECT (server, "failed to bind socket (%s), try next",
        g_strerror (errno));
    close (sockfd);
    sockfd = -1;
  }
  freeaddrinfo (result);

  if (sockfd == -1)
    goto no_socket;

  GST_DEBUG_OBJECT (server, "opened sending server socket with fd %d", sockfd);

  /* keep connection alive; avoids SIGPIPE during write */
  ret = 1;
  if (setsockopt (sockfd, SOL_SOCKET, SO_KEEPALIVE,
          (void *) &ret, sizeof (ret)) < 0)
    goto keepalive_failed;

#ifdef USE_SOLINGER
  /* make sure socket is reset 5 seconds after close. This ensure that we can
   * reuse the socket quickly while still having a chance to send data to the
   * client. */
  linger.l_onoff = 1;
  linger.l_linger = 5;
  if (setsockopt (sockfd, SOL_SOCKET, SO_LINGER,
          (void *) &linger, sizeof (linger)) < 0)
    goto linger_failed;
#endif

  /* set the server socket to nonblocking */
  fcntl (sockfd, F_SETFL, O_NONBLOCK);

  GST_DEBUG_OBJECT (server, "listening on server socket %d with queue of %d",
      sockfd, server->backlog);
  if (listen (sockfd, server->backlog) == -1)
    goto listen_failed;

  GST_DEBUG_OBJECT (server,
      "listened on server socket %d, returning from connection setup", sockfd);

  /* create IO channel for the socket */
  channel = g_io_channel_unix_new (sockfd);
  g_io_channel_set_close_on_unref (channel, TRUE);

  GST_INFO_OBJECT (server, "listening on service %s", server->service);
  GST_HTTP_SERVER_UNLOCK (server);

  return channel;

  /* ERRORS */
no_address:
  {
    GST_ERROR_OBJECT (server, "failed to resolve address: %s",
        gai_strerror (ret));
    goto close_error;
  }
no_socket:
  {
    GST_ERROR_OBJECT (server, "failed to create socket: %s",
        g_strerror (errno));
    goto close_error;
  }
keepalive_failed:
  {
    GST_ERROR_OBJECT (server, "failed to configure keepalive socket: %s",
        g_strerror (errno));
    goto close_error;
  }
#ifdef USE_SOLINGER
linger_failed:
  {
    GST_ERROR_OBJECT (server, "failed to no linger socket: %s",
        g_strerror (errno));
    goto close_error;
  }
#endif
listen_failed:
  {
    GST_ERROR_OBJECT (server, "failed to listen on socket: %s",
        g_strerror (errno));
    goto close_error;
  }
close_error:
  {
    if (sockfd >= 0) {
      close (sockfd);
    }
    GST_HTTP_SERVER_UNLOCK (server);
    return NULL;
  }
}

static void
unmanage_client (GstHTTPClient * client, GstHTTPServer * server)
{
	GST_DEBUG_OBJECT (server, "unmanage client %p", client);

	gst_http_client_set_server (client, NULL);

	GST_HTTP_SERVER_LOCK (server);
	server->clients = g_list_remove (server->clients, client);
	GST_DEBUG_OBJECT (server, "now managing %d clients", g_list_length(server->clients));
	GST_HTTP_SERVER_UNLOCK (server);

	g_object_unref (client);
}

/* add the client to the active list of clients, takes ownership of
 * the client */
static void
manage_client (GstHTTPServer * server, GstHTTPClient * client)
{
  GST_DEBUG_OBJECT (server, "manage client %p", client);
  gst_http_client_set_server (client, server);

  GST_HTTP_SERVER_LOCK (server);
  g_signal_connect (client, "closed", (GCallback) unmanage_client, server);
  server->clients = g_list_prepend (server->clients, client);
  GST_HTTP_SERVER_UNLOCK (server);
}

static GstHTTPClient *
default_create_client (GstHTTPServer * server)
{
	GstHTTPClient *client;

	/* a new client connected, create a session to handle the client. */
	GST_HTTP_SERVER_LOCK (server);
	client = gst_http_client_new ();
	/* set the media mapping that this client should use */
	gst_http_client_set_media_mapping (client, server->media_mapping);
	GST_HTTP_SERVER_UNLOCK (server);

	return client;
}

/* default method for creating a new client object in the server to accept and
 * handle a client connection on this server */
static gboolean
default_accept_client (GstHTTPServer * server, GstHTTPClient * client,
    GIOChannel * channel)
{
  /* accept connections for that client, this function returns after accepting
   * the connection and will run the remainder of the communication with the
   * client asyncronously. */
  if (!gst_http_client_accept (client, channel))
    goto accept_failed;

  return TRUE;

  /* ERRORS */
accept_failed:
  {
    GST_ERROR_OBJECT (server,
        "Could not accept client on server : %s (%d)", g_strerror (errno),
        errno);
    return FALSE;
  }
}

/**
 * gst_http_server_io_func:
 * @channel: a #GIOChannel
 * @condition: the condition on @source
 *
 * A default #GIOFunc that creates a new #GstHTTPClient to accept and handle a
 * new connection on @channel or @server.
 *
 * Returns: TRUE if the source could be connected, FALSE if an error occured.
 */
gboolean
gst_http_server_io_func (GIOChannel * channel, GIOCondition condition,
    GstHTTPServer * server)
{
  gboolean result = FALSE;
  GstHTTPClient *client = NULL;
  GstHTTPServerClass *klass;

GST_DEBUG("condition=%x", condition);
  if (condition & G_IO_IN) {
    klass = GST_HTTP_SERVER_GET_CLASS (server);

    if (klass->create_client)
      client = klass->create_client (server);
    if (client == NULL)
      goto client_failed;

    /* a new client connected, create a client object to handle the client. */
    if (klass->accept_client)
      result = klass->accept_client (server, client, channel);
    if (!result)
      goto accept_failed;

    /* manage the client connection */
    manage_client (server, client);
  } else {
    GST_WARNING_OBJECT (server, "received unknown event %08x", condition);
  }
  return TRUE;

  /* ERRORS */
client_failed:
  {
    GST_ERROR_OBJECT (server, "failed to create a client");
    return FALSE;
  }
accept_failed:
  {
    GST_ERROR_OBJECT (server, "failed to accept client");
    gst_object_unref (client);
    return FALSE;
  }
}

static void
server_watch_destroyed (GstHTTPServer * server)
{
	GST_DEBUG_OBJECT (server, "source destroyed");
	g_object_unref (server);
}

/**
 * gst_http_server_create_watch:
 * @server: a #GstHTTPServer
 *
 * Create a #GSource for @server. The new source will have a default
 * #GIOFunc of gst_http_server_io_func().
 *
 * Returns: the #GSource for @server or NULL when an error occured.
 */
GSource *
gst_http_server_create_watch (GstHTTPServer * server)
{
  GIOChannel *channel;
  GSource *source;

  g_return_val_if_fail (GST_IS_HTTP_SERVER (server), NULL);

  channel = gst_http_server_get_io_channel (server);
  if (channel == NULL)
    goto no_channel;

  /* create a watch for reads (new connections) and possible errors */
  source = g_io_create_watch (channel, G_IO_IN |
      G_IO_ERR | G_IO_HUP | G_IO_NVAL);
  g_io_channel_unref (channel);

  /* configure the callback */
  g_source_set_callback (source,
      (GSourceFunc) gst_http_server_io_func, g_object_ref (server),
      (GDestroyNotify) server_watch_destroyed);

  return source;

no_channel:
  {
    GST_ERROR_OBJECT (server, "failed to create IO channel");
    return NULL;
  }
}

void
gst_http_server_detach(GstHTTPServer *server)
{
	g_source_destroy(server->source);
}

/**
 * gst_http_server_attach:
 * @server: a #GstHTTPServer
 * @context: a #GMainContext
 *
 * Attaches @server to @context. When the mainloop for @context is run, the
 * server will be dispatched. When @context is NULL, the default context will be
 * used).
 *
 * This function should be called when the server properties and urls are fully
 * configured and the server is ready to start.
 *
 * Returns: the ID (greater than 0) for the source within the GMainContext.
 */
guint
gst_http_server_attach (GstHTTPServer * server, GMainContext * context)
{
  guint res;

  g_return_val_if_fail (GST_IS_HTTP_SERVER (server), 0);

  server->source = gst_http_server_create_watch (server);
  if (server->source == NULL)
    goto no_source;

  res = g_source_attach (server->source, context);
  g_source_unref (server->source);

  return res;

  /* ERRORS */
no_source:
  {
    GST_ERROR_OBJECT (server, "failed to create watch");
    return 0;
  }
}
