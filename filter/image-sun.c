/*
 * "$Id: image-sun.c,v 1.1.1.12 2005/01/04 19:15:59 jlovell Exp $"
 *
 *   Sun Raster image file routines for the Common UNIX Printing System (CUPS).
 *
 *   Copyright 1993-2005 by Easy Software Products.
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
 *   This file is subject to the Apple OS-Developed Software exception.
 *
 * Contents:
 *
 *   ImageReadSunRaster() - Read a SunRaster image file.
 *   read_unsigned()      - Read a 32-bit unsigned integer.
 */

/*
 * Include necessary headers...
 */

#include "image.h"


#define	RAS_MAGIC	0x59a66a95

	/* Sun supported ras_type's */
#define RT_OLD		0	/* Raw pixrect image in 68000 byte order */
#define RT_STANDARD	1	/* Raw pixrect image in 68000 byte order */
#define RT_BYTE_ENCODED	2	/* Run-length compression of bytes */
#define RT_FORMAT_RGB   3       /* XRGB or RGB instead of XBGR or BGR */
#define RT_EXPERIMENTAL 0xffff	/* Reserved for testing */

	/* Sun registered ras_maptype's */
#define RMT_RAW		2
	/* Sun supported ras_maptype's */
#define RMT_NONE	0	/* ras_maplength is expected to be 0 */
#define RMT_EQUAL_RGB	1	/* red[ras_maplength/3],green[],blue[] */

#define RAS_RLE 0x80

/*
 * NOTES:
 * 	Each line of the image is rounded out to a multiple of 16 bits.
 *   This corresponds to the rounding convention used by the memory pixrect
 *   package (/usr/include/pixrect/memvar.h) of the SunWindows system.
 *	The ras_encoding field (always set to 0 by Sun's supported software)
 *   was renamed to ras_length in release 2.0.  As a result, rasterfiles
 *   of type 0 generated by the old software claim to have 0 length; for
 *   compatibility, code reading rasterfiles must be prepared to compute the
 *   true length from the width, height, and depth fields.
 */

/*
 * Local functions...
 */

static unsigned	read_unsigned(FILE *fp);


/*
 * 'ImageReadSunRaster()' - Read a SunRaster image file.
 */

int					/* O - Read status */
ImageReadSunRaster(image_t    *img,	/* IO - Image */
        	   FILE       *fp,	/* I - Image file */
        	   int        primary,	/* I - Primary choice for colorspace */
        	   int        secondary,/* I - Secondary choice for colorspace */
        	   int        saturation,/* I - Color saturation (%) */
        	   int        hue,	/* I - Color hue (degrees) */
		   const ib_t *lut)	/* I - Lookup table for gamma/brightness */
{
  int		i, x, y,
		bpp,			/* Bytes per pixel */
		scanwidth,
		run_count,
		run_value;
  ib_t		*in,
		*out,
		*scanline,
		*scanptr,
		*p,
		bit;
  unsigned	ras_depth,		/* depth (1, 8, or 24 bits) of pixel */
		ras_type,		/* type of file; see RT_* below */
		ras_maplength;		/* length (bytes) of following map */
  unsigned char	cmap[3][256];		/* colormap */


 /*
  * Read the header; we already know that this is a raster file (ImageOpen
  * checks this) so we don't need to check the magic number again.
  */

  fputs("DEBUG: Reading Sun Raster image...\n", stderr);

  read_unsigned(fp); /* Skip magic */
  img->xsize    = read_unsigned(fp);
  img->ysize    = read_unsigned(fp);
  ras_depth     = read_unsigned(fp);
  /* ras_length */read_unsigned(fp);
  ras_type      = read_unsigned(fp);
  /* ras_maptype*/read_unsigned(fp);
  ras_maplength = read_unsigned(fp);

  fprintf(stderr, "DEBUG: ras_width=%d, ras_height=%d, ras_depth=%d, ras_type=%d, ras_maplength=%d\n",
          img->xsize, img->ysize, ras_depth, ras_type, ras_maplength);

  if (ras_maplength > 768 ||
      img->xsize == 0 || img->xsize > IMAGE_MAX_WIDTH ||
      img->ysize == 0 || img->ysize > IMAGE_MAX_HEIGHT ||
      ras_depth == 0 || ras_depth > 32)
  {
    fputs("ERROR: Raster image cannot be loaded!\n", stderr);
    return (1);
  }

  if (ras_maplength > 0)
  {
    memset(cmap[0], 255, sizeof(cmap[0]));
    memset(cmap[1], 0, sizeof(cmap[1]));
    memset(cmap[2], 0, sizeof(cmap[2]));

    fread(cmap[0], 1, ras_maplength / 3, fp);
    fread(cmap[1], 1, ras_maplength / 3, fp);
    fread(cmap[2], 1, ras_maplength / 3, fp);
  }

 /*
  * Compute the width of each line and allocate memory as needed...
  */

  scanwidth = (img->xsize * ras_depth + 7) / 8;
  if (scanwidth & 1)
    scanwidth ++;

  if (ras_depth < 24 && ras_maplength == 0)
  {
    img->colorspace = secondary;
    in = malloc(img->xsize + 1);
  }
  else
  {
    img->colorspace = (primary == IMAGE_RGB_CMYK) ? IMAGE_RGB : primary;
    in = malloc(img->xsize * 3 + 1);
  }

  bpp       = ImageGetDepth(img);
  out       = malloc(img->xsize * bpp);
  scanline  = malloc(scanwidth);
  run_count = 0;
  run_value = 0;

  fprintf(stderr, "DEBUG: bpp=%d, scanwidth=%d\n", bpp, scanwidth);

  for (y = 0; y < img->ysize; y ++)
  {
    if (ras_depth != 8 || ras_maplength > 0)
      p = scanline;
    else
      p = in;

    if (ras_type != RT_BYTE_ENCODED)
      fread(p, scanwidth, 1, fp);
    else
    {
      for (i = scanwidth; i > 0; i --, p ++)
      {
        if (run_count > 0)
        {
          *p = run_value;
          run_count --;
        }
        else
        {
          run_value = getc(fp);

          if (run_value == RAS_RLE)
          {
            run_count = getc(fp);
            if (run_count == 0)
              *p = RAS_RLE;
            else
              run_value = *p = getc(fp);
          }
          else
            *p = run_value;
        }
      }
    }

    if (ras_depth == 1 && ras_maplength == 0)
    {
     /*
      * 1-bit B&W image...
      */

      for (x = img->xsize, bit = 128, scanptr = scanline, p = in;
           x > 0;
           x --, p ++)
      {
	if (*scanptr & bit)
          *p = 255;
        else
          *p = 0;

	if (bit > 1)
	{
          bit = 128;
          scanptr ++;
	}
	else
          bit >>= 1;
      }
    }
    else if (ras_depth == 1)
    {
     /*
      * 1-bit colormapped image...
      */

      for (x = img->xsize, bit = 128, scanptr = scanline, p = in;
           x > 0;
           x --)
      {
	if (*scanptr & bit)
	{
          *p++ = cmap[0][1];
          *p++ = cmap[1][1];
          *p++ = cmap[2][1];
	}
        else
	{
          *p++ = cmap[0][0];
          *p++ = cmap[1][0];
          *p++ = cmap[2][0];
	}

	if (bit > 1)
	{
          bit = 128;
          scanptr ++;
	}
	else
          bit >>= 1;
      }
    }
    else if (ras_depth == 8 && ras_maplength > 0)
    {
     /*
      * 8-bit colormapped image.
      */

      for (x = img->xsize, scanptr = scanline, p = in;
           x > 0;
           x --)
      {
        *p++ = cmap[0][*scanptr];
        *p++ = cmap[1][*scanptr];
        *p++ = cmap[2][*scanptr++];
      }
    }
    else if (ras_depth == 24 && ras_type != RT_FORMAT_RGB)
    {
     /*
      * Convert BGR to RGB...
      */

      for (x = img->xsize, scanptr = scanline, p = in;
           x > 0;
           x --, scanptr += 3)
      {
        *p++ = scanptr[2];
        *p++ = scanptr[1];
        *p++ = scanptr[0];
      }
    }

    if (ras_depth <= 8 && ras_maplength == 0)
    {
      if (img->colorspace == IMAGE_WHITE)
      {
        if (lut)
	  ImageLut(in, img->xsize, lut);

        ImagePutRow(img, 0, y, img->xsize, in);
      }
      else
      {
	switch (img->colorspace)
	{
	  case IMAGE_RGB :
	      ImageWhiteToRGB(in, out, img->xsize);
	      break;
	  case IMAGE_BLACK :
	      ImageWhiteToBlack(in, out, img->xsize);
	      break;
	  case IMAGE_CMY :
	      ImageWhiteToCMY(in, out, img->xsize);
	      break;
	  case IMAGE_CMYK :
	      ImageWhiteToCMYK(in, out, img->xsize);
	      break;
	}

        if (lut)
	  ImageLut(out, img->xsize * bpp, lut);

        ImagePutRow(img, 0, y, img->xsize, out);
      }
    }
    else
    {
      if (img->colorspace == IMAGE_RGB)
      {
	if (saturation != 100 || hue != 0)
	  ImageRGBAdjust(in, img->xsize, saturation, hue);

        if (lut)
	  ImageLut(in, img->xsize * 3, lut);

        ImagePutRow(img, 0, y, img->xsize, in);
      }
      else
      {
	if ((saturation != 100 || hue != 0) && bpp > 1)
	  ImageRGBAdjust(in, img->xsize, saturation, hue);

	switch (img->colorspace)
	{
	  case IMAGE_WHITE :
	      ImageRGBToWhite(in, out, img->xsize);
	      break;
	  case IMAGE_BLACK :
	      ImageRGBToBlack(in, out, img->xsize);
	      break;
	  case IMAGE_CMY :
	      ImageRGBToCMY(in, out, img->xsize);
	      break;
	  case IMAGE_CMYK :
	      ImageRGBToCMYK(in, out, img->xsize);
	      break;
	}

        if (lut)
	  ImageLut(out, img->xsize * bpp, lut);

        ImagePutRow(img, 0, y, img->xsize, out);
      }
    }
  }

  free(scanline);
  free(in);
  free(out);

  fclose(fp);

  return (0);
}


/*
 * 'read_unsigned()' - Read a 32-bit unsigned integer.
 */

static unsigned		/* O - Integer from file */
read_unsigned(FILE *fp)	/* I - File to read from */
{
  unsigned	v;	/* Integer from file */


  v = getc(fp);
  v = (v << 8) | getc(fp);
  v = (v << 8) | getc(fp);
  v = (v << 8) | getc(fp);

  return (v);
}


/*
 * End of "$Id: image-sun.c,v 1.1.1.12 2005/01/04 19:15:59 jlovell Exp $".
 */
