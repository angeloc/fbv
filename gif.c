/*
    fbv  --  simple image viewer for the linux framebuffer
    Copyright (C) 2000, 2001, 2003  Mateusz Golicz

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/
#include "config.h"

#ifdef FBV_SUPPORT_GIF
#include "fbv.h"
#include <stdlib.h>
#include <string.h>
#include <gif_lib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

struct gif_frame
{
	unsigned char *rgb;
	unsigned char *alpha;
	int delay;	/* 1/100 s */
};

static struct gif_frame *frames;
static int framecount, frameix;
static int loops, loopcount;	/* loops: 0 = forever */

int fh_gif_id(char *name)
{
	int fd;
	char id[4];
	fd = open(name, O_RDONLY);
	if(fd==-1) return(0);
	read(fd, id, 4);
	close(fd);
	if(id[0]=='G' && id[1]=='I' && id[2]=='F') return(1);
	return(0);
}

static void fill_rect(unsigned char *rgb, unsigned char *alpha, int x, int y,
		      int left, int top, int w, int h, const unsigned char *bg)
{
	int i, j;

	for(i = max(top, 0); i < min(top + h, y); i++)
		for(j = max(left, 0); j < min(left + w, x); j++)
		{
			memcpy(rgb + (i * x + j) * 3, bg, 3);
			alpha[i * x + j] = 0;
		}
}

void fh_gif_unload(void)
{
	int i;

	for(i = 0; i < framecount; i++)
	{
		free(frames[i].rgb);
		free(frames[i].alpha);
	}
	free(frames);
	frames = NULL;
	framecount = frameix = 0;
}

int fh_gif_load(char *name, unsigned char *buffer, unsigned char **alpha, int x, int y)
{
	static const int in_beginrow[4] = {0, 4, 2, 1};
	static const int in_nextrow[4] = {8, 8, 4, 2};
	GifFileType *gft;
	GifRecordType rt;
	GifByteType *ext;
	ColorMapObject *cmap;
	unsigned char *canvas = NULL, *canvas_alpha = NULL;
	unsigned char *prev = NULL, *prev_alpha = NULL;
	unsigned char *line = NULL;
	unsigned char bg[3] = {0, 0, 0};
	int transparency = -1, disposal = 0, delay = 0, transparent = 0;
	int extcode, err, ret = FH_ERROR_FORMAT;
	int i, j, k;

	fh_gif_unload();
	loops = 1;
	loopcount = 0;

	if(!(gft = DGifOpenFileName(name, &err)))
		return(FH_ERROR_FILE);

	canvas = malloc(x * y * 3);
	canvas_alpha = malloc(x * y);
	prev = malloc(x * y * 3);
	prev_alpha = malloc(x * y);
	if(!canvas || !canvas_alpha || !prev || !prev_alpha)
		goto out;

	if(gft->SColorMap && gft->SBackGroundColor < gft->SColorMap->ColorCount)
	{
		bg[0] = gft->SColorMap->Colors[gft->SBackGroundColor].Red;
		bg[1] = gft->SColorMap->Colors[gft->SBackGroundColor].Green;
		bg[2] = gft->SColorMap->Colors[gft->SBackGroundColor].Blue;
	}
	fill_rect(canvas, canvas_alpha, x, y, 0, 0, x, y, bg);

	do
	{
		if(DGifGetRecordType(gft, &rt) == GIF_ERROR)
			break;

		if(rt == EXTENSION_RECORD_TYPE)
		{
			if(DGifGetExtension(gft, &extcode, &ext) == GIF_ERROR)
				break;
			if(extcode == GRAPHICS_EXT_FUNC_CODE && ext && ext[0] >= 4)
			{
				transparency = (ext[1] & 1) ? ext[4] : -1;
				disposal = (ext[1] >> 2) & 7;
				delay = ext[2] | (ext[3] << 8);
			}
			else if(extcode == APPLICATION_EXT_FUNC_CODE && ext && ext[0] == 11 &&
				(!memcmp(ext + 1, "NETSCAPE2.0", 11) || !memcmp(ext + 1, "ANIMEXTS1.0", 11)))
			{
				if(DGifGetExtensionNext(gft, &ext) == GIF_ERROR)
					break;
				if(ext && ext[0] >= 3 && ext[1] == 1)
				{
					loops = ext[2] | (ext[3] << 8);
					if(loops)
						loops++;
				}
			}
			while(ext)
				if(DGifGetExtensionNext(gft, &ext) == GIF_ERROR)
					goto truncated;
		}
		else if(rt == IMAGE_DESC_RECORD_TYPE)
		{
			struct gif_frame *f;
			int left, top, w, h, rows = 0;

			if(DGifGetImageDesc(gft) == GIF_ERROR)
				break;
			left = gft->Image.Left;
			top = gft->Image.Top;
			w = gft->Image.Width;
			h = gft->Image.Height;
			cmap = gft->Image.ColorMap ? gft->Image.ColorMap : gft->SColorMap;
			if(!cmap)
				break;

			free(line);
			if(!(line = malloc(w ? w : 1)))
				goto out;

			if(disposal == 3)
			{
				memcpy(prev, canvas, x * y * 3);
				memcpy(prev_alpha, canvas_alpha, x * y);
			}

			for(j = 0; j < (gft->Image.Interlace ? 4 : 1); j++)
			{
				int first = gft->Image.Interlace ? in_beginrow[j] : 0;
				int step = gft->Image.Interlace ? in_nextrow[j] : 1;

				for(i = first; i < h; i += step, rows++)
				{
					int cy = top + i;

					if(DGifGetLine(gft, line, w) == GIF_ERROR)
						goto partial;
					if(cy >= y)
						continue;
					for(k = 0; k < w && left + k < x; k++)
					{
						int c = line[k];
						int p = cy * x + left + k;

						if(c == transparency || c >= cmap->ColorCount)
							continue;
						canvas[p * 3] = cmap->Colors[c].Red;
						canvas[p * 3 + 1] = cmap->Colors[c].Green;
						canvas[p * 3 + 2] = cmap->Colors[c].Blue;
						canvas_alpha[p] = 0xff;
					}
				}
			}
partial:
			if(!(f = realloc(frames, (framecount + 1) * sizeof(*frames))))
				goto out;
			frames = f;
			f += framecount;
			f->rgb = malloc(x * y * 3);
			f->alpha = malloc(x * y);
			if(!f->rgb || !f->alpha)
			{
				free(f->rgb);
				free(f->alpha);
				goto out;
			}
			memcpy(f->rgb, canvas, x * y * 3);
			memcpy(f->alpha, canvas_alpha, x * y);
			f->delay = delay;
			framecount++;
			if(memchr(canvas_alpha, 0, x * y))
				transparent = 1;

			if(rows < h)
				break;

			if(disposal == 2)
				fill_rect(canvas, canvas_alpha, x, y, left, top, w, h, bg);
			else if(disposal == 3)
			{
				memcpy(canvas, prev, x * y * 3);
				memcpy(canvas_alpha, prev_alpha, x * y);
			}
			transparency = -1;
			disposal = 0;
			delay = 0;
		}
	}
	while(rt != TERMINATE_RECORD_TYPE);
truncated:

	if(!framecount)
		goto out;

	if(!transparent)
		for(i = 0; i < framecount; i++)
		{
			free(frames[i].alpha);
			frames[i].alpha = NULL;
		}

	memcpy(buffer, frames[0].rgb, x * y * 3);
	*alpha = NULL;
	if(frames[0].alpha && (*alpha = malloc(x * y)))
		memcpy(*alpha, frames[0].alpha, x * y);
	ret = FH_ERROR_OK;

out:
	if(ret != FH_ERROR_OK)
		fh_gif_unload();
	free(line);
	free(prev_alpha);
	free(prev);
	free(canvas_alpha);
	free(canvas);
	DGifCloseFile(gft, &err);
	return(ret);
}

/* Time to show the current frame in ms, 0 when the animation is over. */
int fh_gif_delay(void)
{
	int d;

	if(framecount < 2)
		return(0);
	if(loops && loopcount >= loops - 1 && frameix == framecount - 1)
		return(0);
	d = frames[frameix].delay;
	/* like browsers, treat 0 and 1 as 10 */
	return((d < 2 ? 10 : d) * 10);
}

int fh_gif_next(unsigned char *buffer, unsigned char *alpha, int x, int y)
{
	if(!framecount)
		return(FH_ERROR_FORMAT);
	if(++frameix == framecount)
	{
		frameix = 0;
		loopcount++;
	}
	memcpy(buffer, frames[frameix].rgb, x * y * 3);
	if(alpha && frames[frameix].alpha)
		memcpy(alpha, frames[frameix].alpha, x * y);
	return(FH_ERROR_OK);
}

int fh_gif_getsize(char *name, int *x, int *y)
{
	GifFileType *gft;
	int err;

	if(!(gft = DGifOpenFileName(name, &err)))
		return(FH_ERROR_FILE);
	*x = gft->SWidth;
	*y = gft->SHeight;
	DGifCloseFile(gft, &err);
	if(*x <= 0 || *y <= 0)
		return(FH_ERROR_FORMAT);
	return(FH_ERROR_OK);
}
#endif
