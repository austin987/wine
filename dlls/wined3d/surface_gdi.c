/*
 * 2D Surface implementation without OpenGL
 *
 * Copyright 1997-2000 Marcus Meissner
 * Copyright 1998-2000 Lionel Ulmer
 * Copyright 2000-2001 TransGaming Technologies Inc.
 * Copyright 2002-2005 Jason Edmeades
 * Copyright 2002-2003 Raphael Junqueira
 * Copyright 2004 Christian Costa
 * Copyright 2005 Oliver Stieber
 * Copyright 2006-2008 Stefan Dösinger
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

#include "config.h"
#include "wine/port.h"
#include "wined3d_private.h"

#include <stdio.h>

/* Use the d3d_surface debug channel to have one channel for all surfaces */
WINE_DEFAULT_DEBUG_CHANNEL(d3d_surface);

void surface_gdi_cleanup(IWineD3DSurfaceImpl *This)
{
    TRACE("(%p) : Cleaning up.\n", This);

    if (This->Flags & SFLAG_DIBSECTION)
    {
        /* Release the DC. */
        SelectObject(This->hDC, This->dib.holdbitmap);
        DeleteDC(This->hDC);
        /* Release the DIB section. */
        DeleteObject(This->dib.DIBsection);
        This->dib.bitmap_data = NULL;
        This->resource.allocatedMemory = NULL;
    }

    if (This->Flags & SFLAG_USERPTR) IWineD3DSurface_SetMem((IWineD3DSurface *)This, NULL);
    if (This->overlay_dest) list_remove(&This->overlay_entry);

    HeapFree(GetProcessHeap(), 0, This->palette9);

    resource_cleanup((IWineD3DResource *)This);
}

/*****************************************************************************
 * IWineD3DSurface::Release, GDI version
 *
 * In general a normal COM Release method, but the GDI version doesn't have
 * to destroy all the GL things.
 *
 *****************************************************************************/
static ULONG WINAPI IWineGDISurfaceImpl_Release(IWineD3DSurface *iface) {
    IWineD3DSurfaceImpl *This = (IWineD3DSurfaceImpl *)iface;
    ULONG ref = InterlockedDecrement(&This->resource.ref);
    TRACE("(%p) : Releasing from %d\n", This, ref + 1);

    if (!ref)
    {
        surface_gdi_cleanup(This);

        TRACE("(%p) Released.\n", This);
        HeapFree(GetProcessHeap(), 0, This);
    }

    return ref;
}

/*****************************************************************************
 * IWineD3DSurface::PreLoad, GDI version
 *
 * This call is unsupported on GDI surfaces, if it's called something went
 * wrong in the parent library. Write an informative warning
 *
 *****************************************************************************/
static void WINAPI
IWineGDISurfaceImpl_PreLoad(IWineD3DSurface *iface)
{
    ERR("(%p): PreLoad is not supported on X11 surfaces!\n", iface);
    ERR("(%p): Most likely the parent library did something wrong.\n", iface);
    ERR("(%p): Please report to wine-devel\n", iface);
}

/*****************************************************************************
 * IWineD3DSurface::UnLoad, GDI version
 *
 * This call is unsupported on GDI surfaces, if it's called something went
 * wrong in the parent library. Write an informative warning.
 *
 *****************************************************************************/
static void WINAPI IWineGDISurfaceImpl_UnLoad(IWineD3DSurface *iface)
{
    ERR("(%p): UnLoad is not supported on X11 surfaces!\n", iface);
    ERR("(%p): Most likely the parent library did something wrong.\n", iface);
    ERR("(%p): Please report to wine-devel\n", iface);
}

static HRESULT WINAPI IWineGDISurfaceImpl_Map(IWineD3DSurface *iface,
        WINED3DLOCKED_RECT *pLockedRect, const RECT *pRect, DWORD Flags)
{
    IWineD3DSurfaceImpl *This = (IWineD3DSurfaceImpl *)iface;

    /* Already locked? */
    if(This->Flags & SFLAG_LOCKED)
    {
        WARN("(%p) Surface already locked\n", This);
        /* What should I return here? */
        return WINED3DERR_INVALIDCALL;
    }
    This->Flags |= SFLAG_LOCKED;

    if(!This->resource.allocatedMemory) {
        /* This happens on gdi surfaces if the application set a user pointer and resets it.
         * Recreate the DIB section
         */
        IWineD3DBaseSurfaceImpl_CreateDIBSection(iface);
        This->resource.allocatedMemory = This->dib.bitmap_data;
    }

    return IWineD3DBaseSurfaceImpl_Map(iface, pLockedRect, pRect, Flags);
}

static HRESULT WINAPI IWineGDISurfaceImpl_Unmap(IWineD3DSurface *iface)
{
    IWineD3DSurfaceImpl *This = (IWineD3DSurfaceImpl *)iface;
    TRACE("(%p)\n", This);

    if (!(This->Flags & SFLAG_LOCKED))
    {
        WARN("Trying to unmap unmapped surfaces %p.\n", iface);
        return WINEDDERR_NOTLOCKED;
    }

    /* Tell the swapchain to update the screen */
    if (This->container.type == WINED3D_CONTAINER_SWAPCHAIN)
    {
        IWineD3DSwapChainImpl *swapchain = This->container.u.swapchain;
        if (This == swapchain->front_buffer)
        {
            x11_copy_to_screen(swapchain, &This->lockedRect);
        }
    }

    This->Flags &= ~SFLAG_LOCKED;
    memset(&This->lockedRect, 0, sizeof(RECT));
    return WINED3D_OK;
}

/*****************************************************************************
 * IWineD3DSurface::Flip, GDI version
 *
 * Flips 2 flipping enabled surfaces. Determining the 2 targets is done by
 * the parent library. This implementation changes the data pointers of the
 * surfaces and copies the new front buffer content to the screen
 *
 * Params:
 *  override: Flipping target(e.g. back buffer)
 *
 * Returns:
 *  WINED3D_OK on success
 *
 *****************************************************************************/
static HRESULT WINAPI
IWineGDISurfaceImpl_Flip(IWineD3DSurface *iface,
                         IWineD3DSurface *override,
                         DWORD Flags)
{
    IWineD3DSurfaceImpl *surface = (IWineD3DSurfaceImpl *)iface;
    IWineD3DSwapChainImpl *swapchain;
    HRESULT hr;

    if (surface->container.type != WINED3D_CONTAINER_SWAPCHAIN)
    {
        ERR("Flipped surface is not on a swapchain\n");
        return WINEDDERR_NOTFLIPPABLE;
    }

    swapchain = surface->container.u.swapchain;
    hr = IWineD3DSwapChain_Present((IWineD3DSwapChain *)swapchain,
            NULL, NULL, swapchain->win_handle, NULL, 0);

    return hr;
}

/*****************************************************************************
 * IWineD3DSurface::LoadTexture, GDI version
 *
 * This is mutually unsupported by GDI surfaces
 *
 * Returns:
 *  D3DERR_INVALIDCALL
 *
 *****************************************************************************/
static HRESULT WINAPI
IWineGDISurfaceImpl_LoadTexture(IWineD3DSurface *iface, BOOL srgb_mode)
{
    ERR("Unsupported on X11 surfaces\n");
    return WINED3DERR_INVALIDCALL;
}

static void WINAPI IWineGDISurfaceImpl_BindTexture(IWineD3DSurface *iface, BOOL srgb)
{
    ERR("Not supported.\n");
}

static HRESULT WINAPI IWineGDISurfaceImpl_GetDC(IWineD3DSurface *iface, HDC *pHDC) {
    IWineD3DSurfaceImpl *This = (IWineD3DSurfaceImpl *)iface;
    WINED3DLOCKED_RECT lock;
    HRESULT hr;
    RGBQUAD col[256];

    TRACE("(%p)->(%p)\n",This,pHDC);

    if(!(This->Flags & SFLAG_DIBSECTION))
    {
        WARN("DC not supported on this surface\n");
        return WINED3DERR_INVALIDCALL;
    }

    if(This->Flags & SFLAG_USERPTR) {
        ERR("Not supported on surfaces with an application-provided surfaces\n");
        return WINEDDERR_NODC;
    }

    /* Give more detailed info for ddraw */
    if (This->Flags & SFLAG_DCINUSE)
        return WINEDDERR_DCALREADYCREATED;

    /* Can't GetDC if the surface is locked */
    if (This->Flags & SFLAG_LOCKED)
        return WINED3DERR_INVALIDCALL;

    memset(&lock, 0, sizeof(lock)); /* To be sure */

    /* Should have a DIB section already */

    /* Map the surface. */
    hr = IWineD3DSurface_Map(iface, &lock, NULL, 0);
    if (FAILED(hr))
    {
        ERR("IWineD3DSurface_Map failed, hr %#x.\n", hr);
        /* keep the dib section */
        return hr;
    }

    if (This->resource.format->id == WINED3DFMT_P8_UINT
            || This->resource.format->id == WINED3DFMT_P8_UINT_A8_UNORM)
    {
        unsigned int n;
        const PALETTEENTRY *pal = NULL;

        if(This->palette) {
            pal = This->palette->palents;
        } else {
            IWineD3DSurfaceImpl *dds_primary;
            IWineD3DSwapChainImpl *swapchain;
            swapchain = (IWineD3DSwapChainImpl *)This->resource.device->swapchains[0];
            dds_primary = swapchain->front_buffer;
            if (dds_primary && dds_primary->palette)
                pal = dds_primary->palette->palents;
        }

        if (pal) {
            for (n=0; n<256; n++) {
                col[n].rgbRed   = pal[n].peRed;
                col[n].rgbGreen = pal[n].peGreen;
                col[n].rgbBlue  = pal[n].peBlue;
                col[n].rgbReserved = 0;
            }
            SetDIBColorTable(This->hDC, 0, 256, col);
        }
    }

    *pHDC = This->hDC;
    TRACE("returning %p\n",*pHDC);
    This->Flags |= SFLAG_DCINUSE;

    return WINED3D_OK;
}

static HRESULT WINAPI IWineGDISurfaceImpl_ReleaseDC(IWineD3DSurface *iface, HDC hDC) {
    IWineD3DSurfaceImpl *This = (IWineD3DSurfaceImpl *)iface;

    TRACE("(%p)->(%p)\n",This,hDC);

    if (!(This->Flags & SFLAG_DCINUSE))
        return WINEDDERR_NODC;

    if (This->hDC !=hDC) {
        WARN("Application tries to release an invalid DC(%p), surface dc is %p\n", hDC, This->hDC);
        return WINEDDERR_NODC;
    }

    /* we locked first, so unlock now */
    IWineD3DSurface_Unmap(iface);

    This->Flags &= ~SFLAG_DCINUSE;

    return WINED3D_OK;
}

static HRESULT WINAPI IWineGDISurfaceImpl_RealizePalette(IWineD3DSurface *iface) {
    IWineD3DSurfaceImpl *This = (IWineD3DSurfaceImpl *) iface;
    RGBQUAD col[256];
    IWineD3DPaletteImpl *pal = This->palette;
    unsigned int n;
    TRACE("(%p)\n", This);

    if (!pal) return WINED3D_OK;

    if(This->Flags & SFLAG_DIBSECTION) {
        TRACE("(%p): Updating the hdc's palette\n", This);
        for (n=0; n<256; n++) {
            col[n].rgbRed   = pal->palents[n].peRed;
            col[n].rgbGreen = pal->palents[n].peGreen;
            col[n].rgbBlue  = pal->palents[n].peBlue;
            col[n].rgbReserved = 0;
        }
        SetDIBColorTable(This->hDC, 0, 256, col);
    }

    /* Update the image because of the palette change. Some games like e.g Red Alert
       call SetEntries a lot to implement fading. */
    /* Tell the swapchain to update the screen */
    if (This->container.type == WINED3D_CONTAINER_SWAPCHAIN)
    {
        IWineD3DSwapChainImpl *swapchain = This->container.u.swapchain;
        if (This == swapchain->front_buffer)
        {
            x11_copy_to_screen(swapchain, NULL);
        }
    }

    return WINED3D_OK;
}

/*****************************************************************************
 * IWineD3DSurface::PrivateSetup, GDI version
 *
 * Initializes the GDI surface, aka creates the DIB section we render to
 * The DIB section creation is done by calling GetDC, which will create the
 * section and releasing the dc to allow the app to use it. The dib section
 * will stay until the surface is released
 *
 * GDI surfaces do not need to be a power of 2 in size, so the pow2 sizes
 * are set to the real sizes to save memory. The NONPOW2 flag is unset to
 * avoid confusion in the shared surface code.
 *
 * Returns:
 *  WINED3D_OK on success
 *  The return values of called methods on failure
 *
 *****************************************************************************/
static HRESULT WINAPI
IWineGDISurfaceImpl_PrivateSetup(IWineD3DSurface *iface)
{
    IWineD3DSurfaceImpl *This = (IWineD3DSurfaceImpl *) iface;
    HRESULT hr;

    if(This->resource.usage & WINED3DUSAGE_OVERLAY)
    {
        ERR("(%p) Overlays not yet supported by GDI surfaces\n", This);
        return WINED3DERR_INVALIDCALL;
    }

    /* Sysmem textures have memory already allocated -
     * release it, this avoids an unnecessary memcpy
     */
    hr = IWineD3DBaseSurfaceImpl_CreateDIBSection(iface);
    if(SUCCEEDED(hr))
    {
        HeapFree(GetProcessHeap(), 0, This->resource.heapMemory);
        This->resource.heapMemory = NULL;
        This->resource.allocatedMemory = This->dib.bitmap_data;
    }

    /* We don't mind the nonpow2 stuff in GDI */
    This->pow2Width = This->currentDesc.Width;
    This->pow2Height = This->currentDesc.Height;

    return WINED3D_OK;
}

static HRESULT WINAPI IWineGDISurfaceImpl_SetMem(IWineD3DSurface *iface, void *Mem) {
    IWineD3DSurfaceImpl *This = (IWineD3DSurfaceImpl *) iface;

    /* Render targets depend on their hdc, and we can't create an hdc on a user pointer */
    if(This->resource.usage & WINED3DUSAGE_RENDERTARGET) {
        ERR("Not supported on render targets\n");
        return WINED3DERR_INVALIDCALL;
    }

    if(This->Flags & (SFLAG_LOCKED | SFLAG_DCINUSE)) {
        WARN("Surface is locked or the HDC is in use\n");
        return WINED3DERR_INVALIDCALL;
    }

    if(Mem && Mem != This->resource.allocatedMemory) {
        void *release = NULL;

        /* Do I have to copy the old surface content? */
        if(This->Flags & SFLAG_DIBSECTION) {
                /* Release the DC. No need to hold the critical section for the update
            * Thread because this thread runs only on front buffers, but this method
            * fails for render targets in the check above.
                */
            SelectObject(This->hDC, This->dib.holdbitmap);
            DeleteDC(This->hDC);
            /* Release the DIB section */
            DeleteObject(This->dib.DIBsection);
            This->dib.bitmap_data = NULL;
            This->resource.allocatedMemory = NULL;
            This->hDC = NULL;
            This->Flags &= ~SFLAG_DIBSECTION;
        } else if(!(This->Flags & SFLAG_USERPTR)) {
            release = This->resource.allocatedMemory;
        }
        This->resource.allocatedMemory = Mem;
        This->Flags |= SFLAG_USERPTR | SFLAG_INSYSMEM;

        /* Now free the old memory if any */
        HeapFree(GetProcessHeap(), 0, release);
    }
    else if(This->Flags & SFLAG_USERPTR)
    {
        /* Map() and GetDC() will re-create the dib section and allocated memory. */
        This->resource.allocatedMemory = NULL;
        This->Flags &= ~SFLAG_USERPTR;
    }
    return WINED3D_OK;
}

static WINED3DSURFTYPE WINAPI IWineGDISurfaceImpl_GetImplType(IWineD3DSurface *iface) {
    return SURFACE_GDI;
}

static HRESULT WINAPI IWineGDISurfaceImpl_DrawOverlay(IWineD3DSurface *iface) {
    FIXME("GDI surfaces can't draw overlays yet\n");
    return E_FAIL;
}

/* FIXME: This vtable should not use any IWineD3DSurface* implementation functions,
 * only IWineD3DBaseSurface and IWineGDISurface ones.
 */
const IWineD3DSurfaceVtbl IWineGDISurface_Vtbl =
{
    /* IUnknown */
    IWineD3DBaseSurfaceImpl_QueryInterface,
    IWineD3DBaseSurfaceImpl_AddRef,
    IWineGDISurfaceImpl_Release,
    /* IWineD3DResource */
    IWineD3DBaseSurfaceImpl_GetParent,
    IWineD3DBaseSurfaceImpl_SetPrivateData,
    IWineD3DBaseSurfaceImpl_GetPrivateData,
    IWineD3DBaseSurfaceImpl_FreePrivateData,
    IWineD3DBaseSurfaceImpl_SetPriority,
    IWineD3DBaseSurfaceImpl_GetPriority,
    IWineGDISurfaceImpl_PreLoad,
    IWineGDISurfaceImpl_UnLoad,
    IWineD3DBaseSurfaceImpl_GetType,
    /* IWineD3DSurface */
    IWineD3DBaseSurfaceImpl_GetDesc,
    IWineGDISurfaceImpl_Map,
    IWineGDISurfaceImpl_Unmap,
    IWineGDISurfaceImpl_GetDC,
    IWineGDISurfaceImpl_ReleaseDC,
    IWineGDISurfaceImpl_Flip,
    IWineD3DBaseSurfaceImpl_Blt,
    IWineD3DBaseSurfaceImpl_GetBltStatus,
    IWineD3DBaseSurfaceImpl_GetFlipStatus,
    IWineD3DBaseSurfaceImpl_IsLost,
    IWineD3DBaseSurfaceImpl_Restore,
    IWineD3DBaseSurfaceImpl_BltFast,
    IWineD3DBaseSurfaceImpl_GetPalette,
    IWineD3DBaseSurfaceImpl_SetPalette,
    IWineGDISurfaceImpl_RealizePalette,
    IWineD3DBaseSurfaceImpl_SetColorKey,
    IWineD3DBaseSurfaceImpl_GetPitch,
    IWineGDISurfaceImpl_SetMem,
    IWineD3DBaseSurfaceImpl_SetOverlayPosition,
    IWineD3DBaseSurfaceImpl_GetOverlayPosition,
    IWineD3DBaseSurfaceImpl_UpdateOverlayZOrder,
    IWineD3DBaseSurfaceImpl_UpdateOverlay,
    IWineD3DBaseSurfaceImpl_SetClipper,
    IWineD3DBaseSurfaceImpl_GetClipper,
    /* Internal use: */
    IWineGDISurfaceImpl_LoadTexture,
    IWineGDISurfaceImpl_BindTexture,
    IWineD3DBaseSurfaceImpl_GetData,
    IWineD3DBaseSurfaceImpl_SetFormat,
    IWineGDISurfaceImpl_PrivateSetup,
    IWineGDISurfaceImpl_GetImplType,
    IWineGDISurfaceImpl_DrawOverlay
};
