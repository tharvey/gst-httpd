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

#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <ctype.h>
#include <gst/gst.h>

#include "http-server.h"
#include "http-client.h"
#include "media-mapping.h"
#include "media.h"
#include "v4l2-ctl.h"
#include "rate.h"

#define V4L2_CTLS    // JSON set/get not implemented yet
#define LOCAL_PAGES  // useful if/when I have JSON support
#define SYS_STAT     // monitor /proc/stat for system details

#define VERSION "0.0.1"

GST_DEBUG_CATEGORY_STATIC (gst_http_debug);
#define GST_CAT_DEFAULT gst_http_debug

/* globals
 */
GMainLoop *loop;
#ifdef SYS_STAT
// CPU stats courtesy busybox/top.c
struct sysstat {
	unsigned long long usr, nic, sys, idle;
	unsigned long long io, irq, sirq, steal;	
	unsigned long long total;
	unsigned long long busy;
	time_t time;
};
struct sysstat stats[2];
struct sysstat *p_jif, *p_prev_jif;

#define SHOW_STAT(xxx) WRITE(client, "\t\""#xxx"\": \"%2.1f%%\"", ((unsigned)(p_jif->xxx - p_prev_jif->xxx) >= total_diff)?100.0:(((unsigned)(p_jif->xxx - p_prev_jif->xxx) * 100.0) / total_diff))

/* called on 1Hz timer - udpate system stats */
static gboolean
sysstat_timer(gpointer data)
{
	gchar *contents;
	static int idx = 0;
	struct sysstat *s;
	int i;

	if (idx == 0) {
		p_jif = &stats[0];
		p_prev_jif = &stats[1];
		idx = 1;
	} else {
		p_jif = &stats[1];
		p_prev_jif = &stats[0];
		idx = 0;
	}

	s = p_jif;
	if (g_file_get_contents("/proc/stat", &contents, NULL, NULL)) {
		gchar **lines = g_strsplit(contents, "\n", 0);
		g_free(contents);
		for (i = 0; lines[i]; i++) {
			int ret = sscanf(lines[i], "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
				&s->usr, &s->nic, &s->sys, &s->idle,
				&s->io, &s->irq, &s->sirq, &s->steal);
			if (ret >= 4) {
				s->total = s->usr + s->nic + s->sys + s->idle
					 + s->io+ s->irq + s->sirq + s->steal;
				s->busy = s->total - s->idle - s->io;
				break;
			}
		}
		g_strfreev(lines);
	}
	
	return TRUE; // keep calling	
}
#endif //#ifdef SYS_STAT

int
parse_config(GstHTTPServer *server, const gchar *configfile)
{
	GstHTTPMediaMapping *mapping;
	GstHTTPMedia *media = NULL;
	gchar *contents;
	char *path = NULL;
	char *pipe = NULL;
	char *desc = NULL;
	int i;

	GST_DEBUG_OBJECT (server, "Parsing %s...\n", configfile);
	mapping = gst_http_server_get_media_mapping(server);
	if (g_file_get_contents(configfile, &contents, NULL, NULL)) {
		char *line;
		char *p;
		gchar **lines = g_strsplit(contents, "\n", 0);
		g_free(contents);
		for (i = 0; lines[i]; i++) {
			line = lines[i];

			// use comment as pipe description
			if (line[0] == '#') {
				desc = line+1;
				while (isspace(*desc)) desc++;
				continue;
			}

			// options for previous mapping
			if ((p = strchr(line, ':')) && media) {
				*p++ = 0;
				if (strcmp(line, "capture") == 0) {
					media->capture = g_strdup(p);
				}
				continue;
			}

			// first word is path, rest is gst pipeline
			path = pipe = line;
			while (*pipe && !isspace(*pipe)) pipe++;
			if (*pipe) *pipe++ = 0; // terminate path
			while (*pipe && isspace(*pipe)) pipe++;
			if (*path && *pipe) {
				media = gst_http_media_new_pipeline (desc, pipe);
				gst_http_media_mapping_add (mapping, path, media);
			}
			desc = NULL;
		}
		g_strfreev(lines);
	}

	g_object_unref(mapping);

	return 0;
}

/** sighandler - signal handler for catching signal and exiting cleanly
 */
static void 
sighandler(int sig)
{
	static int quit = 0;
	fprintf(stderr, "%s %s (%d)\n", __func__, strsignal(sig), sig);

	switch (sig) {
		// quit
		case SIGINT:
		case SIGQUIT:
			if (quit++ == 0) {
				g_main_loop_quit(loop);
				return;
			}
			break;
		// ignore
		case SIGHUP:
			break;
	}
	exit(1);
}

#define WRITE(x, args...)  gst_http_client_write(x, args)
#define WRITELN(x, args...)  gst_http_client_writeln(x, args)

/** server_status - return server status as JSON
 * @param url - url mapping
 * @param client - client connection
 * @param data - server
 */
gboolean
server_status(MediaURL *url, GstHTTPClient *client, gpointer data)
{
	GstHTTPServer *server = (GstHTTPServer *) data;
	GstHTTPMediaMapping *mapping = gst_http_server_get_media_mapping(server);
	GError *err = NULL;
	int i, j;
	gchar *str;

	GST_INFO("Serving server_status to %s:%d", client->peer_ip, client->port);

	//WRITELN(client, "Last-Modified: %s", unix2date(time(NULL)));
	//WRITELN(client, "Expires: %s", timestr);
	WRITELN(client, "no-cache");
	WRITELN(client, "Content-Type: application/json\r\n");

	GST_HTTP_SERVER_LOCK(server);
	WRITELN(client, "{");
	WRITELN(client, "  \"media\": [");
	for (i = 0, j = 0; i < g_list_length(mapping->mappings); i++) {
		GstHTTPMedia *media = g_list_nth_data(mapping->mappings, i);
		char *name;
		if (!media->desc || !media->pipeline_desc)
			continue;
 		name = media->path;
		WRITE(client, (j++ > 0)?",\r\n\t":"\t");
		if (*name == '/' && (strlen(name) > 1)) name++;
		WRITELN(client, "{");
		WRITELN(client, "\t\t\"path\": \"%s\",", name);
		WRITELN(client, "\t\t\"desc\": \"%s\",", media->desc);
		WRITELN(client, "\t\t\"width\": \"%d\",", media->width);
		WRITELN(client, "\t\t\"height\": \"%d\",", media->height);
		WRITELN(client, "\t\t\"dev\" : \"%s\"", media->v4l2srcdev?media->v4l2srcdev:"");
		WRITE(client, "\t}");
	}
	WRITE(client, "\r\n  ]");

	WRITELN(client, ",");
	WRITELN(client, "  \"clients\": [");
	for (i = 0, j = 0; i < g_list_length(server->clients); i++) {
		GstHTTPClient *c = g_list_nth_data(server->clients, i);
		if (client == c)
			continue; // skip ourselves
		if (c->media) {
			WRITE(client, (j++ > 0)?",\r\n\t":"\t");
			WRITELN(client, "{");
			if (c->media->pipeline_desc) {
				WRITELN(client, "\t\t\"path\": \"%s\",", c->media->path);
				WRITELN(client, "\t\t\"framesize\": \"%ldK\",",
					c->ewma_framesize / 1024);
				WRITELN(client, "\t\t\"bitrate\": \"%2.0fkbps\",",
					(float) avg_get_avg(&c->avg_bytes) * 8.0 / 1024);
				WRITELN(client, "\t\t\"framerate\": \"%ld\",",
					avg_get_avg(&c->avg_frames));
			}
			WRITELN(client, "\t\t\"ip\": \"%s\",", c->peer_ip);
			WRITELN(client, "\t\t\"port\": \"%d\"", c->port);
			WRITE(client, "\t}");
		}
	}
	WRITE(client, "\r\n  ]");

	GST_HTTP_SERVER_UNLOCK(server);

#ifdef SYS_STAT
if (p_jif && p_prev_jif) {
	char buf[80];
	unsigned long total, used, mfree, buffers, cached;
	unsigned total_diff = (unsigned)(p_jif->total - p_prev_jif->total);
	static unsigned long sused = 0;
	if (total_diff == 0) total_diff = 1;

	WRITELN(client, ",");
	WRITELN(client, "  \"cpu\": {");
	SHOW_STAT(usr); WRITELN(client, ",");
	SHOW_STAT(sys); WRITELN(client, ",");
	SHOW_STAT(nic); WRITELN(client, ",");
	SHOW_STAT(idle); WRITELN(client, ",");
	SHOW_STAT(io); WRITELN(client, ",");
	SHOW_STAT(irq); WRITELN(client, ",");
	SHOW_STAT(sirq); WRITELN(client, " ");
	WRITE(client, "  }");

	WRITELN(client, ",");
	WRITELN(client, "  \"memory\": {");
	err = NULL;
	g_file_get_contents("/proc/meminfo", &str, NULL, &err);
	if (err != NULL)
	{
		//GST_ERROR("error:%s", err->message);
		g_error_free(err);
	} else {
		gchar **lines = g_strsplit(str, "\n", 0);
		g_free(str);
		sscanf(lines[0], "MemTotal: %lu %80s\n", &total, buf);
		sscanf(lines[1], "MemFree: %lu %80s\n", &mfree, buf);
		sscanf(lines[2], "Buffers: %lu %80s\n", &buffers, buf);
		sscanf(lines[3], "Cached: %lu %80s\n", &cached, buf);
		g_strfreev(lines);

		used = total - mfree;
		if (sused == 0) sused = used;
		WRITELN(client, "\t\"used\": \"%luK\",", used);
		WRITELN(client, "\t\"free\": \"%luK\",", mfree);
		WRITELN(client, "\t\"buff\": \"%luK\",", buffers);
		WRITELN(client, "\t\"cached\": \"%luK\",", cached);
		WRITELN(client, "\t\"delta\": \"%ldK\"", used - sused);
	}
	WRITE(client, "  }");

	WRITELN(client, ",");
	WRITELN(client, "  \"load\": {");
	err = NULL;
	g_file_get_contents("/proc/loadavg", &str, NULL, &err);
	if (err != NULL) {
		//GST_ERROR("error:%s", err->message);
		g_error_free(err);
	} else {
		WRITELN(client, "\t\"avg\": \"%s\"", g_strstrip(str));
		g_free(str);
	}
	WRITELN(client, "  }");
}
#endif //#ifdef SYS_STAT
	WRITELN(client, "\r\n}");

	g_object_unref(mapping);

	return TRUE;
}

#ifdef LOCAL_PAGES
struct mimetype {
	const char *extn;
	const char *mime;
};

static struct mimetype mime_types[] = {
	{ "html",    "text/html" },
	{ "js",      "text/javascript" },
	{ "css",     "text/css" },
	{ "jpg",     "image/jpeg" },
	{ NULL, NULL }
};

/** mime_lookup - return mime-type for a path
 * @param path
 * @return mime-type
 */
static const char*
mime_lookup(const char* path)
{
	struct mimetype *m = &mime_types[0];
	const char *e;

	while ( m->extn ) {
		e = &path[strlen(path)-1];
		while ( e >= path ) {
			if ( (*e == '.' || *e == '/') && !strcasecmp(&e[1], m->extn) )
				return m->mime;
			e--;
		}
		m++;
	}

	return "application/octet-stream";
}	


/** unix2date - convert a time_t to a string for Last-Modified
 */
char *
unix2date(time_t ts)
{
	static char str[128];
	struct tm *t = gmtime(&ts);

	strftime(str, sizeof(str), "%a, %d %b %Y %H:%M:%S GMT", t);
	return str;
}


/** serve_page - serve a page via HTTP
 * @param url requested
 * @param client to serve to
 * @param data - docroot
 */
gboolean
serve_page(MediaURL *url, GstHTTPClient *client, gpointer data)
{
	int fd;
	char buf[1024];
	int sz;
	struct stat sb;
	const char *docroot = (const char*) data;
	gchar *path;
	char *physpath;
	const char *mimetype;

	path = g_strconcat(docroot, url->path, NULL);
	physpath = realpath(path, NULL);
	if (!physpath)
		goto err;
	/* ensure physpath is within docroot */
	if (strncmp(physpath, docroot, strlen(docroot)) ||
	   ((physpath[strlen(docroot)] != 0) && (physpath[strlen(docroot)] != '/')) )
	{
		goto err;
	}
	/* ensure file is readable */
	if ( (stat(physpath, &sb) < 0)
	  || (fd = open(physpath, O_RDONLY)) == -1)
	{
		goto err;
	}
	if (!S_ISREG(sb.st_mode)) {
		close(fd);
		goto err;
	}
	/* obtain mimetype */
	mimetype = mime_lookup(physpath);

	GST_INFO("Serving %d byte %s to %s:%d as %s", (int)sb.st_size, physpath,
		client->peer_ip, client->port, mimetype);

	WRITELN(client, "Last-Modified: %s", unix2date(sb.st_mtime));
	WRITELN(client, "Content-Length: %ld", sb.st_size);
	WRITELN(client, "Content-Type: %s\r\n", mimetype);

	while ( (sz = read(fd, buf, sizeof(buf))) > 0) {
		write(client->sock, buf, sz);
	}
	write(client->sock, "", 0);
	close(fd);
	
	free(physpath);
	g_free(path);
	return TRUE;

err:
	GST_ERROR("404 Not Found: %s", path);
	if (physpath)
		free(physpath);
	g_free(path);
	return TRUE;
}
#endif // #ifdef LOCAL_PAGES


/** main function
 */
int
main (int argc, char *argv[])
{
	gchar *address = "0.0.0.0";
	gchar *service = "8080";
	gchar *docroot = NULL;
	char *docrootphys = NULL;
	gchar *sysadmin = "server.json";
	gchar *pidfile = NULL;
	GstHTTPServer *server;
	GstHTTPMediaMapping *mapping;
	GstHTTPMedia *media;
	GError *err = NULL;
	GOptionContext *ctx;
	gchar *configfile = NULL;
	int i;

	GOptionEntry options[] = {
		{"config", 'f', 0, G_OPTION_ARG_STRING, &configfile, "config file", "file"},
		{"address", 'a', 0, G_OPTION_ARG_STRING, &address, "address to listen on", "addr"},
		{"service", 's', 0, G_OPTION_ARG_STRING, &service, "service to listen on", "service"},
		{"docroot", 'd', 0, G_OPTION_ARG_STRING, &docroot, "root directory for www", "path"},
		{"sysadmin", 0, 0, G_OPTION_ARG_STRING, &sysadmin, "path to sysadmin", "path"},
		{"pidfile", 'p', 0, G_OPTION_ARG_STRING, &pidfile, "file to store pid", "filename"},
		{NULL}
	};

	printf("gst-mjpeg-streamer v%s\n", VERSION);

	/* must init the threading system before using any other glib function
	 */
	if (!g_thread_supported())
		g_thread_init(NULL);

	GST_DEBUG_CATEGORY_INIT (gst_http_debug, "gst_http", 0, "gst_http");

	ctx = g_option_context_new ("gst-httpd");
	g_option_context_add_main_entries(ctx, options, NULL);
	g_option_context_add_group(ctx, gst_init_get_option_group());
	if (!g_option_context_parse(ctx, &argc, &argv, &err)) {
		g_print ("Error initializing: %s\n", GST_STR_NULL(err->message));
		exit (1);
	}
	
	/* install signal handler */ 
	signal(SIGINT, sighandler);
	signal(SIGSEGV, sighandler);

	/* init gstreamer and create mainloop */
	gst_init (&argc, &argv);
	loop = g_main_loop_new (NULL, FALSE);

	/* create a server instance */
	server = gst_http_server_new ();
	mapping = gst_http_server_get_media_mapping (server);
	gst_http_server_set_address (server, address);
	gst_http_server_set_service (server, service);

	/* parse configfile */
	if (configfile) {
		parse_config(server, configfile);
	}

	/* parse commandline arguments */
	i = 1;
	while ( (argc - i) >= 2) {
		media = gst_http_media_new_pipeline ("", argv[i+1]);
		gst_http_media_mapping_add (mapping, argv[i], media);
		i+=2;
	}

	if (pidfile) {
  	FILE *fp = fopen(pidfile, "w+");
		if (fp) {
			fprintf(fp, "%d", getpid());
			fclose(fp);
		} else {
			perror ("open failed");
		}
	}

	/* add custom URL handlers */
#ifdef V4L2_CTLS
	media = gst_http_media_new_handler ("Video Controls", v4l2_config, server);
	gst_http_media_mapping_add (mapping, "v4l2cfg.json", media);
#endif
	if (sysadmin) {
		media = gst_http_media_new_handler ("Server Status", server_status, server);
		gst_http_media_mapping_add (mapping, sysadmin, media);
	}
#ifdef LOCAL_PAGES
	/* default to a page handler that serves from docroot */
	if (docroot) {
			docrootphys = realpath(docroot, NULL);
			if (docrootphys) {
				media = gst_http_media_new_handler ("Page Handler", serve_page,
					docrootphys);
				gst_http_media_mapping_add (mapping, "*", media);
			} else {
				g_print ("Error: docroot '%s' not found\n", docroot);
			}
	}
#endif // #ifdef LOCAL_PAGES

	/* make sure we have a valid configuration */
	if (gst_http_media_mapping_num_mappings(mapping) == 0) {
		g_print ("Error: no streams defined\n");
		g_print ("%s\n", g_option_context_get_help(ctx, 0, NULL));
		return -1;
	}
	g_object_unref(mapping);
	g_option_context_free(ctx);

	/* attach the server to the default maincontext */
	if (!gst_http_server_attach (server, NULL)) {
		fprintf(stderr, "Failed to attach server\n");
		exit(1);
	}

#ifdef SYS_STAT
	g_timeout_add(1000, sysstat_timer, NULL);
#endif

	/* start serving */
	g_print("%d: Listening on %s:%s\n", getpid(), address, service);
	g_main_loop_run (loop);

	GST_DEBUG("cleaning up...");
	gst_http_server_detach (server);
	g_object_unref (server);

	if (docrootphys)
		free(docrootphys);

	g_main_loop_unref(loop);

	gst_deinit();

	return 0;
}
