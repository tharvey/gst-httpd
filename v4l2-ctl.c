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
#include <ctype.h>
#include <sys/ioctl.h>

#include <linux/videodev2.h>
#include <gst/gst.h>

#include "http-client.h"
#include "http-server.h"
#include "media-mapping.h"
#include "v4l2-ctl.h"

#define DPRINTF(x, args...) fprintf(stdout, x, args)
#define WRITE(x, args...)  gst_http_client_write(x, args)
#define WRITELN(x, args...)  gst_http_client_writeln(x, args)

/* some private CID's */
#define V4L2_PRIV_JPEGQUAL 	(V4L2_CID_PRIVATE_BASE+100)
/* these are really driver dependent but there is no API to query them */
#define JPEGQUAL_MIN 20
#define JPEGQUAL_MAX 95
#define JPEGQUAL_DEF 85

/** v4l2_control_type_str - return a static string describing the control type
 * @param type
 * @returns static string
 */
static const char *
v4l2_control_type_str(int type)
{
	switch (type) {
	case V4L2_CTRL_TYPE_INTEGER:    return "int"; break;
	case V4L2_CTRL_TYPE_BOOLEAN:    return "bool"; break;
	case V4L2_CTRL_TYPE_MENU:       return "menu"; break;
	case V4L2_CTRL_TYPE_BUTTON:     return "button"; break;
	case V4L2_CTRL_TYPE_INTEGER64:  return "int64"; break;
	case V4L2_CTRL_TYPE_CTRL_CLASS: return "control"; break;
	}
	return "unknown";
}


/** v4l2_control_class_str - return a static string describing the control class 
 * @param id 
 * @returns static string
 */
static const char *
v4l2_control_class_str(int id)
{
	switch (V4L2_CTRL_ID2CLASS(id)) {
	case V4L2_CTRL_CLASS_USER:    return "user"; break;
	case V4L2_CTRL_CLASS_MPEG:    return "mpeg"; break;
	case V4L2_CTRL_CLASS_CAMERA:  return "camera"; break;
	case V4L2_CTRL_CLASS_FM_TX:   return "fm_tx"; break;
	}
	return "unknown";
}


/** v4l2_control_name_str - return a sanitized name
 * @param q - control
 * @returns sanitized string - all lowercase and underscores for any non-alpha
 */
static const char *
v4l2_control_name_str(struct v4l2_queryctrl *q)
{
	static char name[32];
	int i;

	strncpy(name, (const char*) q->name, sizeof(name));
	for (i = 0; name[i]; i++) {
		if (isalnum(name[i])) {
			name[i] = tolower(name[i]);
		} else {
			name[i] = '_';
		}
	}
	return name;
}


/** find_control - find a v4l2 control matching 'name'
 * @param fd - file descriptor of v4l2 device
 * @param name of control
 * @returns id of control or -1 if not found
 */
static int
find_control(int fd, const char *name)
{
	struct v4l2_queryctrl queryctrl;

	memset (&queryctrl, 0, sizeof (queryctrl));
	for (queryctrl.id = V4L2_CID_BASE;
			queryctrl.id < V4L2_CID_LASTP1;
			queryctrl.id++)
	{
		if (0 == ioctl (fd, VIDIOC_QUERYCTRL, &queryctrl)) {
			if ( (strcmp(name, (const char*) queryctrl.name) == 0)
		    || (strcmp(name, v4l2_control_name_str(&queryctrl)) == 0) )
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
			if ( (strcmp(name, (const char *)queryctrl.name) == 0)
		    || (strcmp(name, v4l2_control_name_str(&queryctrl)) == 0) )
			{
				return queryctrl.id;
			}
		} else {
			if (errno == EINVAL)
				break;

			perror ("VIDIOC_QUERYCTRL");
		}
	}

	if (strncmp(name, "jpeg_quality", 12) == 0)
		return V4L2_PRIV_JPEGQUAL;
		
	return -1;
}


/** enumerate_menu - send JSON description of v4l2 menu control options
 * @param fd - file descriptor of v4l2 device
 * @param q - control
 * @param c - client to send JSON response to
 */
static void
enumerate_menu (int fd, struct v4l2_queryctrl *q, GstHTTPClient *c)
{
	struct v4l2_querymenu querymenu;
	int i;

	WRITE(c, "\t\t\"menu\": \"");

	memset (&querymenu, 0, sizeof (querymenu));
	querymenu.id = q->id;

	i = 0;
	for (querymenu.index = q->minimum;
	     querymenu.index <= q->maximum;
	     querymenu.index++)
	{
		if (0 == ioctl (fd, VIDIOC_QUERYMENU, &querymenu)) {
			WRITE(c, (i++ > 0)?",":"");
			WRITE(c, "%d:%s", querymenu.index, querymenu.name);
		} else {
			perror ("VIDIOC_QUERYMENU");
		}
	}
	WRITELN(c, "\",");
}


/** v4l2_control - send JSON description of a v4l2 control 
 * @param fd - file descriptor of v4l2 device
 * @param q - control
 * @param c - client to send JSON response to
 * @param i - pointer to num items
 */
static void
v4l2_control(int fd, struct v4l2_queryctrl *q, GstHTTPClient *c, int *i)
{
	struct v4l2_control control;

	if (q->flags & V4L2_CTRL_FLAG_DISABLED)
		return;

	memset (&control, 0, sizeof (control));
/*
	if (q->type == V4L2_CTRL_TYPE_INTEGER64 ||
	    q->type == V4L2_CTRL_TYPE_STRING ||
            (V4L2_CTRL_ID2CLASS(q->id) != V4L2_CTRL_CLASS_USER &&
             q->id < V4L2_CID_PRIVATE_BASE))
	{
	}
	else {
*/
		control.id = q->id;
		if (ioctl (fd, VIDIOC_G_CTRL, &control)) {
			perror ("VIDIOC_G_CTRL");
		}
/*
	}
*/

	WRITE(c, ((*i)++ > 0)?",\r\n\t":"\t");
	WRITELN(c, "{");
	//WRITELN(c, "\t\t\"name\": \"%s\",", v4l2_control_name_str(q));
	WRITELN(c, "\t\t\"name\" : \"%s\",", q->name);
	WRITELN(c, "\t\t\"id\"   : \"0x%x\",", q->id);
	//WRITELN(c, "\t\t\"flags\": \"0x%x\",", q->flags);
	WRITELN(c, "\t\t\"type\" : \"%s\",", v4l2_control_type_str(q->type));
	WRITELN(c, "\t\t\"val\"  : \"%d\",", control.value);
	WRITELN(c, "\t\t\"min\"  : \"%d\",", q->minimum);
	WRITELN(c, "\t\t\"max\"  : \"%d\",", q->maximum);
	WRITELN(c, "\t\t\"step\" : \"%d\",", q->step);
	if (q->type == V4L2_CTRL_TYPE_MENU)
		enumerate_menu (fd, q, c);
	//WRITELN(c, "\t\t\"priv\": \"%s\",", (q->id >= V4L2_CID_PRIVATE_BASE)?"true":"false");
	WRITELN(c, "\t\t\"class\": \"%s\"", v4l2_control_class_str(q->id));
	WRITE(c, "\t}");
}


/** fcc2s - FourCC to string
 */
static const char*
fcc2s(unsigned int val)
{
	static char str[5];

	sprintf(str, "%C%C%C%C",
		val & 0xff,
		(val >> 8) & 0xff,
		(val >> 16) & 0xff,
		(val >> 24) & 0xff);

	return str;
}


/** enumerate_controls - send a JSON description of all v4l2 controls
 *   and device capabilities
 * @param fd - file descriptor of v4l2 device
 * @client c - client to send JSON response to
 */
static void
enumerate_controls(int fd, GstHTTPClient *c)
{
	struct v4l2_queryctrl queryctrl;
	struct v4l2_jpegcompression jc;
	struct v4l2_fmtdesc fmt;
	struct v4l2_frmsizeenum frmsize;
	struct v4l2_capability cap;
//	struct v4l2_frmivalenum frmival;
	int i = 0;

	WRITELN(c, "no-cache");
	WRITELN(c, "Content-Type: application/json\r\n");

	WRITELN(c, "{");

	WRITELN(c, "  \"device\": [");
	if (ioctl(fd, VIDIOC_QUERYCAP, &cap) >= 0) {
		WRITELN(c, "\t{");
		WRITELN(c, "\t\t\"name\" : \"%s\",", "");
		WRITELN(c, "\t\t\"driver\" : \"%s\",", cap.driver);
		WRITELN(c, "\t\t\"card\" : \"%s\",", cap.card);
		WRITELN(c, "\t\t\"bus\" : \"%s\",", cap.bus_info);
		WRITELN(c, "\t\t\"version\" : \"%d.%d.%d\",", 
			(cap.version>>16) & 0xff,
			(cap.version>> 8) & 0xff,
 			cap.version      & 0xff);
		WRITELN(c, "\t\t\"capabilities\" : \"0x%08x\"", cap.capabilities);
		WRITELN(c, "\t}");
	}
	WRITE(c, "\r\n  ],");

	WRITELN(c, "  \"formats\": [");
	fmt.index = 0;
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	i = 0;
	while (ioctl(fd, VIDIOC_ENUM_FMT, &fmt) >= 0) {
		WRITE(c, (i++ > 0)?",\r\n\t":"\t");
		WRITELN(c, "{");
		WRITELN(c, "\t\t\"name\" : \"%s\",", fmt.description);
		WRITELN(c, "\t\t\"id\"   : \"%d\",", fmt.index);
		WRITE(c, "\t\t\"pixel_format\"  : \"%s\"", fcc2s(fmt.pixelformat));
		frmsize.pixel_format = fmt.pixelformat;
		frmsize.index = 0;
		while (ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &frmsize) >= 0) {
			if (frmsize.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
/*
				frmival.index =0;
				frmival.pixel_format = fmt.pixelformat;
				frmival.width = frmsize.discrete.width;
				frmival.height = frmsize.discrete.height;
				while (ioctl(fd, VIDIOC_ENUM_FRAMEINTERVALS, &frmival) >= 0) {
					WRITE(c, (i++ > 0)?",\r\n\t":"\t");
					frmival.index++;
				}
*/
				WRITE(c, (i++ > 0)?",\r\n":"");
				WRITE(c, "\t\t\"size%d\" : \"%dx%d\"", frmsize.index, frmsize.discrete.width, frmsize.discrete.height);
			}
			frmsize.index++;
		}
		WRITE(c, "\r\n");
		WRITE(c, "\t}");
		fmt.index++;
	}
	WRITE(c, "\r\n  ],");

	WRITELN(c, "  \"controls\": [");

	i = 0;
	memset (&queryctrl, 0, sizeof (queryctrl));

	// enumerate extended controls (if fail use old method)
	//queryctrl.id = V4L2_CTRL_CLASS_CAMERA | V4L2_CTRL_FLAG_NEXT_CTRL;
	queryctrl.id = V4L2_CTRL_FLAG_NEXT_CTRL;
	if (0 == ioctl (fd, VIDIOC_QUERYCTRL, &queryctrl)) {
		queryctrl.id = V4L2_CTRL_FLAG_NEXT_CTRL;
		while (0 == ioctl (fd, VIDIOC_QUERYCTRL, &queryctrl)) {
			v4l2_control(fd, &queryctrl, c, &i);
			queryctrl.id |= V4L2_CTRL_FLAG_NEXT_CTRL;
		}
	}

	else {
		for (queryctrl.id = V4L2_CID_BASE;
				queryctrl.id < V4L2_CID_LASTP1;
				queryctrl.id++)
		{
			if (0 == ioctl (fd, VIDIOC_QUERYCTRL, &queryctrl)) {
/*
				if (queryctrl.type == V4L2_CTRL_TYPE_INTEGER64 ||
	    			queryctrl.type == V4L2_CTRL_TYPE_STRING ||
           			 (V4L2_CTRL_ID2CLASS(queryctrl.id) != V4L2_CTRL_CLASS_USER &&
             			queryctrl.id < V4L2_CID_PRIVATE_BASE))
					continue;
*/
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
/*
				if (queryctrl.type == V4L2_CTRL_TYPE_INTEGER64 ||
	    			queryctrl.type == V4L2_CTRL_TYPE_STRING ||
           			 (V4L2_CTRL_ID2CLASS(queryctrl.id) != V4L2_CTRL_CLASS_USER &&
             			queryctrl.id < V4L2_CID_PRIVATE_BASE))
					continue;
*/
				v4l2_control(fd, &queryctrl, c, &i);
			} else {
				if (errno == EINVAL)
					break;
	
			perror ("VIDIOC_QUERYCTRL");
			}
		}
	}

	/* VIDIOC_G_JPEGCOMP */
	if (0 == ioctl (fd, VIDIOC_G_JPEGCOMP, &jc)) {
		WRITE(c, (i++ > 0)?",\r\n\t":"\t");
		WRITELN(c, "{");
		WRITELN(c, "\t\t\"name\" : \"%s\",", "jpeg_quality");
		WRITELN(c, "\t\t\"id\"   : \"0x%x\",", V4L2_PRIV_JPEGQUAL);
		WRITELN(c, "\t\t\"type\" : \"%s\",", v4l2_control_type_str(V4L2_CTRL_TYPE_INTEGER));
		WRITELN(c, "\t\t\"val\"  : \"%d\",", jc.quality);
		WRITELN(c, "\t\t\"min\"  : \"%d\",", JPEGQUAL_MIN);
		WRITELN(c, "\t\t\"max\"  : \"%d\",", JPEGQUAL_MAX);
		WRITELN(c, "\t\t\"step\" : \"%d\",", 5);
		WRITELN(c, "\t\t\"class\": \"%s\"", "custom");
		WRITE(c, "\t}");
	}

	WRITE(c, "\r\n  ]");
	WRITELN(c, "}");
}


static int
set_control(int fd, int id, int *val)
{
	struct v4l2_queryctrl queryctrl;
	struct v4l2_control ctrl;
	int ret;

	memset (&queryctrl, 0, sizeof (queryctrl));
	queryctrl.id = id;
	if ((ret = ioctl (fd, VIDIOC_QUERYCTRL, &queryctrl))) {
		return ret;
	}
	memset (&ctrl, 0, sizeof (ctrl));
	ctrl.id = id;
	if (val)
		ctrl.value = *val;
	else
		ctrl.value = queryctrl.default_value;
	DPRINTF("Setting %s(0x%x)=%d\n", queryctrl.name, id, ctrl.value);
	return (ioctl(fd, VIDIOC_S_CTRL, &ctrl));
}

/** v4l2_config - get/set v4l2 device controls
 * @param url
 * @param client to respond to
 * @data pointer to server
 *
 * URL Query must be of format [v4l2-id|v4l2-control-name]=<value>.
 * If no query, will return JSON response for all controls
 * A query of 'defaults' will set default values for all controls
 * 
 */
gboolean
v4l2_config(MediaURL *url, GstHTTPClient *client, gpointer data)
{
	int fd;
	int matched = 0;
	gchar *dev;

	dev = get_query_field(url, "device");
	if (!dev)
		dev = g_strdup("/dev/video0");
	DPRINTF("Serving v4l2_config to %s:%d dev=%s\n",
		client->peer_ip, client->port, dev);

	fd = open (dev, O_RDWR | O_NONBLOCK, 0);
	if (-1 == fd) {
		fprintf(stderr, "open '%s' failed: %s (%d)", dev, strerror(errno), errno);
		WRITELN(client, "404 Not Found");
		g_free(dev);
		return TRUE;
	}

	/* reset to default config */
	if (url->query && strstr(url->query, "defaults")) {
		struct v4l2_jpegcompression jc;
		int i = 0;

		DPRINTF("resetting %s to defaults\n", dev);
		for (i = V4L2_CID_BASE; i < V4L2_CID_LASTP1; i++)
			if (set_control(fd, i, NULL))
				continue;

		for (i = V4L2_CID_PRIVATE_BASE;; i++)
			if (set_control(fd, i, NULL))
				break;

		jc.quality = JPEGQUAL_DEF;
		ioctl (fd, VIDIOC_S_JPEGCOMP, &jc);

		WRITELN(client, "200 Ok\r\n");
		WRITELN(client, "Reset controls");
		goto out;
	}

	/* get/set control(s) */
	if (url->querys) {
		struct v4l2_queryctrl queryctrl;
		char *name, *value;
		int i, ret, ival;
		unsigned char header_sent = 0;

		memset (&queryctrl, 0, sizeof (queryctrl));
		for (i = 0; url->querys[i]; i++) {
			name = strtok(url->querys[i], "=");
			value = strtok(NULL, "=");
			if (!name || !value)
				continue;
			queryctrl.id = strtol(name, NULL, 0);
			if (queryctrl.id >= V4L2_CID_BASE) {
				if (ioctl(fd, VIDIOC_QUERYCTRL, &queryctrl)) {
					perror("VIDIOC_QUERYCTRL");
				} else
					name = (char *)queryctrl.name;
			} else if ((queryctrl.id = find_control(fd, name)) == -1) {
				continue;
			}

			matched++;
			ival = strtol(value, NULL, 0);
			ret = 0;

			if (queryctrl.id >= V4L2_CID_PRIVATE_BASE) {
				switch (queryctrl.id) {
					case V4L2_PRIV_JPEGQUAL: {
						struct v4l2_jpegcompression jc;
						jc.quality = strtol(value, NULL, 0);
						if (0 != (ret = ioctl (fd, VIDIOC_S_JPEGCOMP, &jc)))
							perror("VIDIOC_S_JPEGCOMP");
					}		break;
				}
			}

			else {
				struct v4l2_control ctrl;
				memset (&ctrl, 0, sizeof (ctrl));
				ctrl.id = queryctrl.id;
				ctrl.value = ival;

				if (0 != (ret = ioctl(fd, VIDIOC_S_CTRL, &ctrl)))
					perror("VIDIOC_S_CTRL");
			}

			if (ret) {
				if (header_sent++ == 0) {
					WRITELN(client, "500 Error\r\n");
				}
				WRITELN(client, "Failed setting %s (0x%x) to %d",
					name, queryctrl.id, ival);
			} else {
				if (header_sent++ == 0) {
					WRITELN(client, "200 Ok\r\n");
				}
				WRITELN(client, "%s (0x%x) set to %d",
					name, queryctrl.id, ival);
			}

		}
	}

	if (!matched)	{
		enumerate_controls(fd, client);
	}

out:
	close(fd);
	g_free(dev);
	return TRUE;
}


/** v4l2_config_device - create media mappings based on device capabilities
 * @param dev - video device (ie /dev/video0)
 * @param mapping - media mapping
 */
gboolean
v4l2_config_device(const gchar *dev, GstHTTPMediaMapping *mapping, const gchar *inputdev)
{
	GstHTTPMedia *media;
	struct v4l2_fmtdesc fmt;
	struct v4l2_frmsizeenum frmsize;
	int fd;
	int w, h, lw, lh;
	int fmtidx = -1;
	gchar *mediafmt;
	gchar *enc;

	fd = open (dev, O_RDWR | O_NONBLOCK, 0);
	if (-1 == fd) {
		fprintf(stderr, "open '%s' failed: %s (%d)", dev, strerror(errno), errno);
		return FALSE;
	}

	/* determine best format (prefer MJPG/JPEG which needs no encoding) */
	fmt.index = 0;
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	while (ioctl(fd, VIDIOC_ENUM_FMT, &fmt) >= 0) {
		printf("%d:%s:%s\n", fmt.index, fmt.description, fcc2s(fmt.pixelformat));
		switch (fmt.pixelformat) {
			case V4L2_PIX_FMT_MJPEG:
			case V4L2_PIX_FMT_JPEG:
				mediafmt = "image/jpeg";
				fmtidx = fmt.index;
				enc = "";
				break;
			case V4L2_PIX_FMT_YUYV:
			case V4L2_PIX_FMT_SN9C10X:
			case V4L2_PIX_FMT_SN9C20X_I420:
				if (fmtidx == -1) {
					fmtidx = fmt.index;
					mediafmt = "video/x-raw-yuv";
					enc = " ! jpegenc";
				}
				break;
			default:
				if (fmtidx == -1) {
					fmtidx = fmt.index;
					mediafmt = "video/x-raw-rgb";
					enc = " ! jpegenc";
				}
				break;
		}
		fmt.index++;
	}

	/* find framesizes from smallest to largest */
	fmt.index = fmtidx;
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	w = h = 0;
	while (1) {
		lw = w;
		lh = h;
		w = 9999;
		h = 9999;
		frmsize.pixel_format = fmt.pixelformat;
		frmsize.index = 0;
		while (ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &frmsize) >= 0) {
			if (frmsize.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
				if (frmsize.discrete.width > lw && frmsize.discrete.height > lh) {
					if (w > frmsize.discrete.width || h > frmsize.discrete.height) {
						w = frmsize.discrete.width;
						h = frmsize.discrete.height;
					}
				}
			}
			frmsize.index++;
		}
		if (w == 9999 && h == 9999)
			break;
		gchar *desc = g_strdup_printf("%dx%d %s", w, h, fmt.description);
		gchar *path = g_strdup_printf("%dx%d", w, h);
		gchar *pipe = g_strdup_printf("v4l2src device=%s ! %s,width=%d,height=%d%s", dev, mediafmt, w, h, enc);
		media = gst_http_media_new_pipeline (desc, pipe, inputdev);
		gst_http_media_mapping_add (mapping, path, media);
		g_free(desc);
		g_free(path);
		g_free(pipe);
	}

	return TRUE;
}
