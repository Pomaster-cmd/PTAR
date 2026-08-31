/* NISFG1B18B1 specialized module: one-shot F9 capture from the isolated FG
 * presenter. This code only executes after an explicit F9 request. It reads the
 * already reconstructed post-HUD native presenter texture on device C and does
 * not alter the normal FG/NVENC scheduling path. */
static void FGCaptureConvertRowBGRA(BYTE* dst,const BYTE* src,UINT width,DXGI_FORMAT fmt){
    UINT x;
    for(x=0u;x<width;x++){
        BYTE* o=&dst[x*4u];
        if(fmt==DXGI_FORMAT_R10G10B10A2_UNORM){
            UINT q=(UINT)src[x*4u]|((UINT)src[x*4u+1u]<<8)|((UINT)src[x*4u+2u]<<16)|((UINT)src[x*4u+3u]<<24);
            UINT r=q&1023u,g=(q>>10)&1023u,b=(q>>20)&1023u;
            o[0]=(BYTE)((b*255u+511u)/1023u);o[1]=(BYTE)((g*255u+511u)/1023u);o[2]=(BYTE)((r*255u+511u)/1023u);o[3]=255u;
        }else if(fmt==DXGI_FORMAT_R8G8B8A8_UNORM||fmt==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB){
            o[0]=src[x*4u+2u];o[1]=src[x*4u+1u];o[2]=src[x*4u];o[3]=src[x*4u+3u];
        }else{
            o[0]=src[x*4u];o[1]=src[x*4u+1u];o[2]=src[x*4u+2u];o[3]=src[x*4u+3u];
        }
    }
}
static int FGCaptureWriteMappedBMP(const D3D11_TEXTURE2D_DESC* d,const D3D11_MAPPED_SUBRESOURCE* m,UINT* outIndex){
    HANDLE f=INVALID_HANDLE_VALUE;BYTE hdr[54];DWORD wr=0u;UINT y,rowBytes,fileSize,captureIndex=0u;int ok=0;
    if(!d||!m||!m->pData||d->Width>8192u)return 0;
    f=ReserveNextCaptureFile(&captureIndex);if(f==INVALID_HANDLE_VALUE){Log("ERROR: F9 FG capture cannot allocate unique output file");return 0;}
    rowBytes=d->Width*4u;fileSize=54u+rowBytes*d->Height;memzero(hdr,sizeof(hdr));hdr[0]='B';hdr[1]='M';Put32(hdr+2,fileSize);Put32(hdr+10,54u);Put32(hdr+14,40u);Put32(hdr+18,d->Width);Put32(hdr+22,(UINT)(0-(LONG)d->Height));Put16(hdr+26,1u);Put16(hdr+28,32u);Put32(hdr+34,rowBytes*d->Height);
    if(!WriteFile(f,hdr,54u,&wr,0)||wr!=54u)goto done;
    for(y=0u;y<d->Height;y++){
        const BYTE* in=(const BYTE*)m->pData+(SIZE_T)y*m->RowPitch;
        FGCaptureConvertRowBGRA(g_captureRow,in,d->Width,d->Format);
        if(!WriteFile(f,g_captureRow,rowBytes,&wr,0)||wr!=rowBytes)break;
    }
    if(y==d->Height){ok=1;if(outIndex)*outIndex=captureIndex;}
done:
    CloseHandle(f);return ok;
}
static int FGCaptureIsolatedPresenterBMP(void){
    ID3D11Texture2D* stage=0;D3D11_TEXTURE2D_DESC d;D3D11_MAPPED_SUBRESOURCE m;PFN_DeviceCreateTexture2D ct;PFN_ContextCopyResource cp;PFN_ContextMap map;PFN_ContextUnmap unmap;HRESULT hr;UINT captureIndex=0u;int ok=0;
    if(!g_fgDisplayDevice||!g_fgDisplayCtx||!g_fgPresenterTex||!g_fgPresenterReady){Log("ERROR: F9 FG capture presenter is not ready");return 0;}
    memzero(&d,sizeof(d));((PFN_Texture2DGetDesc)(*(void***)g_fgPresenterTex)[10])(g_fgPresenterTex,&d);
    if(d.SampleDesc.Count!=1u||d.Width>8192u||!IsSupportedFormat(d.Format)){Log("ERROR: F9 FG capture unsupported presenter format/sample/width");return 0;}
    d.Usage=D3D11_USAGE_STAGING;d.BindFlags=0u;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;d.MiscFlags=0u;
    ct=(PFN_DeviceCreateTexture2D)(*(void***)g_fgDisplayDevice)[5];cp=(PFN_ContextCopyResource)(*(void***)g_fgDisplayCtx)[47];map=(PFN_ContextMap)(*(void***)g_fgDisplayCtx)[14];unmap=(PFN_ContextUnmap)(*(void***)g_fgDisplayCtx)[15];
    hr=ct(g_fgDisplayDevice,&d,0,&stage);if(FAILED(hr)||!stage){Log("ERROR: F9 FG capture staging texture creation failed");goto done;}
    cp(g_fgDisplayCtx,stage,g_fgPresenterTex);memzero(&m,sizeof(m));hr=map(g_fgDisplayCtx,stage,0u,D3D11_MAP_READ,0u,&m);if(FAILED(hr)||!m.pData){Log("ERROR: F9 FG capture Map failed");goto done;}
    ok=FGCaptureWriteMappedBMP(&d,&m,&captureIndex);unmap(g_fgDisplayCtx,stage,0u);
    if(ok){LogDims("OK: F9 FG post-overlay isolated presenter screenshot saved ",d.Width,d.Height);LogCaptureFilename(captureIndex);FeedbackToast(FB_CAPTURE_SAVED,captureIndex,0u,0u,3000u);}
done:
    ReleaseObj((void**)&stage);return ok;
}
