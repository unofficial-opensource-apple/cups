/*
 * "$Id: raster.c,v 1.7.2.1 2005/07/27 21:58:44 jlovell Exp $"
 *
 *   Raster file routines for the Common UNIX Printing System (CUPS).
 *
 *   Copyright 1997-2005 by Easy Software Products.
 *
 *   This file is part of the CUPS Imaging library.
 *
 *   These coded instructions, statements, and computer programs are the
 *   property of Easy Software Products and are protected by Federal
 *   copyright law.  Distribution and use rights are outlined in the file
 *   "LICENSE.txt" which should have been included with this file.  If this
 *   file is missing or damaged please contact Easy Software Products
 *   at:
 *
 *       Attn: CUPS Licensing Information
 *       Easy Software Products
 *       44141 Airport View Drive, Suite 204
 *       Hollywood, Maryland 20636 USA
 *
 *       Voice: (301) 373-9600
 *       EMail: cups-info@cups.org
 *         WWW: http://www.cups.org
 *
 *   This code and any derivative of it may be used and distributed
 *   freely under the terms of the GNU General Public License when
 *   used with GNU Ghostscript or its derivatives.  Use of the code
 *   (or any derivative of it) with software other than GNU
 *   GhostScript (or its derivatives) is governed by the CUPS license
 *   agreement.
 *
 *   This file is subject to the Apple OS-Developed Software exception.
 *
 * Contents:
 *
 *   cupsRasterClose()       - Close a raster stream.
 *   cupsRasterOpen()        - Open a raster stream.
 *   cupsRasterReadHeader()  - Read a raster page header.
 *   cupsRasterReadPixels()  - Read raster pixels.
 *   cupsRasterWriteHeader() - Write a raster page header.
 *   cupsRasterWritePixels() - Write raster pixels.
 */

/*
 * Include necessary headers...
 */

#include "raster.h"
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#if defined(WIN32) || defined(__EMX__)
#  include <io.h>
#else
#  include <unistd.h>
#endif /* WIN32 || __EMX__ */


/*
 * 'cupsRasterClose()' - Close a raster stream.
 */

void
cupsRasterClose(cups_raster_t *r)	/* I - Stream to close */
{
  if (r != NULL)
    free(r);
}


/*
 * 'cupsRasterOpen()' - Open a raster stream.
 */

cups_raster_t *				/* O - New stream */
cupsRasterOpen(int         fd,		/* I - File descriptor */
               cups_mode_t mode)	/* I - Mode */
{
  cups_raster_t	*r;			/* New stream */


  if ((r = calloc(sizeof(cups_raster_t), 1)) == NULL)
    return (NULL);

  r->fd   = fd;
  r->mode = mode;

  if (mode == CUPS_RASTER_READ)
  {
   /*
    * Open for read - get sync word...
    */

    if (read(fd, &(r->sync), sizeof(r->sync)) < sizeof(r->sync))
    {
      free(r);
      return (NULL);
    }

    if (r->sync != CUPS_RASTER_SYNC &&
        r->sync != CUPS_RASTER_REVSYNC)
    {
      free(r);
      return (NULL);
    }
  }
  else
  {
   /*
    * Open for write - put sync word...
    */

    r->sync = CUPS_RASTER_SYNC;
    if (write(fd, &(r->sync), sizeof(r->sync)) < sizeof(r->sync))
    {
      free(r);
      return (NULL);
    }
  }

  return (r);
}


/*
 * 'cupsRasterReadHeader()' - Read a raster page header.
 */

unsigned					/* O - 1 on success, 0 on fail */
cupsRasterReadHeader(cups_raster_t      *r,	/* I - Raster stream */
                     cups_page_header_t *h)	/* I - Pointer to header data */
{
  int		len;				/* Number of words to swap */
  unsigned      *s;

  if (r == NULL || r->mode != CUPS_RASTER_READ)
    return (0);

  if (cupsRasterReadPixels(r, (unsigned char *)h, sizeof(cups_page_header_t)) <
          sizeof(cups_page_header_t))
    return (0);

  if (r->sync == CUPS_RASTER_REVSYNC)
    for (len = (sizeof(cups_page_header_t) - 256) / 4,
             s = (unsigned int *)&h->AdvanceDistance;
	 len > 0;
	 len --, s ++)
      *s = (((*s << 24) & 0xFF000000) | 
	    ((*s << 8)  & 0x00FF0000) | 
	    ((*s >> 8)  & 0x0000FF00) | 
	    ((*s >> 24) & 0x000000FF));

  return (1);
}


/*
 * 'cupsRasterReadPixels()' - Read raster pixels.
 */

unsigned				/* O - Number of bytes read */
cupsRasterReadPixels(cups_raster_t *r,	/* I - Raster stream */
                     unsigned char *p,	/* I - Pointer to pixel buffer */
		     unsigned      len)	/* I - Number of bytes to read */
{
  int		bytes;			/* Bytes read */
  unsigned	remaining;		/* Bytes remaining */


  if (r == NULL || r->mode != CUPS_RASTER_READ)
    return (0);

  remaining = len;

  while (remaining > 0)
  {
    bytes = read(r->fd, p, remaining);

    if (bytes == 0)
      return (0);
    else if (bytes < 0)
    {
      if (errno != EINTR)
        return (0);
      else
        continue;
    }

    remaining -= bytes;
    p += bytes;
  }

  return (len);
}


/*
 * 'cupsRasterWriteHeader()' - Write a raster page header.
 */
 
unsigned
cupsRasterWriteHeader(cups_raster_t *r,
                      cups_page_header_t *h)
{
  if (r == NULL || r->mode != CUPS_RASTER_WRITE)
    return (0);

  return (cupsRasterWritePixels(r, (unsigned char *)h,
                                sizeof(cups_page_header_t)) ==
              sizeof(cups_page_header_t));
}


/*
 * 'cupsRasterWritePixels()' - Write raster pixels.
 */

unsigned				/* O - Number of bytes written */
cupsRasterWritePixels(cups_raster_t *r,	/* I - Raster stream */
                      unsigned char *p,	/* I - Bytes to write */
		      unsigned      len)/* I - Number of bytes to write */
{
  int		bytes;			/* Bytes read */
  unsigned	remaining;		/* Bytes remaining */


  if (r == NULL || r->mode != CUPS_RASTER_WRITE)
    return (0);

  remaining = len;

  while (remaining > 0)
  {
    bytes = write(r->fd, p, remaining);

    if (bytes == 0)
      return (0);
    else if (bytes < 0)
    {
      if (errno != EINTR)
        return (0);
      else
        continue;
    }

    remaining -= bytes;
    p += bytes;
  }

  return (len);
}


/*
 * End of "$Id: raster.c,v 1.7.2.1 2005/07/27 21:58:44 jlovell Exp $".
 */
