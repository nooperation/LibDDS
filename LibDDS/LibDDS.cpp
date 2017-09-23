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
#include <mutex>

#include "LibDDS.h"

#include "..\ThirdParty\DirectXTex\DirectXTex\DirectXTex.h"
#include "Utils.h"

std::mutex blobMapMutex;
std::unordered_map<const void *, DirectX::Blob> blobMap;
static thread_local std::string _errorMessage = "";

void FreeMemory(_In_ const unsigned char *data)
{
    std::lock_guard<std::mutex> lock(blobMapMutex);

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

bool ConvertDdsInMemory(
    _In_ const unsigned char *inDdsBytes,
    _In_ std::size_t inDdsBytesSize,
    _In_ ConversionOptions options,
    _Out_opt_ unsigned char **outBuff,
    _Out_opt_ std::size_t* outBuffSize,
    _Out_opt_ ImageProperties *outImageProperties)
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
    DirectX::TexMetadata originalInfo;
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
    originalInfo = info;

    if (DirectX::IsTypeless(info.format))
    {
        info.format = DirectX::MakeTypelessFLOAT(info.format);
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

    // --- Resize ------------------------------------------------------------------
    if (options.width != 0 && options.height != 0 && (info.width != options.width || info.height != options.height))
    {
        std::unique_ptr<DirectX::ScratchImage> timage(new (std::nothrow) DirectX::ScratchImage);
        if (!timage)
        {
            std::stringstream errorMessage;
            errorMessage << "ERROR Failed to allocate memory for resizing" << std::endl;
            SetError(errorMessage.str());
            return false;
        }

        hr = Resize(image->GetImages(), image->GetImageCount(), image->GetMetadata(), options.width, options.height, 0, *timage);
        if (FAILED(hr))
        {
            std::stringstream errorMessage;
            errorMessage << "ERROR Failed to resize image to " << options.width << "x" << options.height << std::endl;
            SetError(errorMessage.str());
            return false;
        }

        auto& tinfo = timage->GetMetadata();

        info.width = tinfo.width;
        info.height = tinfo.height;
        info.mipLevels = 1;

        image.swap(timage);
        cimage.reset();
    }

    // --- Convert -----------------------------------------------------------------

    auto tformat = (options.format == DXGI_FORMAT_UNKNOWN) ? info.format : options.format;
    if (info.format != tformat && !DirectX::IsCompressed(tformat))
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

    auto blobNeedsToBeReleased = true;

    if (outBuff != nullptr)
    {
        *outBuff = reinterpret_cast<unsigned char *>(blob.GetBufferPointer());
        blobNeedsToBeReleased = false;

        std::lock_guard<std::mutex> lock(blobMapMutex);
        blobMap[blob.GetBufferPointer()] = std::move(blob);
    }

    if (outImageProperties != nullptr)
    {
        outImageProperties->width = originalInfo.width;
        outImageProperties->height = originalInfo.height;
        outImageProperties->format = originalInfo.format;
    }

    if (outBuffSize != nullptr)
    {
         *outBuffSize = blob.GetBufferSize();
    }

    if (blobNeedsToBeReleased)
    {
        blob.Release();
    }

    return true;
}
