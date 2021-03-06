/*
 * Unit test suite for brushes
 *
 * Copyright (C) 2007 Google (Evan Stade)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include "windows.h"
#include "gdiplus.h"
#include "wine/test.h"
#include <math.h>

#define expect(expected, got) ok(got == expected, "Expected %.8x, got %.8x\n", expected, got)
#define expectf(expected, got) ok(fabs(expected - got) < 0.0001, "Expected %.2f, got %.2f\n", expected, got)

static void test_constructor_destructor(void)
{
    GpStatus status;
    GpSolidFill *brush = NULL;

    status = GdipCreateSolidFill((ARGB)0xdeadbeef, &brush);
    expect(Ok, status);
    ok(brush != NULL, "Expected brush to be initialized\n");

    status = GdipDeleteBrush(NULL);
    expect(InvalidParameter, status);

    status = GdipDeleteBrush((GpBrush*) brush);
    expect(Ok, status);
}

static void test_type(void)
{
    GpStatus status;
    GpBrushType bt;
    GpSolidFill *brush = NULL;

    GdipCreateSolidFill((ARGB)0xdeadbeef, &brush);

    status = GdipGetBrushType((GpBrush*)brush, &bt);
    expect(Ok, status);
    expect(BrushTypeSolidColor, bt);

    GdipDeleteBrush((GpBrush*) brush);
}
static GpPointF blendcount_ptf[] = {{0.0, 0.0},
                                    {50.0, 50.0}};
static void test_gradientblendcount(void)
{
    GpStatus status;
    GpPathGradient *brush;
    INT count;

    status = GdipCreatePathGradient(blendcount_ptf, 2, WrapModeClamp, &brush);
    expect(Ok, status);

    status = GdipGetPathGradientBlendCount(NULL, NULL);
    expect(InvalidParameter, status);
    status = GdipGetPathGradientBlendCount(NULL, &count);
    expect(InvalidParameter, status);
    status = GdipGetPathGradientBlendCount(brush, NULL);
    expect(InvalidParameter, status);

    status = GdipGetPathGradientBlendCount(brush, &count);
    expect(Ok, status);
    expect(1, count);

    GdipDeleteBrush((GpBrush*) brush);
}

static GpPointF getblend_ptf[] = {{0.0, 0.0},
                                  {50.0, 50.0}};
static void test_getblend(void)
{
    GpStatus status;
    GpPathGradient *brush;
    REAL blends[4];
    REAL pos[4];

    status = GdipCreatePathGradient(getblend_ptf, 2, WrapModeClamp, &brush);
    expect(Ok, status);

    /* check some invalid parameters combinations */
    status = GdipGetPathGradientBlend(NULL, NULL,  NULL, -1);
    expect(InvalidParameter, status);
    status = GdipGetPathGradientBlend(brush,NULL,  NULL, -1);
    expect(InvalidParameter, status);
    status = GdipGetPathGradientBlend(NULL, blends,NULL, -1);
    expect(InvalidParameter, status);
    status = GdipGetPathGradientBlend(NULL, NULL,  pos,  -1);
    expect(InvalidParameter, status);
    status = GdipGetPathGradientBlend(NULL, NULL,  NULL,  1);
    expect(InvalidParameter, status);

    blends[0] = (REAL)0xdeadbeef;
    pos[0]    = (REAL)0xdeadbeef;
    status = GdipGetPathGradientBlend(brush, blends, pos, 1);
    expect(Ok, status);
    expectf(1.0, blends[0]);
    expectf((REAL)0xdeadbeef, pos[0]);

    GdipDeleteBrush((GpBrush*) brush);
}

static GpPointF getbounds_ptf[] = {{0.0, 20.0},
                                   {50.0, 50.0},
                                   {21.0, 25.0},
                                   {25.0, 46.0}};
static void test_getbounds(void)
{
    GpStatus status;
    GpPathGradient *brush;
    GpRectF bounds;

    status = GdipCreatePathGradient(getbounds_ptf, 4, WrapModeClamp, &brush);
    expect(Ok, status);

    status = GdipGetPathGradientRect(NULL, NULL);
    expect(InvalidParameter, status);
    status = GdipGetPathGradientRect(brush, NULL);
    expect(InvalidParameter, status);
    status = GdipGetPathGradientRect(NULL, &bounds);
    expect(InvalidParameter, status);

    status = GdipGetPathGradientRect(brush, &bounds);
    expect(Ok, status);
    expectf(0.0, bounds.X);
    expectf(20.0, bounds.Y);
    expectf(50.0, bounds.Width);
    expectf(30.0, bounds.Height);

    GdipDeleteBrush((GpBrush*) brush);
}

static void test_getgamma(void)
{
    GpStatus status;
    GpLineGradient *line;
    GpPointF start, end;
    BOOL gamma;

    start.X = start.Y = 0.0;
    end.X = end.Y = 100.0;

    status = GdipCreateLineBrush(&start, &end, (ARGB)0xdeadbeef, 0xdeadbeef, WrapModeTile, &line);
    expect(Ok, status);

    /* NULL arguments */
    status = GdipGetLineGammaCorrection(NULL, NULL);
    expect(InvalidParameter, status);
    status = GdipGetLineGammaCorrection(line, NULL);
    expect(InvalidParameter, status);
    status = GdipGetLineGammaCorrection(NULL, &gamma);
    expect(InvalidParameter, status);

    GdipDeleteBrush((GpBrush*)line);
}

static void test_transform(void)
{
    GpStatus status;
    GpTexture *texture;
    GpGraphics *graphics = NULL;
    GpBitmap *bitmap;
    HDC hdc = GetDC(0);
    GpMatrix *m, *m1;
    BOOL res;

    status = GdipCreateMatrix2(2.0, 0.0, 0.0, 0.0, 0.0, 0.0, &m);
    expect(Ok, status);

    status = GdipCreateFromHDC(hdc, &graphics);
    expect(Ok, status);
    status = GdipCreateBitmapFromGraphics(1, 1, graphics, &bitmap);
    expect(Ok, status);

    status = GdipCreateTexture((GpImage*)bitmap, WrapModeTile, &texture);
    expect(Ok, status);

    /* NULL */
    status = GdipGetTextureTransform(NULL, NULL);
    expect(InvalidParameter, status);
    status = GdipGetTextureTransform(texture, NULL);
    expect(InvalidParameter, status);

    /* get default value - identity matrix */
    status = GdipGetTextureTransform(texture, m);
    expect(Ok, status);
    status = GdipIsMatrixIdentity(m, &res);
    expect(Ok, status);
    expect(TRUE, res);
    /* set and get then */
    status = GdipCreateMatrix2(2.0, 0.0, 0.0, 2.0, 0.0, 0.0, &m1);
    expect(Ok, status);
    status = GdipSetTextureTransform(texture, m1);
    expect(Ok, status);
    status = GdipGetTextureTransform(texture, m);
    expect(Ok, status);
    status = GdipIsMatrixEqual(m, m1, &res);
    expect(Ok, status);
    expect(TRUE, res);
    /* reset */
    status = GdipResetTextureTransform(texture);
    expect(Ok, status);
    status = GdipGetTextureTransform(texture, m);
    expect(Ok, status);
    status = GdipIsMatrixIdentity(m, &res);
    expect(Ok, status);
    expect(TRUE, res);

    status = GdipDeleteBrush((GpBrush*)texture);
    expect(Ok, status);

    status = GdipDeleteMatrix(m1);
    expect(Ok, status);
    status = GdipDeleteMatrix(m);
    expect(Ok, status);
    status = GdipDisposeImage((GpImage*)bitmap);
    expect(Ok, status);
    status = GdipDeleteGraphics(graphics);
    expect(Ok, status);
    ReleaseDC(0, hdc);
}

static void test_texturewrap(void)
{
    GpStatus status;
    GpTexture *texture;
    GpGraphics *graphics = NULL;
    GpBitmap *bitmap;
    HDC hdc = GetDC(0);
    GpWrapMode wrap;

    status = GdipCreateFromHDC(hdc, &graphics);
    expect(Ok, status);
    status = GdipCreateBitmapFromGraphics(1, 1, graphics, &bitmap);
    expect(Ok, status);

    status = GdipCreateTexture((GpImage*)bitmap, WrapModeTile, &texture);
    expect(Ok, status);

    /* NULL */
    status = GdipGetTextureWrapMode(NULL, NULL);
    expect(InvalidParameter, status);
    status = GdipGetTextureWrapMode(texture, NULL);
    expect(InvalidParameter, status);
    status = GdipGetTextureWrapMode(NULL, &wrap);
    expect(InvalidParameter, status);

    /* get */
    wrap = WrapModeClamp;
    status = GdipGetTextureWrapMode(texture, &wrap);
    expect(Ok, status);
    expect(WrapModeTile, wrap);
    /* set, then get */
    wrap = WrapModeClamp;
    status = GdipSetTextureWrapMode(texture, wrap);
    expect(Ok, status);
    wrap = WrapModeTile;
    status = GdipGetTextureWrapMode(texture, &wrap);
    expect(Ok, status);
    expect(WrapModeClamp, wrap);

    status = GdipDeleteBrush((GpBrush*)texture);
    expect(Ok, status);
    status = GdipDisposeImage((GpImage*)bitmap);
    expect(Ok, status);
    status = GdipDeleteGraphics(graphics);
    expect(Ok, status);
    ReleaseDC(0, hdc);
}

static void test_gradientgetrect(void)
{
    GpLineGradient *brush;
    GpMatrix *transform;
    REAL elements[6];
    GpRectF rectf;
    GpStatus status;
    GpPointF pt1, pt2;

    status = GdipCreateMatrix(&transform);
    expect(Ok, status);

    pt1.X = pt1.Y = 1.0;
    pt2.X = pt2.Y = 100.0;
    status = GdipCreateLineBrush(&pt1, &pt2, 0, 0, WrapModeTile, &brush);
    expect(Ok, status);
    memset(&rectf, 0, sizeof(GpRectF));
    status = GdipGetLineRect(brush, &rectf);
    expect(Ok, status);
    expectf(1.0, rectf.X);
    expectf(1.0, rectf.Y);
    expectf(99.0, rectf.Width);
    expectf(99.0, rectf.Height);
    status = GdipGetLineTransform(brush, transform);
    todo_wine expect(Ok, status);
    if (status == Ok)
    {
        status = GdipGetMatrixElements(transform, elements);
        expectf(1.0, elements[0]);
        expectf(1.0, elements[1]);
        expectf(-1.0, elements[2]);
        expectf(1.0, elements[3]);
        expectf(50.50, elements[4]);
        expectf(-50.50, elements[5]);
    }
    status = GdipDeleteBrush((GpBrush*)brush);
    /* vertical gradient */
    pt1.X = pt1.Y = pt2.X = 0.0;
    pt2.Y = 10.0;
    status = GdipCreateLineBrush(&pt1, &pt2, 0, 0, WrapModeTile, &brush);
    expect(Ok, status);
    memset(&rectf, 0, sizeof(GpRectF));
    status = GdipGetLineRect(brush, &rectf);
    expect(Ok, status);
    expectf(-5.0, rectf.X);
    expectf(0.0, rectf.Y);
    expectf(10.0, rectf.Width);
    expectf(10.0, rectf.Height);
    status = GdipGetLineTransform(brush, transform);
    todo_wine expect(Ok, status);
    if (status == Ok)
    {
        status = GdipGetMatrixElements(transform, elements);
        expectf(0.0, elements[0]);
        expectf(1.0, elements[1]);
        expectf(-1.0, elements[2]);
        expectf(0.0, elements[3]);
        expectf(5.0, elements[4]);
        expectf(5.0, elements[5]);
    }
    status = GdipDeleteBrush((GpBrush*)brush);
    /* horizontal gradient */
    pt1.X = pt1.Y = pt2.Y = 0.0;
    pt2.X = 10.0;
    status = GdipCreateLineBrush(&pt1, &pt2, 0, 0, WrapModeTile, &brush);
    expect(Ok, status);
    memset(&rectf, 0, sizeof(GpRectF));
    status = GdipGetLineRect(brush, &rectf);
    expect(Ok, status);
    expectf(0.0, rectf.X);
    expectf(-5.0, rectf.Y);
    expectf(10.0, rectf.Width);
    expectf(10.0, rectf.Height);
    status = GdipGetLineTransform(brush, transform);
    todo_wine expect(Ok, status);
    if (status == Ok)
    {
        status = GdipGetMatrixElements(transform, elements);
        expectf(1.0, elements[0]);
        expectf(0.0, elements[1]);
        expectf(0.0, elements[2]);
        expectf(1.0, elements[3]);
        expectf(0.0, elements[4]);
        expectf(0.0, elements[5]);
    }
    status = GdipDeleteBrush((GpBrush*)brush);
    /* slope = -1 */
    pt1.X = pt1.Y = 0.0;
    pt2.X = 20.0;
    pt2.Y = -20.0;
    status = GdipCreateLineBrush(&pt1, &pt2, 0, 0, WrapModeTile, &brush);
    expect(Ok, status);
    memset(&rectf, 0, sizeof(GpRectF));
    status = GdipGetLineRect(brush, &rectf);
    expect(Ok, status);
    expectf(0.0, rectf.X);
    expectf(-20.0, rectf.Y);
    expectf(20.0, rectf.Width);
    expectf(20.0, rectf.Height);
    status = GdipGetLineTransform(brush, transform);
    todo_wine expect(Ok, status);
    if (status == Ok)
    {
        status = GdipGetMatrixElements(transform, elements);
        expectf(1.0, elements[0]);
        expectf(-1.0, elements[1]);
        expectf(1.0, elements[2]);
        expectf(1.0, elements[3]);
        expectf(10.0, elements[4]);
        expectf(10.0, elements[5]);
    }
    status = GdipDeleteBrush((GpBrush*)brush);
    /* slope = 1/100 */
    pt1.X = pt1.Y = 0.0;
    pt2.X = 100.0;
    pt2.Y = 1.0;
    status = GdipCreateLineBrush(&pt1, &pt2, 0, 0, WrapModeTile, &brush);
    expect(Ok, status);
    memset(&rectf, 0, sizeof(GpRectF));
    status = GdipGetLineRect(brush, &rectf);
    expect(Ok, status);
    expectf(0.0, rectf.X);
    expectf(0.0, rectf.Y);
    expectf(100.0, rectf.Width);
    expectf(1.0, rectf.Height);
    status = GdipGetLineTransform(brush, transform);
    todo_wine expect(Ok, status);
    if (status == Ok)
    {
        status = GdipGetMatrixElements(transform, elements);
        expectf(1.0, elements[0]);
        expectf(0.01, elements[1]);
        expectf(-0.02, elements[2]);
        /* expectf(2.0, elements[3]); */
        expectf(0.01, elements[4]);
        /* expectf(-1.0, elements[5]); */
    }
    status = GdipDeleteBrush((GpBrush*)brush);
    /* zero height rect */
    rectf.X = rectf.Y = 10.0;
    rectf.Width = 100.0;
    rectf.Height = 0.0;
    status = GdipCreateLineBrushFromRect(&rectf, 0, 0, LinearGradientModeVertical,
        WrapModeTile, &brush);
    expect(OutOfMemory, status);
    /* zero width rect */
    rectf.X = rectf.Y = 10.0;
    rectf.Width = 0.0;
    rectf.Height = 100.0;
    status = GdipCreateLineBrushFromRect(&rectf, 0, 0, LinearGradientModeHorizontal,
        WrapModeTile, &brush);
    expect(OutOfMemory, status);
    /* from rect with LinearGradientModeHorizontal */
    rectf.X = rectf.Y = 10.0;
    rectf.Width = rectf.Height = 100.0;
    status = GdipCreateLineBrushFromRect(&rectf, 0, 0, LinearGradientModeHorizontal,
        WrapModeTile, &brush);
    expect(Ok, status);
    memset(&rectf, 0, sizeof(GpRectF));
    status = GdipGetLineRect(brush, &rectf);
    expect(Ok, status);
    expectf(10.0, rectf.X);
    expectf(10.0, rectf.Y);
    expectf(100.0, rectf.Width);
    expectf(100.0, rectf.Height);
    status = GdipGetLineTransform(brush, transform);
    todo_wine expect(Ok, status);
    if (status == Ok)
    {
        status = GdipGetMatrixElements(transform, elements);
        expectf(1.0, elements[0]);
        expectf(0.0, elements[1]);
        expectf(0.0, elements[2]);
        expectf(1.0, elements[3]);
        expectf(0.0, elements[4]);
        expectf(0.0, elements[5]);
    }
    status = GdipDeleteBrush((GpBrush*)brush);
    /* passing negative Width/Height to LinearGradientModeHorizontal */
    rectf.X = rectf.Y = 10.0;
    rectf.Width = rectf.Height = -100.0;
    status = GdipCreateLineBrushFromRect(&rectf, 0, 0, LinearGradientModeHorizontal,
        WrapModeTile, &brush);
    expect(Ok, status);
    memset(&rectf, 0, sizeof(GpRectF));
    status = GdipGetLineRect(brush, &rectf);
    expect(Ok, status);
    expectf(10.0, rectf.X);
    expectf(10.0, rectf.Y);
    expectf(-100.0, rectf.Width);
    expectf(-100.0, rectf.Height);
    status = GdipGetLineTransform(brush, transform);
    todo_wine expect(Ok, status);
    if (status == Ok)
    {
        status = GdipGetMatrixElements(transform, elements);
        expectf(1.0, elements[0]);
        expectf(0.0, elements[1]);
        expectf(0.0, elements[2]);
        expectf(1.0, elements[3]);
        expectf(0.0, elements[4]);
        expectf(0.0, elements[5]);
    }
    status = GdipDeleteBrush((GpBrush*)brush);

    GdipDeleteMatrix(transform);
}

static void test_lineblend(void)
{
    GpLineGradient *brush;
    GpStatus status;
    GpPointF pt1, pt2;
    INT count=10;
    int i;
    const REAL factors[5] = {0.0f, 0.1f, 0.5f, 0.9f, 1.0f};
    const REAL positions[5] = {0.0f, 0.2f, 0.5f, 0.8f, 1.0f};
    const REAL two_positions[2] = {0.0f, 1.0f};
    const ARGB colors[5] = {0xff0000ff, 0xff00ff00, 0xff00ffff, 0xffff0000, 0xffffffff};
    REAL res_factors[6] = {0.3f, 0.0f, 0.0f, 0.0f, 0.0f};
    REAL res_positions[6] = {0.3f, 0.0f, 0.0f, 0.0f, 0.0f};
    ARGB res_colors[6] = {0xdeadbeef, 0, 0, 0, 0};

    pt1.X = pt1.Y = pt2.Y = pt2.X = 1.0;
    status = GdipCreateLineBrush(&pt1, &pt2, 0, 0, WrapModeTile, &brush);
    expect(OutOfMemory, status);

    pt1.X = pt1.Y = 1.0;
    pt2.X = pt2.Y = 100.0;
    status = GdipCreateLineBrush(&pt1, &pt2, 0, 0, WrapModeTile, &brush);
    expect(Ok, status);

    status = GdipGetLineBlendCount(NULL, &count);
    expect(InvalidParameter, status);

    status = GdipGetLineBlendCount(brush, NULL);
    expect(InvalidParameter, status);

    status = GdipGetLineBlendCount(brush, &count);
    expect(Ok, status);
    expect(1, count);

    status = GdipGetLineBlend(NULL, res_factors, res_positions, 1);
    expect(InvalidParameter, status);

    status = GdipGetLineBlend(brush, NULL, res_positions, 1);
    expect(InvalidParameter, status);

    status = GdipGetLineBlend(brush, res_factors, NULL, 1);
    expect(InvalidParameter, status);

    status = GdipGetLineBlend(brush, res_factors, res_positions, 0);
    expect(InvalidParameter, status);

    status = GdipGetLineBlend(brush, res_factors, res_positions, -1);
    expect(InvalidParameter, status);

    status = GdipGetLineBlend(brush, res_factors, res_positions, 1);
    expect(Ok, status);

    status = GdipGetLineBlend(brush, res_factors, res_positions, 2);
    expect(Ok, status);

    status = GdipSetLineBlend(NULL, factors, positions, 5);
    expect(InvalidParameter, status);

    status = GdipSetLineBlend(brush, NULL, positions, 5);
    expect(InvalidParameter, status);

    status = GdipSetLineBlend(brush, factors, NULL, 5);
    expect(InvalidParameter, status);

    status = GdipSetLineBlend(brush, factors, positions, 0);
    expect(InvalidParameter, status);

    status = GdipSetLineBlend(brush, factors, positions, -1);
    expect(InvalidParameter, status);

    /* leave off the 0.0 position */
    status = GdipSetLineBlend(brush, &factors[1], &positions[1], 4);
    expect(InvalidParameter, status);

    /* leave off the 1.0 position */
    status = GdipSetLineBlend(brush, factors, positions, 4);
    expect(InvalidParameter, status);

    status = GdipSetLineBlend(brush, factors, positions, 5);
    expect(Ok, status);

    status = GdipGetLineBlendCount(brush, &count);
    expect(Ok, status);
    expect(5, count);

    status = GdipGetLineBlend(brush, res_factors, res_positions, 4);
    expect(InsufficientBuffer, status);

    status = GdipGetLineBlend(brush, res_factors, res_positions, 5);
    expect(Ok, status);

    for (i=0; i<5; i++)
    {
        expectf(factors[i], res_factors[i]);
        expectf(positions[i], res_positions[i]);
    }

    status = GdipGetLineBlend(brush, res_factors, res_positions, 6);
    expect(Ok, status);

    status = GdipSetLineBlend(brush, factors, positions, 1);
    expect(Ok, status);

    status = GdipGetLineBlendCount(brush, &count);
    expect(Ok, status);
    expect(1, count);

    status = GdipGetLineBlend(brush, res_factors, res_positions, 1);
    expect(Ok, status);

    status = GdipGetLinePresetBlendCount(NULL, &count);
    expect(InvalidParameter, status);

    status = GdipGetLinePresetBlendCount(brush, NULL);
    expect(InvalidParameter, status);

    status = GdipGetLinePresetBlendCount(brush, &count);
    expect(Ok, status);
    expect(0, count);

    status = GdipGetLinePresetBlend(NULL, res_colors, res_positions, 1);
    expect(InvalidParameter, status);

    status = GdipGetLinePresetBlend(brush, NULL, res_positions, 1);
    expect(InvalidParameter, status);

    status = GdipGetLinePresetBlend(brush, res_colors, NULL, 1);
    expect(InvalidParameter, status);

    status = GdipGetLinePresetBlend(brush, res_colors, res_positions, 0);
    expect(InvalidParameter, status);

    status = GdipGetLinePresetBlend(brush, res_colors, res_positions, -1);
    expect(InvalidParameter, status);

    status = GdipGetLinePresetBlend(brush, res_colors, res_positions, 1);
    expect(InvalidParameter, status);

    status = GdipGetLinePresetBlend(brush, res_colors, res_positions, 2);
    expect(GenericError, status);

    status = GdipSetLinePresetBlend(NULL, colors, positions, 5);
    expect(InvalidParameter, status);

    status = GdipSetLinePresetBlend(brush, NULL, positions, 5);
    expect(InvalidParameter, status);

    status = GdipSetLinePresetBlend(brush, colors, NULL, 5);
    expect(InvalidParameter, status);

    status = GdipSetLinePresetBlend(brush, colors, positions, 0);
    expect(InvalidParameter, status);

    status = GdipSetLinePresetBlend(brush, colors, positions, -1);
    expect(InvalidParameter, status);

    status = GdipSetLinePresetBlend(brush, colors, positions, 1);
    expect(InvalidParameter, status);

    /* leave off the 0.0 position */
    status = GdipSetLinePresetBlend(brush, &colors[1], &positions[1], 4);
    expect(InvalidParameter, status);

    /* leave off the 1.0 position */
    status = GdipSetLinePresetBlend(brush, colors, positions, 4);
    expect(InvalidParameter, status);

    status = GdipSetLinePresetBlend(brush, colors, positions, 5);
    expect(Ok, status);

    status = GdipGetLinePresetBlendCount(brush, &count);
    expect(Ok, status);
    expect(5, count);

    status = GdipGetLinePresetBlend(brush, res_colors, res_positions, 4);
    expect(InsufficientBuffer, status);

    status = GdipGetLinePresetBlend(brush, res_colors, res_positions, 5);
    expect(Ok, status);

    for (i=0; i<5; i++)
    {
        expect(colors[i], res_colors[i]);
        expectf(positions[i], res_positions[i]);
    }

    status = GdipGetLinePresetBlend(brush, res_colors, res_positions, 6);
    expect(Ok, status);

    status = GdipSetLinePresetBlend(brush, colors, two_positions, 2);
    expect(Ok, status);

    status = GdipDeleteBrush((GpBrush*)brush);
    expect(Ok, status);
}

static void test_linelinearblend(void)
{
    GpLineGradient *brush;
    GpStatus status;
    GpPointF pt1, pt2;
    INT count=10;
    REAL res_factors[3] = {0.3f};
    REAL res_positions[3] = {0.3f};

    status = GdipSetLineLinearBlend(NULL, 0.6, 0.8);
    expect(InvalidParameter, status);

    pt1.X = pt1.Y = 1.0;
    pt2.X = pt2.Y = 100.0;
    status = GdipCreateLineBrush(&pt1, &pt2, 0, 0, WrapModeTile, &brush);
    expect(Ok, status);


    status = GdipSetLineLinearBlend(brush, 0.6, 0.8);
    expect(Ok, status);

    status = GdipGetLineBlendCount(brush, &count);
    expect(Ok, status);
    expect(3, count);

    status = GdipGetLineBlend(brush, res_factors, res_positions, 3);
    expect(Ok, status);
    expectf(0.0, res_factors[0]);
    expectf(0.0, res_positions[0]);
    expectf(0.8, res_factors[1]);
    expectf(0.6, res_positions[1]);
    expectf(0.0, res_factors[2]);
    expectf(1.0, res_positions[2]);


    status = GdipSetLineLinearBlend(brush, 0.0, 0.8);
    expect(Ok, status);

    status = GdipGetLineBlendCount(brush, &count);
    expect(Ok, status);
    expect(2, count);

    status = GdipGetLineBlend(brush, res_factors, res_positions, 3);
    expect(Ok, status);
    expectf(0.8, res_factors[0]);
    expectf(0.0, res_positions[0]);
    expectf(0.0, res_factors[1]);
    expectf(1.0, res_positions[1]);


    status = GdipSetLineLinearBlend(brush, 1.0, 0.8);
    expect(Ok, status);

    status = GdipGetLineBlendCount(brush, &count);
    expect(Ok, status);
    expect(2, count);

    status = GdipGetLineBlend(brush, res_factors, res_positions, 3);
    expect(Ok, status);
    expectf(0.0, res_factors[0]);
    expectf(0.0, res_positions[0]);
    expectf(0.8, res_factors[1]);
    expectf(1.0, res_positions[1]);

    status = GdipDeleteBrush((GpBrush*)brush);
    expect(Ok, status);
}

static void test_gradientsurroundcolorcount(void)
{
    GpStatus status;
    GpPathGradient *grad;
    ARGB *color;
    INT count = 3;

    status = GdipCreatePathGradient(blendcount_ptf, 2, WrapModeClamp, &grad);
    expect(Ok, status);

    color = GdipAlloc(sizeof(ARGB[3]));

    status = GdipSetPathGradientSurroundColorsWithCount(grad, color, &count);
    expect(InvalidParameter, status);
    GdipFree(color);

    count = 2;

    color = GdipAlloc(sizeof(ARGB[2]));

    color[0] = 0x00ff0000;
    color[1] = 0x0000ff00;

    status = GdipSetPathGradientSurroundColorsWithCount(NULL, color, &count);
    expect(InvalidParameter, status);

    status = GdipSetPathGradientSurroundColorsWithCount(grad, NULL, &count);
    expect(InvalidParameter, status);

    /* WinXP crashes on this test */
    if(0)
    {
        status = GdipSetPathGradientSurroundColorsWithCount(grad, color, NULL);
        expect(InvalidParameter, status);
    }

    status = GdipSetPathGradientSurroundColorsWithCount(grad, color, &count);
    todo_wine expect(Ok, status);
    expect(2, count);

    status = GdipGetPathGradientSurroundColorCount(NULL, &count);
    expect(InvalidParameter, status);

    status = GdipGetPathGradientSurroundColorCount(grad, NULL);
    expect(InvalidParameter, status);

    count = 0;
    status = GdipGetPathGradientSurroundColorCount(grad, &count);
    todo_wine expect(Ok, status);
    todo_wine expect(2, count);

    GdipFree(color);
    GdipDeleteBrush((GpBrush*)grad);
}

START_TEST(brush)
{
    struct GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;

    gdiplusStartupInput.GdiplusVersion              = 1;
    gdiplusStartupInput.DebugEventCallback          = NULL;
    gdiplusStartupInput.SuppressBackgroundThread    = 0;
    gdiplusStartupInput.SuppressExternalCodecs      = 0;

    GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

    test_constructor_destructor();
    test_type();
    test_gradientblendcount();
    test_getblend();
    test_getbounds();
    test_getgamma();
    test_transform();
    test_texturewrap();
    test_gradientgetrect();
    test_lineblend();
    test_linelinearblend();
    test_gradientsurroundcolorcount();

    GdiplusShutdown(gdiplusToken);
}
