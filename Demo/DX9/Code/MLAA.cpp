/**
 * Copyright (C) 2011 Jorge Jimenez (jorge@iryoku.com)
 * Copyright (C) 2011 Belen Masia (bmasia@unizar.es) 
 * Copyright (C) 2011 Jose I. Echevarria (joseignacioechevarria@gmail.com) 
 * Copyright (C) 2011 Fernando Navarro (fernandn@microsoft.com) 
 * Copyright (C) 2011 Diego Gutierrez (diegog@unizar.es)
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 
 *    1. Redistributions of source code must retain the above copyright notice,
 *       this list of conditions and the following disclaimer.
 * 
 *    2. Redistributions in binary form must reproduce the following disclaimer
 *       in the documentation and/or other materials provided with the 
 *       distribution:
 * 
 *      "Uses Jimenez's MLAA. Copyright (C) 2011 by Jorge Jimenez, Belen Masia,
 *       Jose I. Echevarria, Fernando Navarro and Diego Gutierrez."
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS 
 * IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, 
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR 
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL COPYRIGHT HOLDERS OR CONTRIBUTORS 
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR 
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF 
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN 
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE 
 * POSSIBILITY OF SUCH DAMAGE.
 * 
 * The views and conclusions contained in the software and documentation are 
 * those of the authors and should not be interpreted as representing official
 * policies, either expressed or implied, of the copyright holders.
 */


#include <sstream>
#include "MLAA.h"
using namespace std;


#pragma region Useful Macros from DXUT (copy-pasted here as we prefer this to be as self-contained as possible)
#if defined(DEBUG) || defined(_DEBUG)
#ifndef V
#define V(x) { hr = (x); if (FAILED(hr)) { DXTrace(__FILE__, (DWORD)__LINE__, hr, L#x, true); } }
#endif
#ifndef V_RETURN
#define V_RETURN(x) { hr = (x); if (FAILED(hr)) { return DXTrace(__FILE__, (DWORD)__LINE__, hr, L#x, true); } }
#endif
#else
#ifndef V
#define V(x) { hr = (x); }
#endif
#ifndef V_RETURN
#define V_RETURN(x) { hr = (x); if( FAILED(hr) ) { return hr; } }
#endif
#endif

#ifndef SAFE_DELETE
#define SAFE_DELETE(p) { if (p) { delete (p); (p) = NULL; } }
#endif
#ifndef SAFE_DELETE_ARRAY
#define SAFE_DELETE_ARRAY(p) { if (p) { delete[] (p); (p) = NULL; } }
#endif
#ifndef SAFE_RELEASE
#define SAFE_RELEASE(p) { if (p) { (p)->Release(); (p) = NULL; } }
#endif
#pragma endregion

#pragma region This stuff is for loading headers from resources
class ID3D10IncludeResource : public ID3DXInclude {
    public:
        STDMETHOD(Open)(THIS_ D3DXINCLUDE_TYPE, LPCSTR pFileName, LPCVOID, LPCVOID *ppData, UINT *pBytes)  {
            wstringstream s;
            s << pFileName;
            HRSRC src = FindResource(GetModuleHandle(NULL), s.str().c_str(), RT_RCDATA);
            HGLOBAL res = LoadResource(GetModuleHandle(NULL), src);

            *pBytes = SizeofResource(GetModuleHandle(NULL), src);
            *ppData = (LPCVOID) LockResource(res);

            return S_OK;
        }

        STDMETHOD(Close)(THIS_ LPCVOID)  {
            return S_OK;
        }
};
#pragma endregion


MLAA::MLAA(IDirect3DDevice9 *device, int width, int height, const ExternalStorage &storage)
        : device(device),
          threshold(0.1f),
          maxSearchSteps(8),
          width(width), height(height) {
    HRESULT hr;

    // Setup the defines for compiling the effect.
    stringstream s;
    s << "float2(1.0 / " << width << ", 1.0 / " << height << ")";
    string pixelSizeText = s.str();

    D3DXMACRO defines[4] = {
        {"MLAA_PIXEL_SIZE", pixelSizeText.c_str()},
        {NULL, NULL}
    };

    // Setup the flags for the effect.
    DWORD flags = D3DXFX_NOT_CLONEABLE;
    #ifdef D3DXFX_LARGEADDRESS_HANDLE
    flags |= D3DXFX_LARGEADDRESSAWARE;
    #endif

    /**
     * IMPORTANT! Here we load and compile the MLAA effect from a *RESOURCE*
     * (Yeah, we like all-in-one executables for demos =)
     * In case you want it to be loaded from other place change this line accordingly.
     */
    ID3D10IncludeResource includeResource;
    V(D3DXCreateEffectFromResource(device, NULL, L"MLAA.fx", defines, &includeResource, flags, NULL, &effect, NULL));

    // Vertex declaration for rendering the typical fullscreen quad later on.
    const D3DVERTEXELEMENT9 vertexElements[3] = {
        { 0, 0,  D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 },
        { 0, 12, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD,  0 },
        D3DDECL_END()
    };
    V(device->CreateVertexDeclaration(vertexElements , &vertexDeclaration));

    // If storage for the edges is not specified we will create it.
    if (storage.edgeTexture != NULL && storage.edgeSurface != NULL) {
        edgeTexture = storage.edgeTexture;
        edgeSurface = storage.edgeSurface;
        releaseEdgeResources = false;
    } else {
        V(device->CreateTexture(width, height, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &edgeTexture, NULL));
        V(edgeTexture->GetSurfaceLevel(0, &edgeSurface));
        releaseEdgeResources = true;
    }

    // Same for blending weights.
    if (storage.blendTexture != NULL && storage.blendSurface != NULL) {
        blendTexture = storage.blendTexture;
        blendSurface = storage.blendSurface;
        releaseBlendResources = false;
    } else {
        V(device->CreateTexture(width, height, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &blendTexture, NULL));
        V(blendTexture->GetSurfaceLevel(0, &blendSurface));
        releaseBlendResources = true;
    }

    // Load the pre-computed areas texture.
    // For some obscure reason, if if we use D3DX_DEFAULT as the width and height parameters of D3DXCreateTextureFromResourceEx, the texture gets scaled down.
    D3DXIMAGE_INFO info;
    V(D3DXGetImageInfoFromResource(NULL, L"AreaTex.dds", &info));
    V(D3DXCreateTextureFromResourceEx(device, NULL, L"AreaTex.dds", info.Width, info.Height, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, D3DX_FILTER_NONE, D3DX_FILTER_NONE, 0, &info, NULL, &areaTexture));

    // Load the pre-computed search length texture.
    V(D3DXGetImageInfoFromResource(NULL, L"SearchTex.dds", &info));
    V(D3DXCreateTextureFromResourceEx(device, NULL, L"SearchTex.dds", info.Width, info.Height, 1, 0, D3DFMT_L8, D3DPOOL_DEFAULT, D3DX_FILTER_NONE, D3DX_FILTER_NONE, 0, &info, NULL, &searchTexture));

    // Create some handles for techniques and variables.
    thresholdHandle = effect->GetParameterByName(NULL, "threshold");
    maxSearchStepsHandle = effect->GetParameterByName(NULL, "maxSearchSteps");
    areaTexHandle = effect->GetParameterByName(NULL, "areaTex2D");
    searchTexHandle = effect->GetParameterByName(NULL, "searchTex2D");
    colorTexHandle = effect->GetParameterByName(NULL, "colorTex2D");
    depthTexHandle = effect->GetParameterByName(NULL, "depthTex2D");
    edgesTexHandle = effect->GetParameterByName(NULL, "edgesTex2D");
    blendTexHandle = effect->GetParameterByName(NULL, "blendTex2D");
    lumaEdgeDetectionHandle = effect->GetTechniqueByName("LumaEdgeDetection");
    colorEdgeDetectionHandle = effect->GetTechniqueByName("ColorEdgeDetection");
    depthEdgeDetectionHandle = effect->GetTechniqueByName("DepthEdgeDetection");
    blendWeightCalculationHandle = effect->GetTechniqueByName("BlendWeightCalculation");
    neighborhoodBlendingHandle = effect->GetTechniqueByName("NeighborhoodBlending");
}


MLAA::~MLAA() {
    SAFE_RELEASE(effect);
    SAFE_RELEASE(vertexDeclaration);

    if (releaseEdgeResources) { // We will be releasing these things *only* if we created them.
        SAFE_RELEASE(edgeTexture);
        SAFE_RELEASE(edgeSurface);
    }

    if (releaseBlendResources) { // Same applies over here.
        SAFE_RELEASE(blendTexture);
        SAFE_RELEASE(blendSurface);
    }

    SAFE_RELEASE(areaTexture);
    SAFE_RELEASE(searchTexture);
}


void MLAA::go(IDirect3DTexture9 *edges,
              IDirect3DTexture9 *src, 
              IDirect3DSurface9 *dst,
              Input input) {
    HRESULT hr;

    // Setup the layout for our fullscreen quad.
    V(device->SetVertexDeclaration(vertexDeclaration));

    // And here we go!
    edgesDetectionPass(edges, input);
    blendingWeightsCalculationPass();
    neighborhoodBlendingPass(src, dst);
}


void MLAA::edgesDetectionPass(IDirect3DTexture9 *edges, Input input) {
    HRESULT hr;

    // Set the render target and clear both the color and the stencil buffers.
    V(device->SetRenderTarget(0, edgeSurface));
    V(device->Clear(0, NULL, D3DCLEAR_TARGET, D3DCOLOR_ARGB(0, 0, 0, 0), 1.0f, 0));

    // Setup variables.
    V(effect->SetFloat(thresholdHandle, threshold));
    V(effect->SetFloat(maxSearchStepsHandle, float(maxSearchSteps)));

    // Select the technique accordingly.
    switch (input) {
        case INPUT_LUMA:
            V(effect->SetTexture(colorTexHandle, edges));
            V(effect->SetTechnique(lumaEdgeDetectionHandle));
            break;
        case INPUT_COLOR:
            V(effect->SetTexture(colorTexHandle, edges));
            V(effect->SetTechnique(colorEdgeDetectionHandle));
            break;
        case INPUT_DEPTH:
            V(effect->SetTexture(depthTexHandle, edges));
            V(effect->SetTechnique(depthEdgeDetectionHandle));
            break;
        default:
            throw logic_error("unexpected error");
    }

    // Do it!
    UINT passes;
    V(effect->Begin(&passes, 0));
    V(effect->BeginPass(0));
    quad(width, height);
    V(effect->EndPass());
    V(effect->End());
}


void MLAA::blendingWeightsCalculationPass() {
    HRESULT hr;

    // Set the render target and clear it.
    V(device->SetRenderTarget(0, blendSurface));
    V(device->Clear(0, NULL, D3DCLEAR_TARGET, D3DCOLOR_ARGB(0, 0, 0, 0), 1.0f, 0));

    // Setup the variables and the technique (yet again).
    V(effect->SetTexture(edgesTexHandle, edgeTexture));
    V(effect->SetTexture(areaTexHandle, areaTexture));
    V(effect->SetTexture(searchTexHandle, searchTexture));
    V(effect->SetTechnique(blendWeightCalculationHandle));

    // And here we go!
    UINT passes;
    V(effect->Begin(&passes, 0));
    V(effect->BeginPass(0));
    quad(width, height);
    V(effect->EndPass());
    V(effect->End());
}


void MLAA::neighborhoodBlendingPass(IDirect3DTexture9 *src, IDirect3DSurface9 *dst) { 
    HRESULT hr;

    // Blah blah blah
    V(device->SetRenderTarget(0, dst));
    V(effect->SetTexture(colorTexHandle, src));
    V(effect->SetTexture(blendTexHandle, blendTexture));
    V(effect->SetTechnique(neighborhoodBlendingHandle));

    // Yeah! We will finally have the antialiased image :D
    UINT passes;
    V(effect->Begin(&passes, 0));
    V(effect->BeginPass(0));
    quad(width, height);
    V(effect->EndPass());
    V(effect->End());
}


void MLAA::quad(int width, int height) {
    // Typical aligned fullscreen quad.
    HRESULT hr;
    D3DXVECTOR2 pixelSize = D3DXVECTOR2(1.0f / float(width), 1.0f / float(height));
    float quad[4][5] = {
        { -1.0f - pixelSize.x,  1.0f + pixelSize.y, 0.5f, 0.0f, 0.0f },
        {  1.0f - pixelSize.x,  1.0f + pixelSize.y, 0.5f, 1.0f, 0.0f },
        { -1.0f - pixelSize.x, -1.0f + pixelSize.y, 0.5f, 0.0f, 1.0f },
        {  1.0f - pixelSize.x, -1.0f + pixelSize.y, 0.5f, 1.0f, 1.0f }
    };
    V(device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(quad[0])));
}
