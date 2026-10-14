#include <thread>
#include <chrono>
#include <string>
#include <iostream>
#include <cstring>
#include <NVI/API.h>

#ifdef _WIN32
#include <Windows.h>
#include <WinSock2.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

enum class HDRStaticType : uint8_t
{
    ST2086 = 1,
    Vivid = 2,
};

bool operator==(const HDRStaticType& l, const uint8_t& r)
{
    return static_cast<uint8_t>(l) == r;
}

struct hdr_static_syntax
{
    uint8_t type;  ///< See HDRStaticType
    uint8_t params[0];
};

struct hdr_st2086_static
{
    uint16_t display_primaries_x[3];
    uint16_t display_primaries_y[3];
    uint16_t white_point_x;
    uint16_t white_point_y;
    uint32_t max_display_mastering_luminance;
    uint32_t min_display_mastering_luminance;
};
static_assert(sizeof(hdr_st2086_static) == 24);
typedef hdr_st2086_static hdr_st2086_static_N;
static constexpr size_t kST2086StaticMetadataSize = sizeof(hdr_static_syntax) + sizeof(hdr_st2086_static_N);

/// GY/T 358-2022表10
struct hdr_vivid_static
{
    uint16_t display_primaries_x[3];
    uint16_t display_primaries_y[3];
    uint16_t white_point_x;
    uint16_t white_point_y;
    uint16_t max_display_mastering_luminance;
    uint16_t min_display_mastering_luminance;
    uint16_t max_content_light_level;
    uint16_t max_picture_average_light_level;
};
static_assert(sizeof(hdr_vivid_static) == 24);
typedef hdr_vivid_static hdr_vivid_static_N;
static constexpr size_t kVividStaticMetadataSize = sizeof(hdr_static_syntax) + sizeof(hdr_vivid_static_N);

hdr_vivid_static_N Vivid2VividN(const hdr_vivid_static& h)
{
    hdr_vivid_static_N n{};
    n.display_primaries_x[0] = htons(h.display_primaries_x[0]);
    n.display_primaries_x[1] = htons(h.display_primaries_x[1]);
    n.display_primaries_x[2] = htons(h.display_primaries_x[2]);
    n.display_primaries_y[0] = htons(h.display_primaries_y[0]);
    n.display_primaries_y[1] = htons(h.display_primaries_y[1]);
    n.display_primaries_y[2] = htons(h.display_primaries_y[2]);
    n.white_point_x = htons(h.white_point_x);
    n.white_point_y = htons(h.white_point_y);
    n.max_display_mastering_luminance = htons(h.max_display_mastering_luminance);
    n.min_display_mastering_luminance = htons(h.min_display_mastering_luminance);
    n.max_content_light_level = htons(h.max_content_light_level);
    n.max_picture_average_light_level = htons(h.max_picture_average_light_level);
    return n;
}

hdr_vivid_static Vivid2VividH(const hdr_vivid_static_N& n)
{
    hdr_vivid_static h{};
    h.display_primaries_x[0] = ntohs(n.display_primaries_x[0]);
    h.display_primaries_x[1] = ntohs(n.display_primaries_x[1]);
    h.display_primaries_x[2] = ntohs(n.display_primaries_x[2]);
    h.display_primaries_y[0] = ntohs(n.display_primaries_y[0]);
    h.display_primaries_y[1] = ntohs(n.display_primaries_y[1]);
    h.display_primaries_y[2] = ntohs(n.display_primaries_y[2]);
    h.white_point_x = ntohs(n.white_point_x);
    h.white_point_y = ntohs(n.white_point_y);
    h.max_display_mastering_luminance = ntohs(n.max_display_mastering_luminance);
    h.min_display_mastering_luminance = ntohs(n.min_display_mastering_luminance);
    h.max_content_light_level = ntohs(n.max_content_light_level);
    h.max_picture_average_light_level = ntohs(n.max_picture_average_light_level);
    return h;
}

size_t SetupVivid(const hdr_vivid_static& vivid, uint8_t* buffer, size_t size)
{
    uint8_t data[kVividStaticMetadataSize];
    hdr_static_syntax& hdr = *reinterpret_cast<hdr_static_syntax*>(data);
    hdr.type = static_cast<uint8_t>(HDRStaticType::Vivid);
    hdr_vivid_static_N& n = *reinterpret_cast<hdr_vivid_static_N*>(hdr.params);
    n = Vivid2VividN(vivid);
    NVIMetaNode node{NVIMetaDefined::NVIMeta_StaticHDR, static_cast<uint32_t>(kVividStaticMetadataSize), data};
    return NVIMetaSetupNode(&node, buffer, size);
}

inline bool HostBigEndian()
{
    constexpr union
    {
        int32_t s;
        int8_t big[4];
    } endian{0x01000000};
    return endian.big[0] == 1;
}

void ParseSideData(const NVISideData& side)
{
    const uint8_t* pData = side.bytes;
    size_t szData = side.size;
    while (szData > 0)
    {
        NVIMetaNode node{};
        int32_t nParsed = NVIMetaParseNode(pData, szData, &node);
        if (nParsed > 0)
        {
            szData -= nParsed;
            if (node.defined == NVIMetaDefined::NVIMeta_StaticHDR && node.length > kVividStaticMetadataSize)
            {
                if (HDRStaticType::Vivid == node.value[0])
                {
                    hdr_vivid_static vivid = Vivid2VividH(*reinterpret_cast<const hdr_vivid_static_N*>(node.value + 1));
                    // ...
                }
            }
        }
        else
        {
            break;
        }
    }
}

void SendVideoHDR()
{
    NVI_CONTEXT context = NVIContextCreate(nullptr);
    NVISendAllocParam param{};
    param.alias = "Example HDR";
    NVI_SENDER sender = NVISendAlloc(context, &param);
    NVIVideoImageFrame image{};
    image.side.bytes = nullptr;
    image.side.size = 0;
    image.updated = nullptr;
    image.info.codec = NVICodec_HFM;
    image.info.width = 3840;
    image.info.height = 2160u;
    image.info.frame_rate_num = 25u;
    image.info.frame_rate_den = 1u;
    image.info.rotation = 0u;
    image.info.colorspace.primary = NVIPrimary_BT2020;
    image.info.colorspace.transfer = NVITransfer_BT2020_10;
    image.info.colorspace.matrix = NVIMatrix_BT2020_NCL;
    image.info.colorspace.range = NVIRange_Full;
    image.info.tick.value = 0u;
    image.info.tick.freq_num = 1u;
    image.info.tick.freq_den = 90000u;
    image.info.time = 0;
    image.buffer.format = HostBigEndian() ? NVIPixel_422P10BE : NVIPixel_422P10LE;                /// see NVIPixelFormat
    image.buffer.type = NVIBuffer_HOST;                                                           /// see NVIBufferType
    size_t szImageBuffer = (size_t)image.info.width * image.info.height;                          //Y
    szImageBuffer += (((size_t)image.info.width + 1) >> 1) * ((image.info.height + 1) >> 1) * 2;  //UV
    std::unique_ptr<uint8_t> pImageBuffer(new uint8_t[szImageBuffer]);
    memset(pImageBuffer.get(), 0, szImageBuffer);
    image.buffer.strides[0] = image.info.width;
    image.buffer.strides[1] = (image.info.width + 1) >> 1;
    image.buffer.strides[2] = (image.info.width + 1) >> 1;
    image.buffer.strides[3] = 0;
    image.buffer.planes[0] = pImageBuffer.get();
    image.buffer.planes[1] = image.buffer.planes[0] + (size_t)image.buffer.strides[0] * image.info.height;
    image.buffer.planes[2] = image.buffer.planes[1] + (size_t)image.buffer.strides[1] * ((image.info.height + 1) >> 1);
    image.buffer.planes[3] = nullptr;
    NVIAudioWaveFrame wave{};
    wave.info.codec = NVICodec_LPCM;
    wave.info.sample_rate = 48000u;
    wave.info.depth = 16;
    wave.info.channels = 2;
    wave.info.tick.value = 0;
    wave.info.tick.freq_num = 1u;
    wave.info.tick.freq_den = wave.info.sample_rate;
    wave.info.time = 0;
    wave.buffer.align = 2;
    wave.buffer.samples = wave.info.sample_rate / image.info.frame_rate_num;
    size_t szWaveBuffer = (size_t)wave.buffer.samples * wave.buffer.align * wave.info.channels;
    std::unique_ptr<uint8_t> pWaveBuffer(new uint8_t[szWaveBuffer]);
    memset(pWaveBuffer.get(), 0, szWaveBuffer);
    wave.buffer.data = pWaveBuffer.get();
    wave.buffer.size = szWaveBuffer;
    auto next = std::chrono::steady_clock::now();
    auto freq = std::chrono::milliseconds(1000 / image.info.frame_rate_num);
    std::unique_ptr<uint8_t> pSideDataBuffer(new uint8_t[512]);
    {
        hdr_vivid_static vivid{};
        const size_t szMetadata = SetupVivid(vivid, pSideDataBuffer.get(), 512);
        if (szMetadata > 0)
        {
            image.side.bytes = pSideDataBuffer.get();
            image.side.size = szMetadata;
        }
    }
    while (true)
    {
        if (next <= std::chrono::steady_clock::now())
        {
            next += freq;
            image.info.time = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
            wave.info.time = image.info.time;
            if (NVISendVideo(sender, &image) < 0)
            {
                break;
            }
            if (NVISendAudio(sender, &wave) < 0)
            {
                break;
            }
            image.info.tick.value += 3600;
            wave.info.tick.value += wave.buffer.samples;
            memset(pImageBuffer.get(), (uint8_t)(image.info.tick.value / 3600), (size_t)(image.info.width) * image.info.height);
        }
        else
        {
            std::this_thread::sleep_until(next);
        }
    }
    NVISendFree(sender);
    NVIContextDestory(context);
}

int main(int argc, char** argv)
{
    SendVideoHDR();
    return 0;
}
