//========================================================================
//
// CairoOutputDev.cc
//
// Copyright 2003 Glyph & Cog, LLC
// Copyright 2004 Red Hat, Inc
//
//========================================================================

#include <config.h>

#ifdef USE_GCC_PRAGMAS
#pragma implementation
#endif

#include <string.h>
#include <math.h>
#include <cairo.h>

#include "goo/gfile.h"
#include "GlobalParams.h"
#include "Error.h"
#include "Object.h"
#include "GfxState.h"
#include "GfxFont.h"
#include "Link.h"
#include "CharCodeToUnicode.h"
#include "FontEncodingTables.h"
#include <fofi/FoFiTrueType.h>
#include <splash/SplashBitmap.h>
#include "CairoOutputDev.h"
#include "CairoFontEngine.h"

//------------------------------------------------------------------------

#define soutRound(x) ((int)(x + 0.5))

//#define LOG_CAIRO

#ifdef LOG_CAIRO
#define LOG(x) (x)
#else
#define LOG(x)
#endif


//------------------------------------------------------------------------
// CairoOutputDev
//------------------------------------------------------------------------

CairoOutputDev::CairoOutputDev(void) {
  xref = NULL;

  FT_Init_FreeType(&ft_lib);
  fontEngine = NULL;
}

CairoOutputDev::~CairoOutputDev() {
  if (fontEngine) {
    delete fontEngine;
  }
  cairo_destroy (cairo);
  FT_Done_FreeType(ft_lib);

}

void CairoOutputDev::startDoc(XRef *xrefA) {
  xref = xrefA;
  if (fontEngine) {
    delete fontEngine;
  }
  fontEngine = new CairoFontEngine(ft_lib);
}

void CairoOutputDev::startPage(int pageNum, GfxState *state) {
  cairo_destroy (cairo);
  createCairo (state);
  
  cairo_reset_clip (cairo);
  cairo_set_source_rgb (cairo, 0, 0, 0);
  cairo_set_operator (cairo, CAIRO_OPERATOR_OVER);
  cairo_set_line_cap (cairo, CAIRO_LINE_CAP_BUTT);
  cairo_set_line_join (cairo, CAIRO_LINE_JOIN_MITER);
  cairo_set_dash (cairo, NULL, 0, 0.0);
  cairo_set_miter_limit (cairo, 10);
  cairo_set_tolerance (cairo, 1);
}

void CairoOutputDev::endPage() {
}

void CairoOutputDev::drawLink(Link *link, Catalog *catalog) {
}

void CairoOutputDev::saveState(GfxState *state) {
  LOG(printf ("save\n"));
  cairo_save (cairo);
}

void CairoOutputDev::restoreState(GfxState *state) {
  LOG(printf ("restore\n"));
  cairo_restore (cairo);
  /* TODO: Is this really needed for cairo? Maybe not */
  needFontUpdate = gTrue;
}

void CairoOutputDev::updateAll(GfxState *state) {
  updateLineDash(state);
  updateLineJoin(state);
  updateLineCap(state);
  updateLineWidth(state);
  updateFlatness(state);
  updateMiterLimit(state);
  updateFillColor(state);
  updateStrokeColor(state);
  needFontUpdate = gTrue;
}

void CairoOutputDev::updateCTM(GfxState *state, double m11, double m12,
				double m21, double m22,
				double m31, double m32) {
  updateLineDash(state);
  updateLineJoin(state);
  updateLineCap(state);
  updateLineWidth(state);
}

void CairoOutputDev::updateLineDash(GfxState *state) {
  double *dashPattern;
  int dashLength;
  double dashStart;
  double *transformedDash;
  double transformedStart;
  int i;

  state->getLineDash(&dashPattern, &dashLength, &dashStart);

  transformedDash = new double[dashLength];
  
  for (i = 0; i < dashLength; ++i) {
    transformedDash[i] =  state->transformWidth(dashPattern[i]);
  }
  transformedStart = state->transformWidth(dashStart);
  cairo_set_dash (cairo, transformedDash, dashLength, transformedStart);
  delete [] transformedDash;
}

void CairoOutputDev::updateFlatness(GfxState *state) {
  cairo_set_tolerance (cairo, state->getFlatness());
}

void CairoOutputDev::updateLineJoin(GfxState *state) {
  switch (state->getLineJoin()) {
  case 0:
    cairo_set_line_join (cairo, CAIRO_LINE_JOIN_MITER);
    break;
  case 1:
    cairo_set_line_join (cairo, CAIRO_LINE_JOIN_ROUND);
    break;
  case 2:
    cairo_set_line_join (cairo, CAIRO_LINE_JOIN_BEVEL);
    break;
  }
}

void CairoOutputDev::updateLineCap(GfxState *state) {
  switch (state->getLineCap()) {
  case 0:
    cairo_set_line_cap (cairo, CAIRO_LINE_CAP_BUTT);
    break;
  case 1:
    cairo_set_line_cap (cairo, CAIRO_LINE_CAP_ROUND);
    break;
  case 2:
    cairo_set_line_cap (cairo, CAIRO_LINE_CAP_SQUARE);
    break;
  }
}

void CairoOutputDev::updateMiterLimit(GfxState *state) {
  cairo_set_miter_limit (cairo, state->getMiterLimit());
}

void CairoOutputDev::updateLineWidth(GfxState *state) {
  LOG(printf ("line width: %f\n", state->getTransformedLineWidth()));
  cairo_set_line_width (cairo, state->getTransformedLineWidth());
}

void CairoOutputDev::updateFillColor(GfxState *state) {
  state->getFillRGB(&fill_color);
  LOG(printf ("fill color: %f %f %f\n", fill_color.r, fill_color.g, fill_color.b));
}

void CairoOutputDev::updateStrokeColor(GfxState *state) {
  state->getStrokeRGB(&stroke_color);
  LOG(printf ("stroke color: %f %f %f\n", stroke_color.r, stroke_color.g, stroke_color.b));
}

void CairoOutputDev::updateFont(GfxState *state) {
  cairo_font_face_t *font_face;
  double m11, m12, m21, m22;
  double w;
  cairo_matrix_t matrix;

  LOG(printf ("updateFont() font=%s\n", state->getFont()->getName()->getCString()));
  
  /* Needs to be rethough, since fonts are now handled by cairo */
  needFontUpdate = gFalse;

  currentFont = fontEngine->getFont (state->getFont(), xref);

  state->getFontTransMat(&m11, &m12, &m21, &m22);
  m11 *= state->getHorizScaling();
  m12 *= state->getHorizScaling();

  w = currentFont->getSubstitutionCorrection(state->getFont());
  m12 *= -w;
  m22 *= -w;

  LOG(printf ("font matrix: %f %f %f %f\n", m11, m12, m21, m22));
  
  font_face = currentFont->getFontFace();
  cairo_set_font_face (cairo, font_face);

  matrix.xx = m11;
  matrix.xy = m12;
  matrix.yx = m21;
  matrix.yy = m22;
  matrix.x0 = 0;
  matrix.y0 = 0;
  cairo_set_font_matrix (cairo, &matrix);
}

void CairoOutputDev::doPath(GfxState *state, GfxPath *path,
			    GBool snapToGrid) {
  GfxSubpath *subpath;
  double x1, y1, x2, y2, x3, y3;
  int i, j;

  for (i = 0; i < path->getNumSubpaths(); ++i) {
    subpath = path->getSubpath(i);
    if (subpath->getNumPoints() > 0) {
      state->transform(subpath->getX(0), subpath->getY(0), &x1, &y1);
      if (snapToGrid) {
	x1 = round (x1); y1 = round (y1);
      }
      cairo_move_to (cairo, x1, y1);
      LOG (printf ("move_to %f, %f\n", x1, y1));
      j = 1;
      while (j < subpath->getNumPoints()) {
	if (subpath->getCurve(j)) {
	  if (snapToGrid) {
	    x1 = round (x1); y1 = round (y1);
	    x2 = round (x2); y2 = round (y2);
	    x3 = round (x3); y3 = round (y3);
	  }
	  state->transform(subpath->getX(j), subpath->getY(j), &x1, &y1);
	  state->transform(subpath->getX(j+1), subpath->getY(j+1), &x2, &y2);
	  state->transform(subpath->getX(j+2), subpath->getY(j+2), &x3, &y3);
	  cairo_curve_to (cairo, 
			  x1, y1,
			  x2, y2,
			  x3, y3);
	  LOG (printf ("curve_to %f, %f  %f, %f  %f, %f\n", x1, y1, x2, y2, x3, y3));
	  j += 3;
	} else {
	  state->transform(subpath->getX(j), subpath->getY(j), &x1, &y1);
	  if (snapToGrid) {
	    x1 = round (x1); y1 = round (y1);
	  }
	  cairo_line_to (cairo, x1, y1);
	  LOG(printf ("line_to %f, %f\n", x1, y1));
	  ++j;
	}
      }
      if (subpath->isClosed()) {
	LOG (printf ("close\n"));
	cairo_close_path (cairo);
      }
    }
  }
}

void CairoOutputDev::stroke(GfxState *state) {
  doPath (state, state->getPath(), gFalse);
  cairo_set_source_rgb (cairo,
		       stroke_color.r, stroke_color.g, stroke_color.b);
  LOG(printf ("stroke\n"));
  cairo_stroke (cairo);
}

void CairoOutputDev::fill(GfxState *state) {
  doPath (state, state->getPath(), gFalse);
  cairo_set_fill_rule (cairo, CAIRO_FILL_RULE_WINDING);
  cairo_set_source_rgb (cairo,
		       fill_color.r, fill_color.g, fill_color.b);
  LOG(printf ("fill\n"));
  cairo_fill (cairo);
}

void CairoOutputDev::eoFill(GfxState *state) {
  doPath (state, state->getPath(), gFalse);
  cairo_set_fill_rule (cairo, CAIRO_FILL_RULE_EVEN_ODD);
  cairo_set_source_rgb (cairo,
		       fill_color.r, fill_color.g, fill_color.b);
  LOG(printf ("fill-eo\n"));
  cairo_fill (cairo);
}

void CairoOutputDev::clip(GfxState *state, GBool snapToGrid) {
  doPath (state, state->getPath(), snapToGrid);
  cairo_set_fill_rule (cairo, CAIRO_FILL_RULE_WINDING);
  cairo_clip (cairo);
  cairo_new_path (cairo); /* Consume path */
  LOG (printf ("clip\n"));
}

void CairoOutputDev::eoClip(GfxState *state) {
  doPath (state, state->getPath(), gFalse);
  cairo_set_fill_rule (cairo, CAIRO_FILL_RULE_EVEN_ODD);
  cairo_clip (cairo);
  cairo_new_path (cairo); /* Consume path */
  LOG (printf ("clip-eo\n"));
}

void CairoOutputDev::drawString(GfxState *state, GooString *s)
{
  GfxFont *font;
  int wMode;
  int render;
  // the number of bytes in the string and not the number of glyphs?
  int len = s->getLength();
  // need at most len glyphs
  cairo_glyph_t *glyphs;
  
  char *p = s->getCString();
  int count = 0;
  double curX, curY;
  double riseX, riseY;

  font = state->getFont();
  wMode = font->getWMode();
 
  if (needFontUpdate) {
    updateFont(state);
  }
  if (!currentFont) {
    return;
  }
   
  // check for invisible text -- this is used by Acrobat Capture
  render = state->getRender();
  if (render == 3) {
    return;
  }

  // ignore empty strings
  if (len == 0)
    return;
  
  glyphs = (cairo_glyph_t *) gmalloc (len * sizeof (cairo_glyph_t));

  state->textTransformDelta(0, state->getRise(), &riseX, &riseY);
  curX = state->getCurX();
  curY = state->getCurY();
  while (len > 0) {
    double x, y;
    double x1, y1;
    double dx, dy, tdx, tdy;
    double originX, originY, tOriginX, tOriginY;
    int n, uLen;
    CharCode code;
    Unicode u[8];
    n = font->getNextChar(p, len, &code,
	                  u, (int)(sizeof(u) / sizeof(Unicode)), &uLen,
			  &dx, &dy, &originX, &originY);
    if (wMode) {
      dx *= state->getFontSize();
      dy = dy * state->getFontSize() + state->getCharSpace();
      if (n == 1 && *p == ' ') {
	dy += state->getWordSpace();
      }
    } else {
      dx = dx * state->getFontSize() + state->getCharSpace();
      if (n == 1 && *p == ' ') {
	dx += state->getWordSpace();
      }
      dx *= state->getHorizScaling();
      dy *= state->getFontSize();
    }
    originX *= state->getFontSize();
    originY *= state->getFontSize();
    state->textTransformDelta(dx, dy, &tdx, &tdy);
    state->textTransformDelta(originX, originY, &tOriginX, &tOriginY);
    x = curX + riseX;
    y = curY + riseY;
    x -= tOriginX;
    y -= tOriginY;
    state->transform(x, y, &x1, &y1);

    glyphs[count].index = currentFont->getGlyph (code, u, uLen);
    glyphs[count].x = x1;
    glyphs[count].y = y1;
    curX += tdx;
    curY += tdy;
    p += n;
    len -= n;
    count++;
  }
  // fill
  if (!(render & 1)) {
    LOG (printf ("fill string\n"));
    cairo_set_source_rgb (cairo,
			 fill_color.r, fill_color.g, fill_color.b);
    cairo_show_glyphs (cairo, glyphs, count);
  }
  
  // stroke
  if ((render & 3) == 1 || (render & 3) == 2) {
    LOG (printf ("stroke string\n"));
    cairo_set_source_rgb (cairo,
			 stroke_color.r, stroke_color.g, stroke_color.b);
    cairo_glyph_path (cairo, glyphs, count);
    cairo_stroke (cairo);
  }

  // clip
  if (render & 4) {
    // FIXME: This is quite right yet, we need to accumulate all
    // glyphs within one text object before we clip.  Right now this
    // just add this one string.
    LOG (printf ("clip string\n"));
    cairo_glyph_path (cairo, glyphs, count);
    cairo_clip (cairo);
  }
  
  gfree (glyphs);
}

GBool CairoOutputDev::beginType3Char(GfxState *state, double x, double y,
				      double dx, double dy,
				      CharCode code, Unicode *u, int uLen) {
  return gFalse;
}

void CairoOutputDev::endType3Char(GfxState *state) {
}

void CairoOutputDev::type3D0(GfxState *state, double wx, double wy) {
}

void CairoOutputDev::type3D1(GfxState *state, double wx, double wy,
			      double llx, double lly, double urx, double ury) {
}

void CairoOutputDev::endTextObject(GfxState *state) {
}


void CairoOutputDev::drawImageMask(GfxState *state, Object *ref, Stream *str,
				    int width, int height, GBool invert,
				    GBool inlineImg) {
  unsigned char *buffer;
  unsigned char *dest;
  cairo_surface_t *image;
  cairo_pattern_t *pattern;
  int x, y;
  ImageStream *imgStr;
  Guchar *pix;
  double *ctm;
  cairo_matrix_t matrix;
  int invert_bit;
  int row_stride;

  row_stride = (width + 3) & ~3;
  buffer = (unsigned char *) malloc (height * row_stride);
  if (buffer == NULL) {
    error(-1, "Unable to allocate memory for image.");
    return;
  }

  /* TODO: Do we want to cache these? */
  imgStr = new ImageStream(str, width, 1, 1);
  imgStr->reset();

  invert_bit = invert ? 1 : 0;

  for (y = 0; y < height; y++) {
    pix = imgStr->getLine();
    dest = buffer + y * row_stride;
    for (x = 0; x < width; x++) {

      if (pix[x] ^ invert_bit)
	*dest++ = 0;
      else
	*dest++ = 255;
    }
  }

  image = cairo_image_surface_create_for_data (buffer, CAIRO_FORMAT_A8,
					  width, height, row_stride);
  if (image == NULL)
    return;
  pattern = cairo_pattern_create_for_surface (image);
  if (pattern == NULL)
    return;

  ctm = state->getCTM();
  LOG (printf ("drawImageMask %dx%d, matrix: %f, %f, %f, %f, %f, %f\n",
	       width, height, ctm[0], ctm[1], ctm[2], ctm[3], ctm[4], ctm[5]));
  matrix.xx = ctm[0] / width;
  matrix.xy = ctm[1] / width;
  matrix.yx = -ctm[2] / height;
  matrix.yy = -ctm[3] / height;
  matrix.x0 = ctm[2] + ctm[4];
  matrix.y0 = ctm[3] + ctm[5];
  cairo_matrix_invert (&matrix);
  cairo_pattern_set_matrix (pattern, &matrix);

  cairo_pattern_set_filter (pattern, CAIRO_FILTER_BEST);
  /* FIXME: Doesn't the image mask support any colorspace? */
  cairo_set_source_rgb (cairo, fill_color.r, fill_color.g, fill_color.b);
  cairo_mask (cairo, pattern);

  cairo_pattern_destroy (pattern);
  cairo_surface_destroy (image);
  free (buffer);
  delete imgStr;
}

void CairoOutputDev::drawImage(GfxState *state, Object *ref, Stream *str,
				int width, int height,
				GfxImageColorMap *colorMap,
				int *maskColors, GBool inlineImg) {
  unsigned char *buffer;
  unsigned char *dest;
  cairo_surface_t *image;
  cairo_pattern_t *pattern;
  int x, y;
  ImageStream *imgStr;
  Guchar *pix;
  GfxRGB rgb;
  int alpha, i;
  double *ctm;
  cairo_matrix_t matrix;
  int is_identity_transform;
  
  buffer = (unsigned char *)malloc (width * height * 4);

  if (buffer == NULL) {
    error(-1, "Unable to allocate memory for image.");
    return;
  }

  /* TODO: Do we want to cache these? */
  imgStr = new ImageStream(str, width,
			   colorMap->getNumPixelComps(),
			   colorMap->getBits());
  imgStr->reset();
  
  /* ICCBased color space doesn't do any color correction
   * so check its underlying color space as well */
  is_identity_transform = colorMap->getColorSpace()->getMode() == csDeviceRGB ||
		  colorMap->getColorSpace()->getMode() == csICCBased && 
		  ((GfxICCBasedColorSpace*)colorMap->getColorSpace())->getAlt()->getMode() == csDeviceRGB;
  
  for (y = 0; y < height; y++) {
    dest = buffer + y * 4 * width;
    pix = imgStr->getLine();
    for (x = 0; x < width; x++, pix += colorMap->getNumPixelComps()) {
      if (maskColors) {
	alpha = 0;
	for (i = 0; i < colorMap->getNumPixelComps(); ++i) {
	  if (pix[i] < maskColors[2*i] ||
	      pix[i] > maskColors[2*i+1]) {
	    alpha = 255;
	    break;
	  }
	}
      } else {
	alpha = 255;
      }
      if (is_identity_transform) {
	*dest++ = pix[2];
	*dest++ = pix[1];
	*dest++ = pix[0];
      } else {      
	colorMap->getRGB(pix, &rgb);
	*dest++ = soutRound(255 * rgb.b);
	*dest++ = soutRound(255 * rgb.g);
	*dest++ = soutRound(255 * rgb.r);
      }
      *dest++ = alpha;
    }
  }

  image = cairo_image_surface_create_for_data (buffer, CAIRO_FORMAT_ARGB32,
					       width, height, width * 4);
  if (image == NULL)
    return;
  pattern = cairo_pattern_create_for_surface (image);
  if (pattern == NULL)
    return;

  ctm = state->getCTM();
  LOG (printf ("drawImageMask %dx%d, matrix: %f, %f, %f, %f, %f, %f\n",
	       width, height, ctm[0], ctm[1], ctm[2], ctm[3], ctm[4], ctm[5]));
  matrix.xx = ctm[0] / width;
  matrix.xy = ctm[1] / width;
  matrix.yx = -ctm[2] / height;
  matrix.yy = -ctm[3] / height;
  matrix.x0 = ctm[2] + ctm[4];
  matrix.y0 = ctm[3] + ctm[5];

  cairo_matrix_invert (&matrix);
  cairo_pattern_set_matrix (pattern, &matrix);

  cairo_pattern_set_filter (pattern, CAIRO_FILTER_BEST);
  cairo_set_source (cairo, pattern);
  cairo_paint (cairo);

  cairo_pattern_destroy (pattern);
  cairo_surface_destroy (image);
  free (buffer);
  delete imgStr;
}
