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

#ifndef _RATE_H_
#define _RATE_H_

/*
 * track samples over a sliding window
 *   - sample is whatever units you feed in
 */
#define AVG_WINDOW 1 // seconds to average over

struct avg {
	unsigned long total; // total sample count
	unsigned long avg;   // integer average (samples/second)

	/* accounting */
	unsigned int idx;
	unsigned int _idx;
	unsigned long window[AVG_WINDOW+1];
	time_t lastupdate;
};

unsigned long avg_add_samples(struct avg*, unsigned long);
unsigned long avg_get_avg(struct avg*);

#endif /* _RATE_H_ */
