#pragma warning(push)
#pragma warning(disable : 4005)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define NODRAWTEXT
#define NOGDI
#define NOBITMAP
#define NOMCX
#define NOSERVICE
#define NOHELP
#pragma warning(pop)
#include <Windows.h>
#include <string>
#include <vector>
#include <memory>
#include <iomanip>
#include <unordered_map>
#include <sstream>

#include "LibDDS.h"

#include "..\ThirdParty\DirectXTex\DirectXTex\DirectXTex.h"
#include "Utils.h"

std::unordered_map<const void *, DirectX::Blob> blobMap;
static thread_local std::string _errorMessage = "";

void FreeMemory(const unsigned char *data)
{
    auto blobIter = blobMap.find(data);
    if (blobIter != blobMap.end())
    {
        blobIter->second.Release();
        blobMap.erase(blobIter);
    }
}

void SetError(const std::string& errorMessage)
{
    _errorMessage = errorMessage;
}

const char *GetError()
{
    return _errorMessage.c_str();
}

bool ConvertDdsInMemory(const unsigned char *inDdsBytes, std::size_t inDdsBytesSize, unsigned char **outBuff, std::size_t* outBuffSize, ConversionOptions options)
{
    // Initialize COM (needed for WIC)
    auto hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr))
    {
        if (hr != RPC_E_CHANGED_MODE)
        {
            std::stringstream errorMessage;
            errorMessage << "ERROR CoInitializeEx failed with code: " << std::hex << hr << std::endl;
            SetError(errorMessage.str());
            return false;
        }
    }

    // Convert images
    DirectX::TexMetadata info;
    auto image = std::unique_ptr<DirectX::ScratchImage>(new (std::nothrow) DirectX::ScratchImage);
    if (!image)
    {
        std::stringstream errorMessage;
        errorMessage << "ERROR Failed to allocate memory for DirectX::LoadFromDDSMemory" << std::endl;
        SetError(errorMessage.str());
        return false;
    }

    hr = DirectX::LoadFromDDSMemory(inDdsBytes, inDdsBytesSize, DirectX::DDS_FLAGS_NONE, &info, *image);
    if (FAILED(hr))
    {
        std::stringstream errorMessage;
        errorMessage << "ERROR LoadFromDDSMemory failed with code: " << std::hex << hr << std::endl;
        SetError(errorMessage.str());
        return false;
    }

    if (DirectX::IsTypeless(info.format))
    {
        if (options.optionTypelessUnorm)
        {
            info.format = DirectX::MakeTypelessUNORM(info.format);
        }
        else if (options.optionTypelessFloat)
        {
            info.format = DirectX::MakeTypelessFLOAT(info.format);
        }

        if (DirectX::IsTypeless(info.format))
        {
            std::stringstream errorMessage;
            errorMessage << "ERROR Image has a typeless format of: " << info.format << std::endl;
            SetError(errorMessage.str());
            return false;
        }

        image->OverrideFormat(info.format);
    }

    // --- Planar ------------------------------------------------------------------
    if (DirectX::IsPlanar(info.format))
    {
        auto img = image->GetImage(0, 0, 0);
        auto nimg = image->GetImageCount();

        auto timage = std::unique_ptr<DirectX::ScratchImage>(new (std::nothrow) DirectX::ScratchImage);
        if (!timage)
        {
            std::stringstream errorMessage;
            errorMessage << "ERROR Failed to allocate memory for DirectX::ConvertToSinglePlane" << std::endl;
            SetError(errorMessage.str());
            return false;
        }

        hr = DirectX::ConvertToSinglePlane(img, nimg, info, *timage);
        if (FAILED(hr))
        {
            std::stringstream errorMessage;
            errorMessage << "ERROR DirectX::ConvertToSinglePlane failed with code: " << std::hex << hr << std::endl;
            SetError(errorMessage.str());
            return false;
        }

        auto& tinfo = timage->GetMetadata();
        info.format = tinfo.format;

        image.swap(timage);
    }

    // --- Decompress --------------------------------------------------------------
    std::unique_ptr<DirectX::ScratchImage> cimage;
    if (DirectX::IsCompressed(info.format))
    {
        auto img = image->GetImage(0, 0, 0);
        size_t nimg = image->GetImageCount();

        std::unique_ptr<DirectX::ScratchImage> timage(new (std::nothrow) DirectX::ScratchImage);
        if (!timage)
        {
            std::stringstream errorMessage;
            errorMessage << "ERROR failed to allocate memory for DDS decompression" << std::endl;
            SetError(errorMessage.str());
            return false;
        }

        hr = DirectX::Decompress(img, nimg, info, DXGI_FORMAT_UNKNOWN /* picks good default */, *timage);
        if (FAILED(hr))
        {
            std::stringstream errorMessage;
            errorMessage << "ERROR DirectX::Decompress failed with code: " << std::hex << hr << std::endl;
            SetError(errorMessage.str());
            return false;
        }

        auto& tinfo = timage->GetMetadata();
        info.format = tinfo.format;

        image.swap(timage);
    }

    // --- Convert -----------------------------------------------------------------

    auto tformat = (options.format == DXGI_FORMAT_UNKNOWN) ? info.format : options.format;
    switch (tformat)
    {
        case DXGI_FORMAT_R8G8_UNORM:
            tformat = DXGI_FORMAT_R32G32B32_FLOAT;
            break;
    }

    std::string normalMapOptions = std::string(options.normalMapOptions);
    if (normalMapOptions.empty() == false)
    {
        auto normalMapFlags = 0;
        if (normalMapOptions.find('l') != std::string::npos)
        {
            normalMapFlags |= DirectX::CNMAP_CHANNEL_LUMINANCE;
        }
        else if (normalMapOptions.find('r') != std::string::npos)
        {
            normalMapFlags |= DirectX::CNMAP_CHANNEL_RED;
        }
        else if (normalMapOptions.find('g') != std::string::npos)
        {
            normalMapFlags |= DirectX::CNMAP_CHANNEL_GREEN;
        }
        else if (normalMapOptions.find('b') != std::string::npos)
        {
            normalMapFlags |= DirectX::CNMAP_CHANNEL_BLUE;
        }
        else if (normalMapOptions.find('a') != std::string::npos)
        {
            normalMapFlags |= DirectX::CNMAP_CHANNEL_ALPHA;
        }
        else
        {
            std::stringstream errorMessage;
            errorMessage << "ERROR Invalid value specified for -nmap (" << normalMapOptions << "), missing l, r, g, b, or a" << std::endl;
            SetError(errorMessage.str());
            return false;
        }

        if (normalMapOptions.find('m') != std::string::npos)
        {
            normalMapFlags |= DirectX::CNMAP_MIRROR;
        }
        else
        {
            if (normalMapOptions.find('u') != std::string::npos)
            {
                normalMapFlags |= DirectX::CNMAP_MIRROR_U;
            }
            if (normalMapOptions.find('v') != std::string::npos)
            {
                normalMapFlags |= DirectX::CNMAP_MIRROR_V;
            }
        }
        if (normalMapOptions.find('i') != std::string::npos)
        {
            normalMapFlags |= DirectX::CNMAP_INVERT_SIGN;
        }
        if (normalMapOptions.find('o') != std::string::npos)
        {
            normalMapFlags |= DirectX::CNMAP_COMPUTE_OCCLUSION;
        }

        std::unique_ptr<DirectX::ScratchImage> timage(new (std::nothrow) DirectX::ScratchImage);
        if (!timage)
        {
            std::stringstream errorMessage;
            errorMessage << "ERROR Failed to allocate memory for DirectX::ComputeNormalMap" << std::endl;
            SetError(errorMessage.str());
            return false;
        }

        auto nmfmt = tformat;
        if (DirectX::IsCompressed(tformat))
        {
            nmfmt = (normalMapFlags & DirectX::CNMAP_COMPUTE_OCCLUSION) ? DXGI_FORMAT_R32G32B32A32_FLOAT : DXGI_FORMAT_R32G32B32_FLOAT;
        }

        hr = DirectX::ComputeNormalMap(image->GetImages(), image->GetImageCount(), image->GetMetadata(), normalMapFlags, 1.0f, nmfmt, *timage);
        if (FAILED(hr))
        {
            std::stringstream errorMessage;
            errorMessage << "ERROR DirectX::ComputeNormalMap failed with code: " << std::hex << hr << std::endl;
            SetError(errorMessage.str());
            return false;
        }

        auto& tinfo = timage->GetMetadata();
        info.format = tinfo.format;

        image.swap(timage);
    }
    else if (info.format != tformat && !DirectX::IsCompressed(tformat))
    {
        std::unique_ptr<DirectX::ScratchImage> timage(new (std::nothrow) DirectX::ScratchImage);
        if (!timage)
        {
            std::stringstream errorMessage;
            errorMessage << "ERROR Failed to allocate memory for DirectX::Convert" << std::endl;
            SetError(errorMessage.str());
            return false;
        }

        hr = DirectX::Convert(image->GetImages(), image->GetImageCount(), image->GetMetadata(), tformat, DirectX::TEX_FILTER_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, *timage);
        if (FAILED(hr))
        {
            std::stringstream errorMessage;
            errorMessage << "ERROR DirectX::Convert failed with code: " << std::hex << hr << std::endl;
            SetError(errorMessage.str());
            return false;
        }

        auto& tinfo = timage->GetMetadata();
        info.format = tinfo.format;

        image.swap(timage);
    }
    cimage.reset();

    // --- Set alpha mode ----------------------------------------------------------
    if (DirectX::HasAlpha(info.format) && info.format != DXGI_FORMAT_A8_UNORM)
    {
        if (image->IsAlphaAllOpaque())
        {
            info.SetAlphaMode(DirectX::TEX_ALPHA_MODE_OPAQUE);
        }
        else if (info.IsPMAlpha())
        {
            // Aleady set TEX_ALPHA_MODE_PREMULTIPLIED
        }
        else if (info.GetAlphaMode() == DirectX::TEX_ALPHA_MODE_UNKNOWN)
        {
            info.SetAlphaMode(DirectX::TEX_ALPHA_MODE_STRAIGHT);
        }
    }
    else
    {
        info.SetAlphaMode(DirectX::TEX_ALPHA_MODE_UNKNOWN);
    }

    // --- Save result -------------------------------------------------------------
    auto blob = DirectX::Blob();
    auto img = image->GetImage(0, 0, 0);
    hr = DirectX::SaveToWICMemory(
        img,
        1,
        DirectX::WIC_FLAGS_NONE,
        GetWICCodec(options.codec),
        blob
    );
    if (FAILED(hr))
    {
        std::stringstream errorMessage;
        errorMessage << "ERROR SaveToWICFile failed with code: " << std::hex << hr << std::endl;
        SetError(errorMessage.str());
        return false;
    }

    *outBuff = reinterpret_cast<unsigned char *>(blob.GetBufferPointer());
    *outBuffSize = blob.GetBufferSize();
    blobMap[blob.GetBufferPointer()] = std::move(blob);

    return true;
}
