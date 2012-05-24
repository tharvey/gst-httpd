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
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
//#include <asm/types.h>

#include <linux/videodev2.h>
#include <gst/gst.h>

#include "http-client.h"
#include "http-server.h"
#include "v4l2-ctl.h"

static int
xioctl (int fd, int request, void *arg)
{
	int r;

	do r = ioctl (fd, request, arg);
	while (-1 == r && EINTR == errno);

	return r;
}

#define WRITE(x, args...)  gst_http_client_write(x, args)
#define WRITELN(x, args...)  gst_http_client_writeln(x, args)

static void
enumerate_menu (int fd, struct v4l2_queryctrl *q, GstHTTPClient *c)
{
	struct v4l2_querymenu querymenu;
	int i;

        //printf ("  Menu items:\n");
	WRITE(c, "\t\t\"menu\": \"");

        memset (&querymenu, 0, sizeof (querymenu));
        querymenu.id = q->id;

	i = 0;
        for (querymenu.index = q->minimum;
             querymenu.index <= q->maximum;
              querymenu.index++) {
                if (0 == ioctl (fd, VIDIOC_QUERYMENU, &querymenu)) {
                        //printf ("  %s\n", querymenu.name);
			WRITE(c, (i++ > 0)?",":"");
			WRITE(c, "%d:%s", querymenu.index, querymenu.name);
                } else {
                        perror ("VIDIOC_QUERYMENU");
                        exit (EXIT_FAILURE);
                }
        }
	WRITELN(c, "\",");
}

static const char *
v4l2_type_str(int type) {
	switch (type) {
	case V4L2_CTRL_TYPE_INTEGER: return "int"; break;
	case V4L2_CTRL_TYPE_BOOLEAN: return "bool"; break;
	case V4L2_CTRL_TYPE_MENU: return "menu"; break;
	case V4L2_CTRL_TYPE_BUTTON: return "button"; break;
	case V4L2_CTRL_TYPE_INTEGER64: return "int64"; break;
	case V4L2_CTRL_TYPE_CTRL_CLASS: return "control"; break;
	}
	return "unknown";
}

static const char *
v4l2_name_str(struct v4l2_queryctrl *q)
{
	static char name[32];
	int i;

	strncpy(name, q->name, sizeof(name));
	for (i = 0; name[i]; i++) {
		if (isalnum(name[i])) {
			name[i] = tolower(name[i]);
		} else {
			name[i] = '_';
		}
	}
	return name;
}

static void
v4l2_control(int fd, struct v4l2_queryctrl *q, GstHTTPClient *c, int *i)
{
	long val;
	struct v4l2_control control;

	if (q->flags & V4L2_CTRL_FLAG_DISABLED)
		return;

	memset (&control, 0, sizeof (control));
	if (q->type == V4L2_CTRL_TYPE_INTEGER64 ||
	    q->type == V4L2_CTRL_TYPE_STRING ||
            (V4L2_CTRL_ID2CLASS(q->id) != V4L2_CTRL_CLASS_USER &&
             q->id < V4L2_CID_PRIVATE_BASE))
	{
	}
	else {
		control.id = q->id;
		//control.value = q->default_value;
		if (ioctl (fd, VIDIOC_G_CTRL, &control)) {
			perror ("VIDIOC_G_CTRL");
		} else {
		}
	}

	//printf ("Control %s\n", q->name);

	WRITE(c, ((*i)++ > 0)?",\r\n\t":"\t");
	WRITELN(c, "{");
	//WRITELN(c, "\t\t\"name\": \"%s\",", v4l2_name_str(q));
	WRITELN(c, "\t\t\"name\" : \"%s\",", q->name);
	WRITELN(c, "\t\t\"id\"   : \"0x%x\",", q->id);
	WRITELN(c, "\t\t\"flags\": \"0x%x\",", q->flags);
	WRITELN(c, "\t\t\"type\" : \"%s\",", v4l2_type_str(q->type));
	WRITELN(c, "\t\t\"val\"  : \"%d\",", control.value);
	WRITELN(c, "\t\t\"min\"  : \"%d\",", q->minimum);
	WRITELN(c, "\t\t\"max\"  : \"%d\",", q->maximum);
	WRITELN(c, "\t\t\"step\" : \"%d\",", q->step);
	if (q->type == V4L2_CTRL_TYPE_MENU)
		enumerate_menu (fd, q, c);
	WRITELN(c, "\t\t\"priv\": \"%s\"", (q->id >= V4L2_CID_PRIVATE_BASE)?
		"true":"false");
	WRITE(c, "\t}");
}

static int
find_control(int fd, const char *name)
{
	struct v4l2_queryctrl queryctrl;
	struct v4l2_querymenu querymenu;
	int i = 0;

	memset (&queryctrl, 0, sizeof (queryctrl));
	for (queryctrl.id = V4L2_CID_BASE;
			queryctrl.id < V4L2_CID_LASTP1;
			queryctrl.id++)
	{
		if (0 == ioctl (fd, VIDIOC_QUERYCTRL, &queryctrl)) {
			if ( (strcmp(name, queryctrl.name) == 0)
		    || (strcmp(name, v4l2_name_str(&queryctrl)) == 0) )
			{
				return queryctrl.id;
			}
		} else {
			if (errno == EINVAL)
				continue;

			perror ("VIDIOC_QUERYCTRL");
		}
	}

	for (queryctrl.id = V4L2_CID_PRIVATE_BASE;;
			queryctrl.id++)
	{
		if (0 == ioctl (fd, VIDIOC_QUERYCTRL, &queryctrl)) {
			if ( (strcmp(name, queryctrl.name) == 0)
		    || (strcmp(name, v4l2_name_str(&queryctrl)) == 0) )
			{
				return queryctrl.id;
			}
		} else {
			if (errno == EINVAL)
				break;

			perror ("VIDIOC_QUERYCTRL");
		}
	}

	return -1;
}

static void
enumerate_controls(int fd, GstHTTPClient *c)
{
	struct v4l2_queryctrl queryctrl;
	struct v4l2_querymenu querymenu;
	int i = 0;

	WRITELN(c, "no-cache");
	WRITELN(c, "Content-Type: application/json");
	WRITELN(c, "");

	WRITELN(c, "{");
	WRITELN(c, "  \"controls\": [");

	memset (&queryctrl, 0, sizeof (queryctrl));

	for (queryctrl.id = V4L2_CID_BASE;
			queryctrl.id < V4L2_CID_LASTP1;
			queryctrl.id++)
	{
		if (0 == ioctl (fd, VIDIOC_QUERYCTRL, &queryctrl)) {
			v4l2_control(fd, &queryctrl, c, &i);
		} else {
			if (errno == EINVAL)
				continue;

			perror ("VIDIOC_QUERYCTRL");
		}
	}

	for (queryctrl.id = V4L2_CID_PRIVATE_BASE;;
			queryctrl.id++)
	{
		if (0 == ioctl (fd, VIDIOC_QUERYCTRL, &queryctrl)) {
			v4l2_control(fd, &queryctrl, c, &i);
		} else {
			if (errno == EINVAL)
				break;

			perror ("VIDIOC_QUERYCTRL");
		}
	}

	WRITE(c, "\r\n  ]");
	WRITELN(c, "}");
}

gboolean
v4l2_config(MediaURL *url, GstHTTPClient *client, gpointer data)
{
	int fd;
	gchar *dev, *name, *id, *value;

	printf("Serving v4l2_config to %s:%d\n", client->peer_ip, client->port);
	dev = get_query_field(url, "device");
	if (!dev)
		dev = g_strdup("/dev/video0");
	printf("dev='%s'\n", dev);

	fd = open (dev, O_RDWR | O_NONBLOCK, 0);
	if (-1 == fd) {
		perror("open");
		goto err;
	}	

	id = get_query_field(url, "id");
	name = get_query_field(url, "name");
	value = get_query_field(url, "value");
	if ( (id || name) && value) {
		struct v4l2_queryctrl queryctrl;
		struct v4l2_control ctrl;
		const char *result;

		memset (&queryctrl, 0, sizeof (queryctrl));
		if (name) {
			int val;

			if ((val = find_control(fd, name)) == -1)
				goto err;
			queryctrl.id = val;
		} else {
			queryctrl.id = strtol(id, NULL, 0);
		}
		if (ioctl(fd, VIDIOC_QUERYCTRL, &queryctrl)) {
			perror("VIDIOC_QUERYCTRL");
		}

		memset (&ctrl, 0, sizeof (ctrl));
		ctrl.id = queryctrl.id;
		ctrl.value = strtol(value, NULL, 0);

		printf("Setting %s (0x%x) to %d\n", id, ctrl.id, ctrl.value);
		result = "Ok";
		if (ioctl(fd, VIDIOC_S_CTRL, &ctrl)) {
			perror("VIDIOC_S_CTRL");
			gst_http_client_writeln(client, "500 Error");
			result = "Error";
		} else {
			gst_http_client_writeln(client, "200 Ok");
		}
		WRITELN(client, "");
		WRITELN(client, "%s (0x%x) set to %d - %s",
			id, ctrl.id, ctrl.value, result);
	}

	else {
		enumerate_controls(fd, client);
	}

	g_free(dev);
	g_free(id);
	g_free(name);
	g_free(value);
	return TRUE;

err:
	g_free(dev);
	gst_http_client_writeln(client, "404 Not Found");
	return TRUE;
}


