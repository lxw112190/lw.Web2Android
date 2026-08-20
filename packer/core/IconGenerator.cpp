#include "core/IconGenerator.h"

#ifdef _WIN32
#include <Windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <array>
#include <stdexcept>
#include <utility>

namespace lw::web2android {
namespace {

using Microsoft::WRL::ComPtr;

class ComScope {
public:
    ComScope() : initialized_(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) {}
    ~ComScope() { if (initialized_) CoUninitialize(); }
    ComScope(const ComScope&) = delete;
    ComScope& operator=(const ComScope&) = delete;
private:
    bool initialized_;
};

ComPtr<IWICImagingFactory> Factory() {
    ComPtr<IWICImagingFactory> factory;
    const HRESULT result = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                             IID_PPV_ARGS(&factory));
    if (FAILED(result)) throw std::runtime_error("Unable to create Windows Imaging Component factory");
    return factory;
}

ComPtr<IWICBitmapFrameDecode> DecodeFrame(const std::filesystem::path& source,
                                          IWICImagingFactory* factory) {
    ComPtr<IWICBitmapDecoder> decoder;
    HRESULT result = factory->CreateDecoderFromFilename(source.c_str(), nullptr, GENERIC_READ,
                                                        WICDecodeMetadataCacheOnLoad, &decoder);
    if (FAILED(result)) throw std::runtime_error("Invalid app icon: unable to decode PNG");
    GUID container{};
    if (FAILED(decoder->GetContainerFormat(&container)) || container != GUID_ContainerFormatPng) {
        throw std::runtime_error("Invalid app icon: file is not a PNG image");
    }
    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(0, &frame))) throw std::runtime_error("Invalid app icon: PNG has no frame");
    return frame;
}

ComPtr<IWICBitmapSource> ToBgra(IWICBitmapFrameDecode* frame, IWICImagingFactory* factory) {
    ComPtr<IWICFormatConverter> converter;
    if (FAILED(factory->CreateFormatConverter(&converter)) ||
        FAILED(converter->Initialize(frame, GUID_WICPixelFormat32bppBGRA,
                                     WICBitmapDitherTypeNone, nullptr, 0.0,
                                     WICBitmapPaletteTypeCustom))) {
        throw std::runtime_error("Invalid app icon: unable to convert PNG pixels");
    }
    return converter;
}

void Encode(IWICBitmapSource* source, const std::filesystem::path& destination,
            UINT size, IWICImagingFactory* factory) {
    std::filesystem::create_directories(destination.parent_path());
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICStream> stream;
    if (FAILED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) ||
        FAILED(factory->CreateStream(&stream)) ||
        FAILED(stream->InitializeFromFilename(destination.c_str(), GENERIC_WRITE)) ||
        FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache))) {
        throw std::runtime_error("Unable to create PNG launcher icon");
    }
    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> properties;
    if (FAILED(encoder->CreateNewFrame(&frame, &properties)) ||
        FAILED(frame->Initialize(properties.Get())) ||
        FAILED(frame->SetSize(size, size))) {
        throw std::runtime_error("Unable to initialize PNG launcher icon");
    }
    GUID format = GUID_WICPixelFormat32bppBGRA;
    if (FAILED(frame->SetPixelFormat(&format)) || format != GUID_WICPixelFormat32bppBGRA ||
        FAILED(frame->WriteSource(source, nullptr)) || FAILED(frame->Commit()) || FAILED(encoder->Commit())) {
        throw std::runtime_error("Unable to encode PNG launcher icon");
    }
}

}  // namespace

IconInfo IconGenerator::Inspect(const std::filesystem::path& source) {
    if (!std::filesystem::is_regular_file(source)) {
        throw std::runtime_error("Invalid app icon: file does not exist: " + source.u8string());
    }
    ComScope com;
    const auto factory = Factory();
    const auto frame = DecodeFrame(source, factory.Get());
    UINT width = 0;
    UINT height = 0;
    if (FAILED(frame->GetSize(&width, &height)) || width != height) {
        throw std::runtime_error("Invalid app icon: PNG image must be square");
    }
    if (width < 192U) throw std::runtime_error("Invalid app icon: image must be at least 192x192 pixels");
    if (width > 4096U) throw std::runtime_error("Invalid app icon: image is too large; maximum supported size is 4096x4096");
    return {width, height};
}

void IconGenerator::Generate(const std::filesystem::path& source,
                             const std::filesystem::path& resourceDirectory) {
    const auto info = Inspect(source);
    (void)info;
    ComScope com;
    const auto factory = Factory();
    const auto frame = DecodeFrame(source, factory.Get());
    const auto bgra = ToBgra(frame.Get(), factory.Get());
    const std::array<std::pair<const char*, UINT>, 5> densities = {{
        {"mipmap-mdpi", 48}, {"mipmap-hdpi", 72}, {"mipmap-xhdpi", 96},
        {"mipmap-xxhdpi", 144}, {"mipmap-xxxhdpi", 192}}};
    for (const auto& density : densities) {
        ComPtr<IWICBitmapScaler> scaler;
        if (FAILED(factory->CreateBitmapScaler(&scaler)) ||
            FAILED(scaler->Initialize(bgra.Get(), density.second, density.second,
                                       WICBitmapInterpolationModeFant))) {
            throw std::runtime_error("Unable to resize PNG launcher icon");
        }
        Encode(scaler.Get(), resourceDirectory / density.first / "ic_launcher.png",
               density.second, factory.Get());
    }
}

}  // namespace lw::web2android
#else
#include <stdexcept>
namespace lw::web2android {
IconInfo IconGenerator::Inspect(const std::filesystem::path&) {
    throw std::runtime_error("Custom app icons are supported on Windows only");
}
void IconGenerator::Generate(const std::filesystem::path&, const std::filesystem::path&) {
    throw std::runtime_error("Custom app icons are supported on Windows only");
}
}  // namespace lw::web2android
#endif
