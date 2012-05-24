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

#include <time.h>

#include "rate.h"

unsigned long
avg_add_samples(struct avg* pavg, unsigned long val)
{
	int i;
	time_t now = time(NULL);

	if (pavg->lastupdate > now 
	 || now - pavg->lastupdate > 0
	 || pavg->_idx == 0)
	{
		// TODO: this is incorrect for Windows sizes > 1
		// - need to increment by number of seconds passed and clear
		//   all windows in between
		pavg->idx++;
		pavg->lastupdate = now;
		if (pavg->_idx < AVG_WINDOW+1)
			pavg->_idx++;
		if (pavg->idx >= AVG_WINDOW+1)
			pavg->idx = 0;
		pavg->window[pavg->idx] = 0;
	}

	pavg->window[pavg->idx] += val;
	pavg->total += val;

	/* recalc average over window */
	pavg->avg = 0;
	if (pavg->_idx > 1) {
		for (i = 0; i < AVG_WINDOW+1; i++) {
			if (i != pavg->idx)
				pavg->avg += pavg->window[i];
		}
		if (pavg->_idx > 2)
			pavg->avg /= (pavg->_idx - 1);
	}
	return pavg->avg;
}

unsigned long
avg_get_avg(struct avg *pavg)
{
	return pavg->avg;
}
