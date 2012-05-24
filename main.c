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
#include <sys/stat.h>
#include <unistd.h>
#include <gst/gst.h>

#include "http-server.h"
#include "http-client.h"
#include "media-mapping.h"
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
	unsigned long long iowait, irq, softirq, steal;	
	unsigned long long total;
	unsigned long long busy;
	time_t time;
};
struct sysstat stats[2];
struct sysstat *p_jif, *p_prev_jif;

#define CALC_STAT(xxx) char xxx[8]
#define SHOW_STAT(xxx) fmt_100percent_8(xxx, (unsigned)(p_jif->xxx - p_prev_jif->xxx), total_diff)

/* formats 7 char string (8 with terminating NUL) */
static char *fmt_100percent_8(char pbuf[8], unsigned value, unsigned total)
{
        unsigned t;
        if (value >= total) { /* 100% ? */
                strcpy(pbuf, "  100% ");
                return pbuf;
        }
        /* else generate " [N/space]N.N% " string */
        value = 1000 * value / total;
        t = value / 100;
        value = value % 100;
        pbuf[0] = ' ';
        pbuf[1] = t ? t + '0' : ' ';
        pbuf[2] = '0' + (value / 10);
        pbuf[3] = '.';
        pbuf[4] = '0' + (value % 10);
        pbuf[5] = '%';
        pbuf[6] = ' ';
        pbuf[7] = '\0';
        return pbuf;
}

/* called on 1Hz timer - udpate system stats */
static gboolean
sysstat_timer(gpointer data)
{
	gchar *contents;
	gchar **lines;
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
				&s->iowait, &s->irq, &s->softirq, &s->steal);
			if (ret >= 4) {
				s->total = s->usr + s->nic + s->sys + s->idle
					 + s->iowait + s->irq + s->softirq + s->steal;
				s->busy = s->total - s->idle - s->iowait;
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
	gchar *contents;
	char *path = NULL;
	char *pipe = NULL;
	char *desc = NULL;
	int i;

	GST_DEBUG_OBJECT (server, "Parsing %s...\n", configfile);
	if (g_file_get_contents(configfile, &contents, NULL, NULL)) {
		char *line;
		char *p;
		MediaMapping *m;
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
			if ((p = strchr(line, ':')) && m) {
				*p++ = 0;
				if (strcmp(line, "capture") == 0) {
					m->capture = g_strdup(p);
				}
				continue;
			}

			// first word is path, rest is gst pipeline
			path = pipe = line;
			while (*pipe && !isspace(*pipe)) pipe++;
			if (*pipe) *pipe++ = 0; // terminate path
			while (*pipe && isspace(*pipe)) pipe++;
			if (*path && *pipe) {
				m = gst_http_server_add_mapping_pipe (server, path, desc, pipe);
			}
			desc = NULL;
		}
		g_strfreev(lines);
	}

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
	GError *err = NULL;
	int i, j;
	gchar *str;
	time_t now = time(NULL);
	char *timestr = ctime(&now);

	GST_INFO("Serving server_status to %s:%d", client->peer_ip, client->port);

	timestr[strlen(timestr)-1] = 0;
	//WRITELN(client, "Last-Modified: %s", timestr);
	//WRITELN(client, "Expires: %s", timestr);
	WRITELN(client, "no-cache");
	WRITELN(client, "Content-Type: application/json");
	WRITELN(client, "");

	GST_HTTP_SERVER_LOCK(server);
	WRITELN(client, "{");
	WRITELN(client, "  \"media\": [");
	for (i = 0, j = 0; i < g_list_length(server->mappings); i++) {
		MediaMapping *m = g_list_nth_data(server->mappings, i);
		char *name;
		if (!m->desc)
			continue;
 		name = m->path;
		WRITE(client, (j++ > 0)?",\r\n\t":"\t");
		if (*name == '/' && (strlen(name) > 1)) name++;
		WRITELN(client, "{");
		WRITELN(client, "\t\t\"path\": \"%s\",", name);
		WRITELN(client, "\t\t\"desc\": \"%s\"", m->desc);
		WRITE(client, "\t}");
	}
	WRITE(client, "\r\n  ]");

	WRITELN(client, ",");
	WRITELN(client, "  \"clients\": [");
	for (i = 0, j = 0; i < g_list_length(server->clients); i++) {
		GstHTTPClient *c = g_list_nth_data(server->clients, i);
		if (client == c)
			continue; // skip ourselves
		if (c->mapping) {
			WRITE(client, (j++ > 0)?",\r\n\t":"\t");
			WRITELN(client, "{");
			if (c->mapping->pipeline_desc) {
				WRITELN(client, "\t\t\"path\": \"%s\",", c->mapping->path);
				WRITELN(client, "\t\t\"framesize\": \"%ldK\",",
					c->ewma_framesize / 1024);
				WRITELN(client, "\t\t\"bitrate\": \"%2fkbps\",",
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
{
	char buf[80];
	unsigned long total, used, mfree, buffers, cached;
	unsigned total_diff = (unsigned)(p_jif->total - p_prev_jif->total);
	if (total_diff == 0) total_diff = 1;
	CALC_STAT(usr);
	CALC_STAT(sys);
	CALC_STAT(nic);
	CALC_STAT(idle);
	CALC_STAT(iowait);
	CALC_STAT(irq);
	CALC_STAT(softirq);

	WRITELN(client, ",");
	WRITELN(client, "  \"cpu\": {");
	WRITELN(client, "\t\"usr\": \"%s\",", SHOW_STAT(usr));
	WRITELN(client, "\t\"sys\": \"%s\",", SHOW_STAT(sys));
	WRITELN(client, "\t\"nic\": \"%s\",", SHOW_STAT(nic));
	WRITELN(client, "\t\"idle\": \"%s\",", SHOW_STAT(idle));
	WRITELN(client, "\t\"io\": \"%s\",", SHOW_STAT(iowait));
	WRITELN(client, "\t\"irq\": \"%s\",", SHOW_STAT(irq));
	WRITELN(client, "\t\"sirq\": \"%s\"", SHOW_STAT(softirq));
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
		used = total - mfree;
		WRITELN(client, "\t\"used\": \"%luK\",", used);
		WRITELN(client, "\t\"free\": \"%luK\",", mfree);
		WRITELN(client, "\t\"buff\": \"%luK\",", buffers);
		WRITELN(client, "\t\"cached\": \"%luK\",", cached);
		g_strfreev(lines);
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
		WRITELN(client, "\t\"avg\": \"%s\",", g_strstrip(str));
		g_free(str);
	}
	WRITELN(client, "  }");
}
#endif //#ifdef SYS_STAT
	WRITELN(client, "\r\n}");

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

gboolean
serve_page(MediaURL *url, GstHTTPClient *client, gpointer data)
{
	FILE *fp;
	char buf[1024];
	int sz;
	struct stat sb;
	char *timestr;
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
	  || !(fp = fopen(physpath, "r")))
	{
		goto err;
	}
	if (!S_ISREG(sb.st_mode)) {
		goto err;
	}
	/* obtain mimetype */
	mimetype = mime_lookup(physpath);

	GST_INFO("Serving %d byte %s to %s:%d as %s", sb.st_size, physpath,
		client->peer_ip, client->port, mimetype);

	timestr = ctime(&sb.st_mtime);
	timestr[strlen(timestr)-1] = 0;
	gst_http_client_writeln(client, "Last-Modified: %s", timestr);
	gst_http_client_writeln(client, "Content-Length: %ld", sb.st_size);
	gst_http_client_writeln(client, "Content-Type: %s", mimetype);
	gst_http_client_writeln(client, "");

	while (!feof(fp)) {
		sz = fread(buf, 1, sizeof(buf)-1, fp);
		if (sz < 0)
			break;
		buf[sz] = 0;
		gst_http_client_write(client, "%s", buf);
	}
	fflush(fp);
	fclose(fp);
	
	free(physpath);
	g_free(path);
	return TRUE;

err:
	free(physpath);
	g_free(path);
	gst_http_client_writeln(client, "404 Not Found");
	return TRUE;
}
#endif // #ifdef LOCAL_PAGES

int
main (int argc, char *argv[])
{
	gchar *address = "0.0.0.0";
	void *gst_handle = NULL;
	gchar *service = "8080";
	gchar *docroot = NULL;
	gchar *sysadmin = NULL;
	gchar *pidfile = NULL;
	GstHTTPServer *server;
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

	/* init gstreamer and create mainloop */
	gst_init (&argc, &argv);
	loop = g_main_loop_new (NULL, FALSE);

	GST_DEBUG_CATEGORY_INIT (gst_http_debug, "gst_http", 0, "gst_http");

	if (!g_thread_supported())
		g_thread_init(NULL);

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

	/* create a server instance */
	server = gst_http_server_new ();
	gst_http_server_set_address (server, address);
	gst_http_server_set_service (server, service);

	/* parse configfile */
	if (configfile) {
		parse_config(server, configfile);
	}

	/* parse commandline arguments */
	i = 1;
	while ( (argc - i) >= 2) {
		gst_http_server_add_mapping_pipe ( server, argv[i], "", argv[i+1]);
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
	gst_http_server_add_mapping_func ( server, "v4l2-ctl",
		"Video Controls", v4l2_config, server);
#endif
	if (sysadmin) {
		gst_http_server_add_mapping_func ( server, sysadmin,
			"Server Status", server_status, server);
	}
#ifdef LOCAL_PAGES
	/* default to a page handler that serves from docroot */
	if (docroot) {
			char *real = realpath(docroot, NULL);
			if (real) {
				gst_http_server_add_mapping_func ( server, "*", NULL, serve_page, real);
			} else {
				g_print ("Error: docroot '%s' not found\n", docroot);
			}
	}
#endif // #ifdef LOCAL_PAGES

	/* make sure we have a valid configuration */
	if (gst_http_server_num_mappings(server) == 0) {
		g_print ("Error: no streams defined\n");
		g_print ("%s\n", g_option_context_get_help(ctx, 0, NULL));
		return -1;
	}
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

	g_main_loop_unref(loop);

	gst_deinit();

	return 0;
}
