/*
 * Copyright 2005 Jacek Caban
 * Copyright 2007 Misha Koshelev
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

#include "urlmon_main.h"
#include "wininet.h"

#define NO_SHLWAPI_REG
#include "shlwapi.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(urlmon);

typedef struct {
    Protocol base;

    const IInternetProtocolExVtbl *lpIInternetProtocolExVtbl;
    const IInternetPriorityVtbl *lpInternetPriorityVtbl;
    const IWinInetHttpInfoVtbl  *lpWinInetHttpInfoVtbl;

    BOOL https;
    IHttpNegotiate *http_negotiate;
    LPWSTR full_header;

    LONG ref;
} HttpProtocol;

#define PROTOCOLEX(x)    ((IInternetProtocolEx*)&(x)->lpIInternetProtocolExVtbl)
#define PRIORITY(x)      ((IInternetPriority*)  &(x)->lpInternetPriorityVtbl)
#define INETHTTPINFO(x)  ((IWinInetHttpInfo*)   &(x)->lpWinInetHttpInfoVtbl)

/* Default headers from native */
static const WCHAR wszHeaders[] = {'A','c','c','e','p','t','-','E','n','c','o','d','i','n','g',
                                   ':',' ','g','z','i','p',',',' ','d','e','f','l','a','t','e',0};

static LPWSTR query_http_info(HttpProtocol *This, DWORD option)
{
    LPWSTR ret = NULL;
    DWORD len = 0;
    BOOL res;

    res = HttpQueryInfoW(This->base.request, option, NULL, &len, NULL);
    if (!res && GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
        ret = heap_alloc(len);
        res = HttpQueryInfoW(This->base.request, option, ret, &len, NULL);
    }
    if(!res) {
        TRACE("HttpQueryInfoW(%d) failed: %08x\n", option, GetLastError());
        heap_free(ret);
        return NULL;
    }

    return ret;
}

#define ASYNCPROTOCOL_THIS(iface) DEFINE_THIS2(HttpProtocol, base, iface)

static HRESULT HttpProtocol_open_request(Protocol *prot, IUri *uri, DWORD request_flags,
        HINTERNET internet_session, IInternetBindInfo *bind_info)
{
    HttpProtocol *This = ASYNCPROTOCOL_THIS(prot);
    INTERNET_BUFFERSW send_buffer = {sizeof(INTERNET_BUFFERSW)};
    LPWSTR addl_header = NULL, post_cookie = NULL;
    IServiceProvider *service_provider = NULL;
    IHttpNegotiate2 *http_negotiate2 = NULL;
    BSTR url, host, user, pass, path;
    LPOLESTR accept_mimes[257];
    const WCHAR **accept_types;
    BYTE security_id[512];
    DWORD len = 0, port;
    ULONG num;
    BOOL res, b;
    HRESULT hres;

    static const WCHAR wszBindVerb[BINDVERB_CUSTOM][5] =
        {{'G','E','T',0},
         {'P','O','S','T',0},
         {'P','U','T',0}};

    hres = IUri_GetPort(uri, &port);
    if(FAILED(hres))
        return hres;

    hres = IUri_GetHost(uri, &host);
    if(FAILED(hres))
        return hres;

    hres = IUri_GetUserName(uri, &user);
    if(SUCCEEDED(hres)) {
        hres = IUri_GetPassword(uri, &pass);

        if(SUCCEEDED(hres)) {
            This->base.connection = InternetConnectW(internet_session, host, port, user, pass,
                    INTERNET_SERVICE_HTTP, This->https ? INTERNET_FLAG_SECURE : 0, (DWORD_PTR)&This->base);
            SysFreeString(pass);
        }
        SysFreeString(user);
    }
    SysFreeString(host);
    if(FAILED(hres))
        return hres;
    if(!This->base.connection) {
        WARN("InternetConnect failed: %d\n", GetLastError());
        return INET_E_CANNOT_CONNECT;
    }

    num = sizeof(accept_mimes)/sizeof(accept_mimes[0])-1;
    hres = IInternetBindInfo_GetBindString(bind_info, BINDSTRING_ACCEPT_MIMES, accept_mimes, num, &num);
    if(hres == INET_E_USE_DEFAULT_SETTING) {
        static const WCHAR default_accept_mimeW[] = {'*','/','*',0};
        static const WCHAR *default_accept_mimes[] = {default_accept_mimeW, NULL};

        accept_types = default_accept_mimes;
        num = 0;
    }else if(hres == S_OK) {
        accept_types = (const WCHAR**)accept_mimes;
    }else {
        WARN("GetBindString BINDSTRING_ACCEPT_MIMES failed: %08x\n", hres);
        return INET_E_NO_VALID_MEDIA;
    }
    accept_mimes[num] = 0;

    if(This->https)
        request_flags |= INTERNET_FLAG_SECURE;

    hres = IUri_GetPathAndQuery(uri, &path);
    if(SUCCEEDED(hres)) {
        This->base.request = HttpOpenRequestW(This->base.connection,
                This->base.bind_info.dwBindVerb < BINDVERB_CUSTOM
                    ? wszBindVerb[This->base.bind_info.dwBindVerb] : This->base.bind_info.szCustomVerb,
                path, NULL, NULL, accept_types, request_flags, (DWORD_PTR)&This->base);
        SysFreeString(path);
    }
    while(num--)
        CoTaskMemFree(accept_mimes[num]);
    if(FAILED(hres))
        return hres;
    if (!This->base.request) {
        WARN("HttpOpenRequest failed: %d\n", GetLastError());
        return INET_E_RESOURCE_NOT_FOUND;
    }

    hres = IInternetProtocolSink_QueryInterface(This->base.protocol_sink, &IID_IServiceProvider,
            (void **)&service_provider);
    if (hres != S_OK) {
        WARN("IInternetProtocolSink_QueryInterface IID_IServiceProvider failed: %08x\n", hres);
        return hres;
    }

    hres = IServiceProvider_QueryService(service_provider, &IID_IHttpNegotiate,
            &IID_IHttpNegotiate, (void **)&This->http_negotiate);
    if (hres != S_OK) {
        WARN("IServiceProvider_QueryService IID_IHttpNegotiate failed: %08x\n", hres);
        IServiceProvider_Release(service_provider);
        return hres;
    }

    hres = IUri_GetAbsoluteUri(uri, &url);
    if(FAILED(hres)) {
        IServiceProvider_Release(service_provider);
        return hres;
    }

    hres = IHttpNegotiate_BeginningTransaction(This->http_negotiate, url, wszHeaders,
            0, &addl_header);
    SysFreeString(url);
    if(hres != S_OK) {
        WARN("IHttpNegotiate_BeginningTransaction failed: %08x\n", hres);
        IServiceProvider_Release(service_provider);
        return hres;
    }

    if(addl_header) {
        int len_addl_header = strlenW(addl_header);

        This->full_header = heap_alloc(len_addl_header*sizeof(WCHAR)+sizeof(wszHeaders));

        lstrcpyW(This->full_header, addl_header);
        lstrcpyW(&This->full_header[len_addl_header], wszHeaders);
        CoTaskMemFree(addl_header);
    }else {
        This->full_header = (LPWSTR)wszHeaders;
    }

    hres = IServiceProvider_QueryService(service_provider, &IID_IHttpNegotiate2,
            &IID_IHttpNegotiate2, (void **)&http_negotiate2);
    IServiceProvider_Release(service_provider);
    if(hres != S_OK) {
        WARN("IServiceProvider_QueryService IID_IHttpNegotiate2 failed: %08x\n", hres);
        /* No goto done as per native */
    }else {
        len = sizeof(security_id)/sizeof(security_id[0]);
        hres = IHttpNegotiate2_GetRootSecurityId(http_negotiate2, security_id, &len, 0);
        IHttpNegotiate2_Release(http_negotiate2);
        if (hres != S_OK)
            WARN("IHttpNegotiate2_GetRootSecurityId failed: %08x\n", hres);
    }

    /* FIXME: Handle security_id. Native calls undocumented function IsHostInProxyBypassList. */

    if(This->base.bind_info.dwBindVerb == BINDVERB_POST) {
        num = 0;
        hres = IInternetBindInfo_GetBindString(bind_info, BINDSTRING_POST_COOKIE, &post_cookie, 1, &num);
        if(hres == S_OK && num) {
            if(!InternetSetOptionW(This->base.request, INTERNET_OPTION_SECONDARY_CACHE_KEY,
                                   post_cookie, lstrlenW(post_cookie)))
                WARN("InternetSetOption INTERNET_OPTION_SECONDARY_CACHE_KEY failed: %d\n", GetLastError());
            CoTaskMemFree(post_cookie);
        }
    }

    send_buffer.lpcszHeader = This->full_header;
    send_buffer.dwHeadersLength = send_buffer.dwHeadersTotal = strlenW(This->full_header);

    if(This->base.bind_info.dwBindVerb != BINDVERB_GET) {
        switch(This->base.bind_info.stgmedData.tymed) {
        case TYMED_HGLOBAL:
            /* Native does not use GlobalLock/GlobalUnlock, so we won't either */
            send_buffer.lpvBuffer = This->base.bind_info.stgmedData.u.hGlobal;
            send_buffer.dwBufferLength = send_buffer.dwBufferTotal = This->base.bind_info.cbstgmedData;
            break;
        case TYMED_ISTREAM: {
            LARGE_INTEGER offset;

            send_buffer.dwBufferTotal = This->base.bind_info.cbstgmedData;
            This->base.post_stream = This->base.bind_info.stgmedData.u.pstm;
            IStream_AddRef(This->base.post_stream);

            offset.QuadPart = 0;
            IStream_Seek(This->base.post_stream, offset, STREAM_SEEK_SET, NULL);
            break;
        }
        default:
            FIXME("Unsupported This->base.bind_info.stgmedData.tymed %d\n", This->base.bind_info.stgmedData.tymed);
        }
    }

    b = TRUE;
    res = InternetSetOptionW(This->base.request, INTERNET_OPTION_HTTP_DECODING, &b, sizeof(b));
    if(!res)
        WARN("InternetSetOption(INTERNET_OPTION_HTTP_DECODING) failed: %08x\n", GetLastError());

    if(This->base.post_stream)
        res = HttpSendRequestExW(This->base.request, &send_buffer, NULL, 0, 0);
    else
        res = HttpSendRequestW(This->base.request, send_buffer.lpcszHeader, send_buffer.dwHeadersLength,
                send_buffer.lpvBuffer, send_buffer.dwBufferLength);
    if(!res && GetLastError() != ERROR_IO_PENDING) {
        WARN("HttpSendRequest failed: %d\n", GetLastError());
        return INET_E_DOWNLOAD_FAILURE;
    }

    return S_OK;
}

static HRESULT HttpProtocol_end_request(Protocol *protocol)
{
    BOOL res;

    res = HttpEndRequestW(protocol->request, NULL, 0, 0);
    if(!res && GetLastError() != ERROR_IO_PENDING) {
        FIXME("HttpEndRequest failed: %u\n", GetLastError());
        return E_FAIL;
    }

    return S_OK;
}

static HRESULT HttpProtocol_start_downloading(Protocol *prot)
{
    HttpProtocol *This = ASYNCPROTOCOL_THIS(prot);
    LPWSTR content_type, content_length, ranges;
    DWORD len = sizeof(DWORD);
    DWORD status_code;
    BOOL res;
    HRESULT hres;

    static const WCHAR wszDefaultContentType[] =
        {'t','e','x','t','/','h','t','m','l',0};

    if(!This->http_negotiate) {
        WARN("Expected IHttpNegotiate pointer to be non-NULL\n");
        return S_OK;
    }

    res = HttpQueryInfoW(This->base.request, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER,
            &status_code, &len, NULL);
    if(res) {
        LPWSTR response_headers = query_http_info(This, HTTP_QUERY_RAW_HEADERS_CRLF);
        if(response_headers) {
            hres = IHttpNegotiate_OnResponse(This->http_negotiate, status_code, response_headers,
                    NULL, NULL);
            heap_free(response_headers);
            if (hres != S_OK) {
                WARN("IHttpNegotiate_OnResponse failed: %08x\n", hres);
                return S_OK;
            }
        }
    }else {
        WARN("HttpQueryInfo failed: %d\n", GetLastError());
    }

    ranges = query_http_info(This, HTTP_QUERY_ACCEPT_RANGES);
    if(ranges) {
        IInternetProtocolSink_ReportProgress(This->base.protocol_sink, BINDSTATUS_ACCEPTRANGES, NULL);
        heap_free(ranges);
    }

    content_type = query_http_info(This, HTTP_QUERY_CONTENT_TYPE);
    if(content_type) {
        /* remove the charset, if present */
        LPWSTR p = strchrW(content_type, ';');
        if (p) *p = '\0';

        IInternetProtocolSink_ReportProgress(This->base.protocol_sink,
                (This->base.bindf & BINDF_FROMURLMON)
                 ? BINDSTATUS_MIMETYPEAVAILABLE : BINDSTATUS_RAWMIMETYPE,
                 content_type);
        heap_free(content_type);
    }else {
        WARN("HttpQueryInfo failed: %d\n", GetLastError());
        IInternetProtocolSink_ReportProgress(This->base.protocol_sink,
                 (This->base.bindf & BINDF_FROMURLMON)
                  ? BINDSTATUS_MIMETYPEAVAILABLE : BINDSTATUS_RAWMIMETYPE,
                  wszDefaultContentType);
    }

    content_length = query_http_info(This, HTTP_QUERY_CONTENT_LENGTH);
    if(content_length) {
        This->base.content_length = atoiW(content_length);
        heap_free(content_length);
    }

    return S_OK;
}

static void HttpProtocol_close_connection(Protocol *prot)
{
    HttpProtocol *This = ASYNCPROTOCOL_THIS(prot);

    if(This->http_negotiate) {
        IHttpNegotiate_Release(This->http_negotiate);
        This->http_negotiate = 0;
    }

    if(This->full_header) {
        if(This->full_header != wszHeaders)
            heap_free(This->full_header);
        This->full_header = 0;
    }
}

#undef ASYNCPROTOCOL_THIS

static const ProtocolVtbl AsyncProtocolVtbl = {
    HttpProtocol_open_request,
    HttpProtocol_end_request,
    HttpProtocol_start_downloading,
    HttpProtocol_close_connection
};

#define PROTOCOL_THIS(iface) DEFINE_THIS(HttpProtocol, IInternetProtocolEx, iface)

static HRESULT WINAPI HttpProtocol_QueryInterface(IInternetProtocolEx *iface, REFIID riid, void **ppv)
{
    HttpProtocol *This = PROTOCOL_THIS(iface);

    *ppv = NULL;
    if(IsEqualGUID(&IID_IUnknown, riid)) {
        TRACE("(%p)->(IID_IUnknown %p)\n", This, ppv);
        *ppv = PROTOCOLEX(This);
    }else if(IsEqualGUID(&IID_IInternetProtocolRoot, riid)) {
        TRACE("(%p)->(IID_IInternetProtocolRoot %p)\n", This, ppv);
        *ppv = PROTOCOLEX(This);
    }else if(IsEqualGUID(&IID_IInternetProtocol, riid)) {
        TRACE("(%p)->(IID_IInternetProtocol %p)\n", This, ppv);
        *ppv = PROTOCOLEX(This);
    }else if(IsEqualGUID(&IID_IInternetProtocolEx, riid)) {
        TRACE("(%p)->(IID_IInternetProtocolEx %p)\n", This, ppv);
        *ppv = PROTOCOLEX(This);
    }else if(IsEqualGUID(&IID_IInternetPriority, riid)) {
        TRACE("(%p)->(IID_IInternetPriority %p)\n", This, ppv);
        *ppv = PRIORITY(This);
    }else if(IsEqualGUID(&IID_IWinInetInfo, riid)) {
        TRACE("(%p)->(IID_IWinInetInfo %p)\n", This, ppv);
        *ppv = INETHTTPINFO(This);
    }else if(IsEqualGUID(&IID_IWinInetHttpInfo, riid)) {
        TRACE("(%p)->(IID_IWinInetHttpInfo %p)\n", This, ppv);
        *ppv = INETHTTPINFO(This);
    }

    if(*ppv) {
        IInternetProtocol_AddRef(iface);
        return S_OK;
    }

    WARN("not supported interface %s\n", debugstr_guid(riid));
    return E_NOINTERFACE;
}

static ULONG WINAPI HttpProtocol_AddRef(IInternetProtocolEx *iface)
{
    HttpProtocol *This = PROTOCOL_THIS(iface);
    LONG ref = InterlockedIncrement(&This->ref);
    TRACE("(%p) ref=%d\n", This, ref);
    return ref;
}

static ULONG WINAPI HttpProtocol_Release(IInternetProtocolEx *iface)
{
    HttpProtocol *This = PROTOCOL_THIS(iface);
    LONG ref = InterlockedDecrement(&This->ref);

    TRACE("(%p) ref=%d\n", This, ref);

    if(!ref) {
        protocol_close_connection(&This->base);
        heap_free(This);

        URLMON_UnlockModule();
    }

    return ref;
}

static HRESULT WINAPI HttpProtocol_Start(IInternetProtocolEx *iface, LPCWSTR szUrl,
        IInternetProtocolSink *pOIProtSink, IInternetBindInfo *pOIBindInfo,
        DWORD grfPI, HANDLE_PTR dwReserved)
{
    HttpProtocol *This = PROTOCOL_THIS(iface);
    IUri *uri;
    HRESULT hres;

    TRACE("(%p)->(%s %p %p %08x %lx)\n", This, debugstr_w(szUrl), pOIProtSink,
            pOIBindInfo, grfPI, dwReserved);

    hres = CreateUri(szUrl, 0, 0, &uri);
    if(FAILED(hres))
        return hres;

    hres = IInternetProtocolEx_StartEx(PROTOCOLEX(This), uri, pOIProtSink, pOIBindInfo,
            grfPI, (HANDLE*)dwReserved);

    IUri_Release(uri);
    return hres;
}

static HRESULT WINAPI HttpProtocol_Continue(IInternetProtocolEx *iface, PROTOCOLDATA *pProtocolData)
{
    HttpProtocol *This = PROTOCOL_THIS(iface);

    TRACE("(%p)->(%p)\n", This, pProtocolData);

    return protocol_continue(&This->base, pProtocolData);
}

static HRESULT WINAPI HttpProtocol_Abort(IInternetProtocolEx *iface, HRESULT hrReason,
        DWORD dwOptions)
{
    HttpProtocol *This = PROTOCOL_THIS(iface);

    TRACE("(%p)->(%08x %08x)\n", This, hrReason, dwOptions);

    return protocol_abort(&This->base, hrReason);
}

static HRESULT WINAPI HttpProtocol_Terminate(IInternetProtocolEx *iface, DWORD dwOptions)
{
    HttpProtocol *This = PROTOCOL_THIS(iface);

    TRACE("(%p)->(%08x)\n", This, dwOptions);

    protocol_close_connection(&This->base);
    return S_OK;
}

static HRESULT WINAPI HttpProtocol_Suspend(IInternetProtocolEx *iface)
{
    HttpProtocol *This = PROTOCOL_THIS(iface);
    FIXME("(%p)\n", This);
    return E_NOTIMPL;
}

static HRESULT WINAPI HttpProtocol_Resume(IInternetProtocolEx *iface)
{
    HttpProtocol *This = PROTOCOL_THIS(iface);
    FIXME("(%p)\n", This);
    return E_NOTIMPL;
}

static HRESULT WINAPI HttpProtocol_Read(IInternetProtocolEx *iface, void *pv,
        ULONG cb, ULONG *pcbRead)
{
    HttpProtocol *This = PROTOCOL_THIS(iface);

    TRACE("(%p)->(%p %u %p)\n", This, pv, cb, pcbRead);

    return protocol_read(&This->base, pv, cb, pcbRead);
}

static HRESULT WINAPI HttpProtocol_Seek(IInternetProtocolEx *iface, LARGE_INTEGER dlibMove,
        DWORD dwOrigin, ULARGE_INTEGER *plibNewPosition)
{
    HttpProtocol *This = PROTOCOL_THIS(iface);
    FIXME("(%p)->(%d %d %p)\n", This, dlibMove.u.LowPart, dwOrigin, plibNewPosition);
    return E_NOTIMPL;
}

static HRESULT WINAPI HttpProtocol_LockRequest(IInternetProtocolEx *iface, DWORD dwOptions)
{
    HttpProtocol *This = PROTOCOL_THIS(iface);

    TRACE("(%p)->(%08x)\n", This, dwOptions);

    return protocol_lock_request(&This->base);
}

static HRESULT WINAPI HttpProtocol_UnlockRequest(IInternetProtocolEx *iface)
{
    HttpProtocol *This = PROTOCOL_THIS(iface);

    TRACE("(%p)\n", This);

    return protocol_unlock_request(&This->base);
}

static HRESULT WINAPI HttpProtocol_StartEx(IInternetProtocolEx *iface, IUri *pUri,
        IInternetProtocolSink *pOIProtSink, IInternetBindInfo *pOIBindInfo,
        DWORD grfPI, HANDLE *dwReserved)
{
    HttpProtocol *This = PROTOCOL_THIS(iface);
    DWORD scheme = 0;
    HRESULT hres;

    TRACE("(%p)->(%p %p %p %08x %p)\n", This, pUri, pOIProtSink,
            pOIBindInfo, grfPI, dwReserved);

    hres = IUri_GetScheme(pUri, &scheme);
    if(FAILED(hres))
        return hres;
    if(scheme != (This->https ? URL_SCHEME_HTTPS : URL_SCHEME_HTTP))
        return MK_E_SYNTAX;

    return protocol_start(&This->base, (IInternetProtocol*)PROTOCOLEX(This), pUri, pOIProtSink, pOIBindInfo);
}

#undef PROTOCOL_THIS

static const IInternetProtocolExVtbl HttpProtocolVtbl = {
    HttpProtocol_QueryInterface,
    HttpProtocol_AddRef,
    HttpProtocol_Release,
    HttpProtocol_Start,
    HttpProtocol_Continue,
    HttpProtocol_Abort,
    HttpProtocol_Terminate,
    HttpProtocol_Suspend,
    HttpProtocol_Resume,
    HttpProtocol_Read,
    HttpProtocol_Seek,
    HttpProtocol_LockRequest,
    HttpProtocol_UnlockRequest,
    HttpProtocol_StartEx
};

#define PRIORITY_THIS(iface) DEFINE_THIS(HttpProtocol, InternetPriority, iface)

static HRESULT WINAPI HttpPriority_QueryInterface(IInternetPriority *iface, REFIID riid, void **ppv)
{
    HttpProtocol *This = PRIORITY_THIS(iface);
    return IInternetProtocolEx_QueryInterface(PROTOCOLEX(This), riid, ppv);
}

static ULONG WINAPI HttpPriority_AddRef(IInternetPriority *iface)
{
    HttpProtocol *This = PRIORITY_THIS(iface);
    return IInternetProtocolEx_AddRef(PROTOCOLEX(This));
}

static ULONG WINAPI HttpPriority_Release(IInternetPriority *iface)
{
    HttpProtocol *This = PRIORITY_THIS(iface);
    return IInternetProtocolEx_Release(PROTOCOLEX(This));
}

static HRESULT WINAPI HttpPriority_SetPriority(IInternetPriority *iface, LONG nPriority)
{
    HttpProtocol *This = PRIORITY_THIS(iface);

    TRACE("(%p)->(%d)\n", This, nPriority);

    This->base.priority = nPriority;
    return S_OK;
}

static HRESULT WINAPI HttpPriority_GetPriority(IInternetPriority *iface, LONG *pnPriority)
{
    HttpProtocol *This = PRIORITY_THIS(iface);

    TRACE("(%p)->(%p)\n", This, pnPriority);

    *pnPriority = This->base.priority;
    return S_OK;
}

#undef PRIORITY_THIS

static const IInternetPriorityVtbl HttpPriorityVtbl = {
    HttpPriority_QueryInterface,
    HttpPriority_AddRef,
    HttpPriority_Release,
    HttpPriority_SetPriority,
    HttpPriority_GetPriority
};

#define INETINFO_THIS(iface) DEFINE_THIS(HttpProtocol, WinInetHttpInfo, iface)

static HRESULT WINAPI HttpInfo_QueryInterface(IWinInetHttpInfo *iface, REFIID riid, void **ppv)
{
    HttpProtocol *This = INETINFO_THIS(iface);
    return IInternetProtocolEx_QueryInterface(PROTOCOLEX(This), riid, ppv);
}

static ULONG WINAPI HttpInfo_AddRef(IWinInetHttpInfo *iface)
{
    HttpProtocol *This = INETINFO_THIS(iface);
    return IInternetProtocolEx_AddRef(PROTOCOLEX(This));
}

static ULONG WINAPI HttpInfo_Release(IWinInetHttpInfo *iface)
{
    HttpProtocol *This = INETINFO_THIS(iface);
    return IInternetProtocolEx_Release(PROTOCOLEX(This));
}

static HRESULT WINAPI HttpInfo_QueryOption(IWinInetHttpInfo *iface, DWORD dwOption,
        void *pBuffer, DWORD *pcbBuffer)
{
    HttpProtocol *This = INETINFO_THIS(iface);
    FIXME("(%p)->(%x %p %p)\n", This, dwOption, pBuffer, pcbBuffer);
    return E_NOTIMPL;
}

static HRESULT WINAPI HttpInfo_QueryInfo(IWinInetHttpInfo *iface, DWORD dwOption,
        void *pBuffer, DWORD *pcbBuffer, DWORD *pdwFlags, DWORD *pdwReserved)
{
    HttpProtocol *This = INETINFO_THIS(iface);
    FIXME("(%p)->(%x %p %p %p %p)\n", This, dwOption, pBuffer, pcbBuffer, pdwFlags, pdwReserved);
    return E_NOTIMPL;
}

#undef INETINFO_THIS

static const IWinInetHttpInfoVtbl WinInetHttpInfoVtbl = {
    HttpInfo_QueryInterface,
    HttpInfo_AddRef,
    HttpInfo_Release,
    HttpInfo_QueryOption,
    HttpInfo_QueryInfo
};

static HRESULT create_http_protocol(BOOL https, void **ppobj)
{
    HttpProtocol *ret;

    ret = heap_alloc_zero(sizeof(HttpProtocol));
    if(!ret)
        return E_OUTOFMEMORY;

    ret->base.vtbl = &AsyncProtocolVtbl;
    ret->lpIInternetProtocolExVtbl = &HttpProtocolVtbl;
    ret->lpInternetPriorityVtbl    = &HttpPriorityVtbl;
    ret->lpWinInetHttpInfoVtbl     = &WinInetHttpInfoVtbl;

    ret->https = https;
    ret->ref = 1;

    *ppobj = PROTOCOLEX(ret);
    
    URLMON_LockModule();
    return S_OK;
}

HRESULT HttpProtocol_Construct(IUnknown *pUnkOuter, LPVOID *ppobj)
{
    TRACE("(%p %p)\n", pUnkOuter, ppobj);

    return create_http_protocol(FALSE, ppobj);
}

HRESULT HttpSProtocol_Construct(IUnknown *pUnkOuter, LPVOID *ppobj)
{
    TRACE("(%p %p)\n", pUnkOuter, ppobj);

    return create_http_protocol(TRUE, ppobj);
}
