/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * gsf-output-bzip.c: wrapper to compress to bzipped output
 *
 * Copyright (C) 2003 Dom Lachowicz (cinamod@hotmail.com)
 *               2002 Jon K Hellan (hellan@acm.org)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */

#include <gsf-config.h>

#ifdef HAVE_BZIP

#include <bzlib.h>
#include <gsf/gsf-output-bzip.h>
#include <gsf/gsf-output-impl.h>
#include <gsf/gsf-impl-utils.h>
#include <gsf/gsf-utils.h>

#define BZ_BUFSIZE 0x100

struct _GsfOutputBzip {
	GsfOutput output;

	GsfOutput *sink; /* compressed data */

	bz_stream  stream;
	guint8   *buf;
	size_t    buf_size;
};

typedef struct {
	GsfOutputClass output_class;
} GsfOutputBzipClass;

static gboolean
init_bzip (GsfOutputBzip *bzip, GError **err)
{
	int ret;
	
	ret = BZ2_bzCompressInit (&bzip->stream, 6, 0, 0);

	if (ret != BZ_OK) {
		if (err != NULL)
			*err = g_error_new (gsf_output_error_id (), 0,
					    "Unable to initialize BZ2 library");
		return FALSE;
	}
	if (!bzip->buf) {
		bzip->buf_size = BZ_BUFSIZE; 
		bzip->buf = g_new (guint8, bzip->buf_size);
	}
	bzip->stream.next_out  = bzip->buf;
	bzip->stream.avail_out = bzip->buf_size;

	return TRUE;
}

static void
gsf_output_bzip_finalize (GObject *obj)
{
	GObjectClass *parent_class;
	GsfOutputBzip *bzip = (GsfOutputBzip *)obj;

	if (bzip->sink != NULL) {
		g_object_unref (G_OBJECT (bzip->sink));
		bzip->sink = NULL;
	}

	g_free (bzip->buf);

	parent_class = g_type_class_peek (GSF_OUTPUT_TYPE);
	if (parent_class && parent_class->finalize)
		parent_class->finalize (obj);
}

static gboolean
bzip_output_block (GsfOutputBzip *bzip)
{
	size_t num_bytes = bzip->buf_size - bzip->stream.avail_out;
	
	if (!gsf_output_write (bzip->sink, num_bytes, bzip->buf))
		return FALSE;

	bzip->stream.next_out  = bzip->buf;
	bzip->stream.avail_out = bzip->buf_size;

	return TRUE;
}

static gboolean
bzip_flush (GsfOutputBzip *bzip)
{
	int zret;

	do {
		zret = BZ2_bzCompress (&bzip->stream, BZ_FINISH);
		if (zret == BZ_FINISH_OK) {
			/*  In this case BZ_FINISH_OK means more buffer space
			    needed  */
			if (!bzip_output_block (bzip))
				return FALSE;
		}
	} while (zret == BZ_FINISH_OK);
	if (zret != BZ_STREAM_END)
		return FALSE;
	if (!bzip_output_block (bzip))
		return FALSE;

	return TRUE;
}

static gboolean
gsf_output_bzip_write (GsfOutput *output,
		       size_t num_bytes, guint8 const *data)
{
	GsfOutputBzip *bzip = GSF_OUTPUT_BZIP (output);

	g_return_val_if_fail (data, FALSE);

	bzip->stream.next_in  = (unsigned char *) data;
	bzip->stream.avail_in = num_bytes;
	
	while (bzip->stream.avail_in > 0) {
		if (bzip->stream.avail_out == 0) {
			if (!bzip_output_block (bzip))
				return FALSE;
		}
		if (BZ2_bzCompress (&bzip->stream, BZ_RUN) != BZ_RUN_OK)
			return FALSE;
	}

	return TRUE;
}

static gboolean
gsf_output_bzip_seek (GsfOutput *output, gsf_off_t offset, GSeekType whence)
{
	(void) output;
	(void) offset;
	(void) whence;
	
	return FALSE;
}

static gboolean
gsf_output_bzip_close (GsfOutput *output)
{
	GsfOutputBzip *bzip = GSF_OUTPUT_BZIP (output);
	gboolean rt;

	rt = bzip_flush (bzip);
	BZ2_bzCompressEnd (&bzip->stream);

	return rt;
}

static void
gsf_output_bzip_init (GObject *obj)
{
	GsfOutputBzip *bzip = GSF_OUTPUT_BZIP (obj);

	bzip->sink = NULL;
	bzip->stream.bzalloc	= NULL;
	bzip->stream.bzfree	= NULL;
	bzip->stream.opaque	= NULL;
	bzip->stream.next_in	= NULL;
	bzip->stream.next_out	= NULL;
	bzip->stream.avail_in	= bzip->stream.avail_out = 0;
	bzip->buf		= NULL;
	bzip->buf_size		= 0;
}

static void
gsf_output_bzip_class_init (GObjectClass *gobject_class)
{
	GsfOutputClass *output_class = GSF_OUTPUT_CLASS (gobject_class);

	gobject_class->finalize = gsf_output_bzip_finalize;
	output_class->Write	= gsf_output_bzip_write;
	output_class->Seek	= gsf_output_bzip_seek;
	output_class->Close	= gsf_output_bzip_close;
}

GSF_CLASS (GsfOutputBzip, gsf_output_bzip,
	   gsf_output_bzip_class_init, gsf_output_bzip_init, GSF_OUTPUT_TYPE)

#endif /* HAVE_BZIP */

/**
 * gsf_output_bzip_new :
 * @sink : The underlying data source.
 * @err	   : optionally NULL.
 *
 * Adds a reference to @sink.
 *
 * Returns a new file or NULL.
 **/
GsfOutputBzip *
gsf_output_bzip_new (GsfOutput *sink, GError **err)
{
#ifdef HAVE_BZIP
	GsfOutputBzip *bzip;

	g_return_val_if_fail (GSF_IS_OUTPUT (sink), NULL);

	bzip = g_object_new (GSF_OUTPUT_BZIP_TYPE, NULL);
	g_object_ref (G_OBJECT (sink));
	bzip->sink = sink;

	if (!init_bzip (bzip, err)) {
		g_object_unref (G_OBJECT (bzip));
		return NULL;
	}

	return bzip;
#else
#warning Building without BZ2 support
	if (err)
		*err = g_error_new (gsf_input_error (), 0,
				    "BZ2 support not enabled");
	return NULL;
#endif
}
