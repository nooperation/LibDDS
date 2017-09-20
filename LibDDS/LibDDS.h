#pragma once
#define DllExport   __declspec( dllexport ) 

namespace DirectX
{
    enum WICCodecs;
}
enum DXGI_FORMAT;

struct ConversionOptions
{
    const char* normalMapOptions;
    bool optionTypelessFloat;
    bool optionTypelessUnorm;
    DirectX::WICCodecs codec;
    DXGI_FORMAT format;
};

extern "C"
{
    DllExport bool ConvertDdsInMemory(
        _In_ const unsigned char *inDdsBytes,
        std::size_t inDdsBytesSize,
        _Out_ unsigned char **outBuff,
        _Out_ std::size_t* outBuffSize,
        ConversionOptions options
    );

    DllExport const char *GetError();
    DllExport void FreeMemory(const unsigned char *data);
}
