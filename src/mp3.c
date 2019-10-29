/*
** Copyright (C) 2002-2011 Erik de Castro Lopo <erikd@mega-nerd.com>
** Copyright (C) 2019 John ffitch
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU Lesser General Public License as published by
** the Free Software Foundation; either version 2.1 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
** GNU Lesser General Public License for more details.
**
** You should have received a copy of the GNU Lesser General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
*/

#include	"sfconfig.h"

#include	<stdio.h>
#include	<fcntl.h>
#include	<string.h>
#include	<ctype.h>
#include	<math.h>

#include	"sndfile.h"
#include	"sfendian.h"
#include	"common.h"
#include	"mp3.h"

/*------------------------------------------------------------------------------
** Typedefs.
*/

/*------------------------------------------------------------------------------
** Private static functions.
*/

static int mp3_open_lame (SF_PRIVATE *psf) ;
static int mp3_close_lame (SF_PRIVATE *psf) ;
static int mp3_open_read (SF_PRIVATE *psf) ;
static int mp3_close_read (SF_PRIVATE *psf) ;

static sf_count_t mp3_lame_write_short (SF_PRIVATE *psf, const short *ptr, sf_count_t items) ;
static sf_count_t mp3_lame_write_int (SF_PRIVATE *psf, const int *ptr, sf_count_t items) ;
static sf_count_t mp3_lame_write_float (SF_PRIVATE *psf, const float *ptr, sf_count_t items) ;
static sf_count_t mp3_lame_write_double (SF_PRIVATE *psf, const double *ptr, sf_count_t items) ;
static sf_count_t mp3_lame_write_short_M (SF_PRIVATE *psf, const short *ptr, sf_count_t items) ;
static sf_count_t mp3_lame_write_int_M (SF_PRIVATE *psf, const int *ptr, sf_count_t items) ;
static sf_count_t mp3_lame_write_float_M (SF_PRIVATE *psf, const float *ptr, sf_count_t items) ;
static sf_count_t mp3_lame_write_double_M (SF_PRIVATE *psf, const double *ptr, sf_count_t items) ;
static int mp3_command (SF_PRIVATE *psf, int command, void *data, int datasize) ;

#ifdef HIP
static sf_count_t mp3_read_short (SF_PRIVATE *psf, short *ptr, sf_count_t items) ;
static sf_count_t mp3_read_int (SF_PRIVATE *psf, int *ptr, sf_count_t items) ;
static sf_count_t mp3_read_float (SF_PRIVATE *psf, float *ptr, sf_count_t items) ;
static sf_count_t mp3_read_double (SF_PRIVATE *psf, double *ptr, sf_count_t items) ;
#else
static	int		mp3_read_header		(SF_PRIVATE * psf, mpg123_handle * decoder) ;
static	sf_count_t	mp3_read_2s		(SF_PRIVATE * psf, short * ptr, sf_count_t len) ;
static	sf_count_t	mp3_read_2i		(SF_PRIVATE * psf, int * ptr, sf_count_t len) ;
static	sf_count_t	mp3_read_2f		(SF_PRIVATE * psf, float * ptr, sf_count_t len) ;
static	sf_count_t	mp3_read_2d		(SF_PRIVATE * psf, double * ptr, sf_count_t len) ;
static	sf_count_t	mp3_read_seek		(SF_PRIVATE *psf, int whence, sf_count_t offset) ;
static	ssize_t		mp3_read_sf_handle	(void * handle, void * buffer, size_t bytes) ;
static	off_t		mp3_seek_sf_handle	(void * handle, off_t offset, int whence) ;
static	int		mp3_format_to_encoding	(int encoding) ;
#endif

/*------------------------------------------------------------------------------
** Public function.
*/

int
mp3_open (SF_PRIVATE *psf)
{	int	subformat, error = 0 ;

	if (psf->file.mode == SFM_RDWR)
		return SFE_BAD_MODE_RW ;


	if (psf->file.mode == SFM_WRITE)
		return mp3_open_lame (psf) ;

	if (psf->file.mode == SFM_RDWR)
		return SFE_UNIMPLEMENTED ;


	if (psf->file.mode == SFM_READ)
		return mp3_open_read (psf) ;

	return error ;
} /* mp3_open */

/*------------------------------------------------------------------------------
*/

static int
mp3_open_lame (SF_PRIVATE *psf)
{	MP3_PRIVATE_W *p ;
	if ((p = calloc (1, sizeof (MP3_PRIVATE_W))) == NULL)
		return SFE_MALLOC_FAILED ;
	psf->container_data = p ;
	lame_global_flags *gfp = p->gfp = lame_init () ;
	int error = 0, format, mode ;

	format = SF_CONTAINER (psf->sf.format) ;
	if (format != SF_FORMAT_MP3)
		return SFE_BAD_OPEN_FORMAT ;
	lame_set_num_channels (gfp, psf->sf.channels) ;
	lame_set_in_samplerate (gfp, psf->sf.samplerate) ;
	lame_set_brate (gfp, 256) ;	/* FIXME to parameter */
	lame_set_mode (gfp, mode) ;
	p->quality = 2 ;
	if ((p->mp3buffer = calloc (1, MP3BUFFER_SIZE)) == NULL)
		return SFE_MALLOC_FAILED ;
	psf->command = mp3_command ;
	psf->datalength = 0 ;	/* What is this?? */
	psf->dataoffset = 0 ;
	if (psf->sf.channels == 1)		/* MONO may overwrite data */
	{	psf->write_short = mp3_lame_write_short_M ;
		psf->write_int = mp3_lame_write_int_M ;
		psf->write_float = mp3_lame_write_float_M ;
		psf->write_double = mp3_lame_write_double_M ;
		}
	else
	{	psf->write_short = mp3_lame_write_short ;
		psf->write_int = mp3_lame_write_int ;
		psf->write_float = mp3_lame_write_float ;
		psf->write_double = mp3_lame_write_double ;
		}
	psf->container_close = mp3_close_lame ;
	psf->command = mp3_command ;
	return error ;
}

static int
mp3_close_lame (SF_PRIVATE *psf)
{	MP3_PRIVATE_W *p = psf->container_data ;
	lame_global_flags *gfp = p->gfp ;
	int bytes = lame_encode_flush (gfp, p->mp3buffer, MP3BUFFER_SIZE) ;
	if (bytes > 0) psf_fwrite (p->mp3buffer, 1, bytes, psf) ;
	//lame_mp3_tags_fid (gfp, p->fout) ;
	lame_close (gfp) ;

	free (p->mp3buffer) ;
	p->gfp = NULL ;
	return 0 ;
}

/*------------------------------------------------------------------------------
*/

static int
mp3_lame_parameters (lame_global_flags *gfp, MP3_PRIVATE_W *p)
{	int error ;
	lame_set_quality (gfp, p->quality) ;
	if ((error = lame_init_params (gfp)) < 0)
		return SFE_BAD_OPEN_FORMAT ;
	p->initialised = 1 ;
	return 1 ;
}

/* **** Note that if mode is MONO (3) then left and right channels are averaged and the data is changed.  Hence the _M functions **** */
static sf_count_t
mp3_lame_write_short (SF_PRIVATE *psf, const short int *ptr, sf_count_t items)
{	MP3_PRIVATE_W *p = psf->container_data ;
	lame_global_flags *gfp = p->gfp ;
	sf_count_t total = items ;
	int bytes, count ;
	items /= 2 ;
	if (p->initialised == 0)
		mp3_lame_parameters (gfp, p) ;
	do
	{	count = (items > MP3DATA_SIZE ? MP3DATA_SIZE : items) ;
		bytes = lame_encode_buffer_interleaved (gfp, (short *) ptr,
							count, p->mp3buffer,
							MP3BUFFER_SIZE) ;
		if (bytes > 0) psf_fwrite (p->mp3buffer, 1, bytes, psf) ;
		if (bytes < 0) return 0 ;
		ptr += count * 2 ;
		items -= count ;
		} while (count > 0) ;
	return items ;
}

static sf_count_t
mp3_lame_write_int (SF_PRIVATE *psf, const int *ptr, sf_count_t items)
{	MP3_PRIVATE_W *p = psf->container_data ;
	lame_global_flags *gfp = p->gfp ;
	sf_count_t total = items ;
	int bytes, count ;
	items /= 2 ;
	if (p->initialised == 0)
		mp3_lame_parameters (gfp, p) ;
	do
	{	count = (items > MP3DATA_SIZE ? MP3DATA_SIZE : items) ;
		bytes = lame_encode_buffer_interleaved_int (gfp, ptr,
							count, p->mp3buffer,
							MP3BUFFER_SIZE) ;
		if (bytes > 0) psf_fwrite (p->mp3buffer, 1, bytes, psf) ;
		if (bytes < 0) return 0 ;
		ptr += count * 2 ;
		items -= count ;
		} while (count > 0) ;
	return total ;
}

static sf_count_t
mp3_lame_write_float (SF_PRIVATE *psf, const float *ptr, sf_count_t items)
{	MP3_PRIVATE_W *p = psf->container_data ;
	lame_global_flags *gfp = p->gfp ;
	sf_count_t total = items ;
	int bytes, count ;
	items /= 2 ;
	if (p->initialised == 0)
		if (mp3_lame_parameters (gfp, p) == 0) return 0 ;
	do
	{	count = (items > MP3DATA_SIZE ? MP3DATA_SIZE : items) ;
		bytes = lame_encode_buffer_interleaved_ieee_float (gfp, ptr,
							count, p->mp3buffer,
							MP3BUFFER_SIZE) ;
		if (bytes > 0) psf_fwrite (p->mp3buffer, 1, bytes, psf) ;
		if (bytes < 0) return 0 ;
		items -= count ;
		ptr += count * 2 ;
		} while (count > 0) ;
	return 1 ;
}

static sf_count_t
mp3_lame_write_double (SF_PRIVATE *psf, const double *ptr, sf_count_t items)
{	MP3_PRIVATE_W *p = psf->container_data ;
	lame_global_flags *gfp = p->gfp ;
	sf_count_t total = items ;
	int bytes, count ;
	items /= 2 ;
	if (p->initialised == 0)
		mp3_lame_parameters (gfp, p) ;
	do
	{	count = (items > MP3DATA_SIZE ? MP3DATA_SIZE : items) ;
		bytes = lame_encode_buffer_interleaved_ieee_double (gfp, ptr,
								count, p->mp3buffer,
								MP3BUFFER_SIZE) ;
		if (bytes > 0) psf_fwrite (p->mp3buffer, 1, bytes, psf) ;
		if (bytes < 0) return 0 ;
		items -= count ;
		ptr += count * 2 ;
		} while (count > 0) ;
	return total ;
}
/* **************** MONO cases to avoid overwriting ************ */

static sf_count_t
mp3_lame_write_short_M (SF_PRIVATE *psf, const short *ptr, sf_count_t items)
{	MP3_PRIVATE_W *p = psf->container_data ;
	lame_global_flags *gfp = p->gfp ;
	sf_count_t total = items ;
	int bytes, count ;
	/* short *left = (short *) p->mp3data ; */
	/* short *right = left + MP3DATA_SIZE ; */
	if (p->initialised == 0)
		mp3_lame_parameters (gfp, p) ;
	do
	{	count = (items > MP3DATA_SIZE ? MP3DATA_SIZE : items) ;
		/* memcpy(left, ptr, count * sizeof (short)) ; */
		/* memcpy(right, ptr, count * sizeof (short)) ; */
		bytes = lame_encode_buffer (gfp, ptr, NULL,
						count, p->mp3buffer,
						MP3BUFFER_SIZE) ;
		if (bytes > 0) psf_fwrite (p->mp3buffer, 1, bytes, psf) ;
		if (bytes < 0) return 0 ;
		ptr += count ;
		items -= count ;
		} while (count > 0) ;
	return 1 ;
}

static	sf_count_t
mp3_lame_write_int_M (SF_PRIVATE *psf, const int *ptr, sf_count_t items)
{	MP3_PRIVATE_W *p = psf->container_data ;
	lame_global_flags *gfp = p->gfp ;

	int bytes, count ;
	/* int *left = (int *) p->mp3data ; */
	/* int *right = left+MP3DATA_SIZE ; */
	if (p->initialised == 0)
		mp3_lame_parameters (gfp, p) ;
	do
	{	count = (items > MP3DATA_SIZE ? MP3DATA_SIZE : items) ;
		/* memcpy(left, ptr, count * sizeof (int)) ; */
		/* memcpy(right, ptr, count * sizeof (int)) ; */
		bytes = lame_encode_buffer_int (gfp, ptr, NULL,
						count, p->mp3buffer,
						MP3BUFFER_SIZE) ;
		if (bytes > 0) psf_fwrite (p->mp3buffer, 1, bytes, psf) ;
		if (bytes < 0) return 0 ;
		ptr += count ;
		items -= count ;
		} while (count > 0) ;
	return items ;
}
static sf_count_t
mp3_lame_write_float_M (SF_PRIVATE *psf, const float *ptr, sf_count_t items)
{	MP3_PRIVATE_W *p = psf->container_data ;
	lame_global_flags *gfp = p->gfp ;
	sf_count_t total = items ;
	int bytes, count ;
	/* float *left = (float *) p->mp3data ; */
	/* float *right = left+MP3DATA_SIZE ; */
	if (p->initialised == 0)
		mp3_lame_parameters (gfp, p) ;
	do
	{	count = (items > MP3DATA_SIZE ? MP3DATA_SIZE : items) ;
		/* memcpy(left, ptr, count * sizeof (float)) ; */
		/* memcpy(right, ptr, count * sizeof (float)) ; */
		bytes = lame_encode_buffer_ieee_float (gfp, ptr, NULL,
						count, p->mp3buffer,
						MP3BUFFER_SIZE) ;
		if (bytes > 0) psf_fwrite (p->mp3buffer, 1, bytes, psf) ;
		if (bytes < 0) return 0 ;
		ptr += count ;
		items -= count * psf->sf.channels ;
		} while (count > 0) ;
	return total ;
}

static sf_count_t
mp3_lame_write_double_M (SF_PRIVATE *psf, const double *ptr, sf_count_t items)
{	MP3_PRIVATE_W *p = psf->container_data ;
	lame_global_flags *gfp = p->gfp ;
	sf_count_t total = items ;
	int bytes, count ;
	/* double *left = (double *) p->mp3data ; */
	/* double *right = left+MP3DATA_SIZE ; */
	if (p->initialised == 0)
		mp3_lame_parameters (gfp, p) ;
	do
	{	count = (items > MP3DATA_SIZE ? MP3DATA_SIZE : items) ;
		/* memcpy(left, ptr, count * sizeof (double)) ; */
		/* memcpy(right, ptr, count * sizeof (double)) ; */
		bytes = lame_encode_buffer_ieee_double (gfp, ptr, NULL,
						count, p->mp3buffer,
						MP3BUFFER_SIZE) ;
		if (bytes > 0) psf_fwrite (p->mp3buffer, 1, bytes, psf) ;
		if (bytes < 0) return 0 ;
		items -= count ;
		ptr += count ;
		} while (count > 0) ;
	return total ;
}

/* ********************************************************* */

static int
mp3_command (SF_PRIVATE * psf, int command, void * data, int datasize)
{	MP3_PRIVATE_W* p = (MP3_PRIVATE_W*) psf->container_data ;
	double quality ;
	int i, bitrate ;

	switch (command)
	{	case SFC_SET_QUALITY_LEVEL :
			if (data == NULL || datasize != sizeof (double))
				return SF_FALSE ;

			if (psf->have_written)
				return SF_FALSE ;

			/* MP3 quality is in the range	[0, 9] while libsndfile takes
			** values in the range [0.0, 1.0]. Massage the libsndfile value here.
			*/
			quality = (*((double *) data)) * 9.0 ;
			/* Clip range. */
			p->quality = lrint (SF_MAX (0.0, SF_MIN (9.0, quality))) ;
			lame_set_quality (p->gfp, p->quality) ;

			psf_log_printf (psf, "%s : Setting SFC_SET_QUALITY_LEVEL to %u.\n", __func__, p->quality) ;

			return SF_TRUE ;

		case SFC_SET_BITRATE:
			if (data == NULL || datasize != sizeof (int))
				return SF_FALSE ;

			if (psf->have_written)
				return SF_FALSE ;
			bitrate = (*((int *) data)) ;
			i = 0 ;
			while (mp3_bitrates [i] != 0)
			{	if (mp3_bitrates [i] >= bitrate)
				{	p->bitrate = mp3_bitrates [i] ;
					if (p->bitrate == 0) p->bitrate = 256 ;
					lame_set_brate (p->gfp, p->bitrate) ;
					return SF_TRUE ;
				}
			i++ ;
			}
			return SF_FALSE ;

		case SFC_MP3_STEREO:
			if (psf->have_written)
				return SF_FALSE ;
			lame_set_mode (p->gfp, 0) ;
			return SF_TRUE ;

		default :
			return SF_FALSE ;
		} ;

	return SF_FALSE ;
} /* mp3_command */


/* ====================================================================== */

#ifdef HIP
static int mp3_close_read (SF_PRIVATE *psf)
{	return SFE_UNIMPLEMENTED ;
}

#define CHUNK_SIZE	(512)
/* #define FRAME_SIZE	(1152) */
#define NORMALISE	(0.000030517578125)
#define NORMALISEI	(65536)

static int mp3_open_read (SF_PRIVATE *psf)
{	MP3_PRIVATE_R *p = (MP3_PRIVATE_R*) psf->container_data ;
	int err ;
	if ((p->mp3buffer = calloc (4, MP3DATA_SIZE)) == NULL)
		return SFE_MALLOC_FAILED ;
	p->hgf = hip_decode_init () ;
	if ((p->left = calloc (1, 8192)) == NULL)
		return SFE_MALLOC_FAILED ;
	if ((p->right = calloc (1, 8193)) == NULL)
		return SFE_MALLOC_FAILED ;
	do
	{	err = psf_fread (p->mp3buffer, 1, CHUNK_SIZE, psf) ;
		//printf ("*** fread= %d\n", err) ;
		err = hip_decode1_headers (p->hgf, p->mp3buffer, CHUNK_SIZE,
					p->left, p->right, &p->mp3data) ;
		//printf ("*** decode-headers= %d\n", err) ;
		} while (err == 0) ;
	p->left = realloc (p->left, p->mp3data.framesize * sizeof (short)) ;
	p->right = realloc (p->right, p->mp3data.framesize * sizeof (short)) ;
	p->count = err ;
	p->start = 0 ;
	psf->sf.format = SF_FORMAT_MP3 | 1 ;
	psf->sf.channels = p->mp3data.stereo ;
	psf->sf.samplerate = p->mp3data.samplerate ;
	psf->datalength = psf->sf.frames = SF_COUNT_MAX ; /* Unknown really */
	psf->dataoffset = 0 ;

	psf->read_short = mp3_read_short ;
	psf->read_int = mp3_read_int ;
	psf->read_float = mp3_read_float ;
	psf->read_double = mp3_read_double ;
#if 0
	psf->container_close = mp3_close_read ;
	psf->seek = mp3_read_seek ;

#endif
	return 0 ;
}

static sf_count_t mp3_read_short (SF_PRIVATE *psf, short *ptr, sf_count_t items)
{	MP3_PRIVATE_R *p = (MP3_PRIVATE_R*) psf->container_data ;
	sf_count_t count = p->count ;
	sf_count_t start = p->start ;
	int i ;
	sf_count_t total = 0 ;
	int stereo = (p->mp3data.stereo == 2) ;
	items /= psf->sf.channels ;
 more:
	if (items <= count)
	{	for (i = 0 ; i < items ; i++)
		{	*ptr++ = p->left [start] ;
			if (stereo) *ptr++ = p->right [start] ;
			start++ ;
			}
		total += items ;
		p->count = count - items ;
		p->start - start ;
		return total * psf->sf.channels ;
		}
	/* Need more data */
	for (i = 0 ; i < count ; i++)
	{	*ptr++ = p->left [start] ;
		if (stereo) *ptr++ = p->right [start] ;
		start++ ;
		}
	total += count ;
	items -= count ;
	start = 0 ;
	do
	{	count = psf_fread (p->mp3buffer, 1, CHUNK_SIZE, psf) ;
		if (count <= 0)
		{	//printf ("EOF? %d\n", count) ;
			return total * psf->sf.channels ;
			}
		count = hip_decode1 (p->hgf, p->mp3buffer, CHUNK_SIZE,
				p->left, p->right) ;
		} while (count == 0) ;
	goto more ;
}

static sf_count_t mp3_read_int (SF_PRIVATE *psf, int *ptr, sf_count_t items)
{	MP3_PRIVATE_R *p = (MP3_PRIVATE_R*) psf->container_data ;
	sf_count_t count = p->count ;
	sf_count_t start = p->start ;
	int i ;
	sf_count_t total = 0 ;
	int stereo = (p->mp3data.stereo == 2) ;
	items /= psf->sf.channels ;
 more:
	if (items <= count)
	{	for (i = 0 ; i < items ; i++)
		{	*ptr++ = (int) p->left [start] * NORMALISEI ;
			if (stereo) *ptr++ = (int) p->right [start] * NORMALISEI ;
			start++ ;
			}
		total += items ;
		p->count = count - items ;
		p->start - start ;
		return total * psf->sf.channels ;
		}
	/* Need more data */
	for (i = 0 ; i < count ; i++)
	{	*ptr++ = (int) p->left [start] * NORMALISEI ;
		if (stereo) *ptr++ = (int) p->right [start] * NORMALISEI ;
		start++ ;
		}
	total += count ;
	items -= count ;
	start = 0 ;
	do
	{	count = psf_fread (p->mp3buffer, 1, CHUNK_SIZE, psf) ;
		if (count <= 0)
		{	//printf ("EOF? %d\n", count) ;
			return total * psf->sf.channels ;
			}
		count = hip_decode1 (p->hgf, p->mp3buffer, CHUNK_SIZE,
				p->left, p->right) ;
		} while (count == 0) ;
	goto more ;
}

static sf_count_t mp3_read_float (SF_PRIVATE *psf, float *ptr, sf_count_t items)
{	MP3_PRIVATE_R *p = (MP3_PRIVATE_R*) psf->container_data ;
	sf_count_t count = p->count ;
	sf_count_t start = p->start ;
	int i ;
	sf_count_t total = 0 ;
	int stereo = (p->mp3data.stereo == 2) ;
	items /= psf->sf.channels ;
 more:
	if (items <= count)
	{	for (i = 0 ; i < items ; i++)
		{	*ptr++ = (float) p->left [start] * NORMALISE ;
			if (stereo) *ptr++ = (float) p->right [start] * NORMALISE ;
			start++ ;
			}
		total += items ;
		p->count = count - items ;
		p->start - start ;
		return total * psf->sf.channels ;
		}
	/* Need more data */
	for (i = 0 ; i < count ; i++)
	{	*ptr++ = (float) p->left [start] * NORMALISE ;
		if (stereo) *ptr++ = (float) p->right [start] * NORMALISE ;
		start++ ;
		}
	total += count ;
	items -= count ;
	start = 0 ;
	do
	{	count = psf_fread (p->mp3buffer, 1, CHUNK_SIZE, psf) ;
		if (count <= 0)
		{	//printf ("EOF? %d\n", count) ;
			return total * psf->sf.channels ;
			}
		count = hip_decode1 (p->hgf, p->mp3buffer, CHUNK_SIZE,
				p->left, p->right) ;
		} while (count == 0) ;
	goto more ;
}

static sf_count_t mp3_read_double (SF_PRIVATE *psf, double *ptr, sf_count_t items)
{	MP3_PRIVATE_R *p = (MP3_PRIVATE_R*) psf->container_data ;
	sf_count_t count = p->count ;
	sf_count_t start = p->start ;
	int i ;
	sf_count_t total = 0 ;
	int stereo = (p->mp3data.stereo == 2) ;
	items /= psf->sf.channels ;
 more:
	if (items <= count)
	{	for (i = 0 ; i < items ; i++)
		{	*ptr++ = (double) p->left [start] * NORMALISE ;
			if (stereo) *ptr++ = (double) p->right [start] * NORMALISE ;
			start++ ;
			}
		total += items ;
		p->count = count - items ;
		p->start - start ;
		return total * psf->sf.channels ;
		}
	/* Need more data */
	for (i = 0 ; i < count ; i++)
	{	*ptr++ = (double) p->left [start] * NORMALISE ;
		if (stereo) *ptr++ = (double) p->right [start] * NORMALISE ;
		start++ ;
		}
	total += count ;
	items -= count ;
	start = 0 ;
	do
	{	count = psf_fread (p->mp3buffer, 1, CHUNK_SIZE, psf) ;
		if (count <= 0)
		{	//printf ("EOF? %d\n", count) ;
			return total * psf->sf.channels ;
			}
		count = hip_decode1 (p->hgf, p->mp3buffer, CHUNK_SIZE,
				p->left, p->right) ;
		} while (count == 0) ;
	goto more ;
}
#else

/* ******************************************************** */

// FIXME: This initialisation should have a better hook
static int mpg123_initialised = 0 ;

// FIXME: use mpg123 error string reporting
static int
mp3_open_read (SF_PRIVATE * psf)
{
	int decoder_err = MPG123_OK ;
	mpg123_handle * decoder ;
	if (mpg123_initialised == 0)
	{	int decoder_init_err = mpg123_init () ;
		if (decoder_init_err != MPG123_OK)
		{	psf_log_printf (psf, "Failed to init mpg123.\n") ;
			return SFE_UNIMPLEMENTED ; // FIXME: semantically wrong return code
			}
		}
	mpg123_initialised++ ;

	decoder = mpg123_new (NULL, &decoder_err) ;

	if (decoder_err == MPG123_OK)
		decoder_err = mpg123_replace_reader_handle (
			decoder, mp3_read_sf_handle, mp3_seek_sf_handle, NULL) ;
	psf->fileoffset = 0 ; // FIXME: Remove this once the seek fixes are in
	if (decoder_err == MPG123_OK)
		decoder_err = mpg123_open_handle (decoder, psf) ;

	if (decoder_err == MPG123_OK)
		decoder_err = mp3_read_header (psf, decoder) ;

	if (decoder_err != MPG123_OK)
	{	psf_log_printf (psf, "Failed to initialise mp3 decoder.\n") ;
		return SFE_UNIMPLEMENTED ; // FIXME: semantically wrong return code
		}

	psf->dataoffset = psf_ftell (psf) ;
	psf->datalength = psf->filelength - psf->dataoffset ;
	psf->codec_data = decoder ;

	psf->container_close = mp3_close_read ;
	psf->seek = mp3_read_seek ;

	psf->read_short = mp3_read_2s ;
	psf->read_int = mp3_read_2i ;
	psf->read_float = mp3_read_2f ;
	psf->read_double = mp3_read_2d ;

	return 0 ;
}

static int
mp3_close_read (SF_PRIVATE * psf)
{	mpg123_handle * decoder = psf->codec_data ;
	if (decoder != NULL)
	{	// mpg123_close(decoder); <- Not sure if we need this?
		mpg123_delete (decoder) ;
		// The psf sometimes gets reused:
		psf->codec_data = NULL ;
		}
	if (! -- mpg123_initialised)
		mpg123_exit () ;
	return 0 ;
}

static sf_count_t
mp3_read_seek (SF_PRIVATE *psf, int whence, sf_count_t offset)
{	mpg123_handle * decoder = psf->codec_data ;
	return mpg123_seek (decoder, whence, offset) ;
}

static int
mp3_format_to_encoding (int encoding)
{	// FIXME: This function isn't done.
	// https://www.mpg123.de/api/fmt123_8h_source.shtml
	// http://www.mega-nerd.com/libsndfile/api.html
	encoding++ ;// FIXME: this is just to suppress warning
	return SF_FORMAT_MP3 | SF_FORMAT_PCM_16 ;
}

static int
mp3_read_header (SF_PRIVATE * psf, mpg123_handle * decoder)
{	int decoder_err, channels, encoding ;
	long sample_rate ;
	decoder_err = mpg123_getformat (decoder, &sample_rate, &channels, &encoding) ;
	if (decoder_err == MPG123_OK)
	{	psf->sf.format = mp3_format_to_encoding (encoding) ;
		psf->sf.channels = channels ;
		psf->sf.samplerate = sample_rate ;
		}
	psf->sf.frames = mpg123_length (decoder) ;
	return decoder_err ;
}

static ssize_t mp3_read_sf_handle (void * handle, void * buffer, size_t bytes)
{	SF_PRIVATE * psf = handle ;
	return psf_fread (buffer, 1, bytes, psf) ;
}

static off_t mp3_seek_sf_handle (void * handle, off_t offset, int whence)
{	SF_PRIVATE * psf = handle ;
	return psf_fseek (psf, offset, whence) ;
}

static sf_count_t
mp3_read_as (SF_PRIVATE *psf, unsigned char * buffer, int encoding, size_t elem_size, sf_count_t len)
{
	size_t n_decoded = 0 ;
	mpg123_handle * decoder = psf->codec_data ;
	int decoder_err = mpg123_format (decoder, psf->sf.samplerate, psf->sf.channels, encoding) ;
	if (decoder_err == MPG123_OK)
	{	decoder_err = mpg123_read (
			decoder,
			(unsigned char *) buffer, len * elem_size,
			&n_decoded) ;
		if (decoder_err != MPG123_OK)
			psf_log_printf (psf, "Errors occured during mpg123_read.") ;
		}
	else
		psf_log_printf (psf, "Failed to set mpg123_format.\n") ;
	return psf->sf.channels * n_decoded / elem_size ;
}

static sf_count_t
mp3_read_2s (SF_PRIVATE *psf, short *ptr, sf_count_t len)
{	return mp3_read_as (psf, (unsigned char *) ptr, MPG123_ENC_SIGNED_16, sizeof (short), len) ;
}

static sf_count_t
mp3_read_2i (SF_PRIVATE * psf, int * ptr, sf_count_t len)
{	return mp3_read_as (psf, (unsigned char *) ptr, MPG123_ENC_SIGNED_32, sizeof (int), len) ;
}

static sf_count_t
mp3_read_2f (SF_PRIVATE * psf, float * ptr, sf_count_t len)
{	return mp3_read_as (psf, (unsigned char *) ptr, MPG123_ENC_FLOAT_32, sizeof (float), len) ;
}

static sf_count_t
mp3_read_2d (SF_PRIVATE * psf, double * ptr, sf_count_t len)
{	return mp3_read_as (psf, (unsigned char *) ptr, MPG123_ENC_FLOAT_64, sizeof (double), len) ;
}

#endif /* HIP */