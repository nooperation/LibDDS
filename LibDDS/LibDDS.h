#pragma once
#define DllExport   __declspec( dllexport ) 

namespace DirectX
{
    enum WICCodecs;
}
enum DXGI_FORMAT;

struct ConversionOptions
{
    std::size_t width;
    std::size_t height;
    DirectX::WICCodecs codec;
    DXGI_FORMAT format;
};

struct ImageProperties
{
    std::size_t width;
    std::size_t height;
    DXGI_FORMAT format;
};

extern "C"
{
    DllExport bool ConvertDdsInMemory(
        _In_ const unsigned char *inDdsBytes,
        _In_ std::size_t inDdsBytesSize,
        _In_ ConversionOptions options,
        _Out_opt_ unsigned char **outBuff,
        _Out_opt_ std::size_t *outBuffSize,
        _Out_opt_ ImageProperties *outImageProperties
    );

    DllExport const char *GetError();
    DllExport void FreeMemory(_In_ const unsigned char *data);
}
