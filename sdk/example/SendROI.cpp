#include <thread>
#include <chrono>
#include <string>
#include <iostream>
#include <fstream>
#include <cstring>
#include <NVI/API.h>

void SendVideoROI()
{
    NVI_CONTEXT context = NVIContextCreate(nullptr);
    NVISendAllocParam param{};
    param.alias = "Example ROI";
    NVI_SENDER sender = NVISendAlloc(context, &param);
    NVIVideoImageFrame image{};
    image.side.bytes = nullptr;
    image.side.size = 0;
    image.updated = nullptr;
    image.info.codec = NVICodec_AVC;
    image.info.width = 1920u;
    image.info.height = 1080u;
    image.info.frame_rate_num = 25u;
    image.info.frame_rate_den = 1u;
    image.info.rotation = 0u;
    image.info.colorspace.primary = NVIPrimary_BT709;
    image.info.colorspace.transfer = NVITransfer_BT709;
    image.info.colorspace.matrix = NVIMatrix_BT709;
    image.info.colorspace.range = NVIRange_Limited;
    image.info.tick.value = 0u;
    image.info.tick.freq_num = 1u;
    image.info.tick.freq_den = 90000u;
    image.info.time = 0;
    image.buffer.format = NVIPixel_I420;                                                          /// see NVIPixelFormat
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
    wave.buffer.samples = 1920;
    size_t szWaveBuffer = (size_t)wave.buffer.samples * wave.buffer.align * wave.info.channels;
    std::unique_ptr<uint8_t> pWaveBuffer(new uint8_t[szWaveBuffer]);
    memset(pWaveBuffer.get(), 0, szWaveBuffer);
    wave.buffer.data = pWaveBuffer.get();
    wave.buffer.size = szWaveBuffer;
    auto next = std::chrono::steady_clock::now();
    auto freq = std::chrono::milliseconds(40);
    NVIRegionOfInterest roi[2];
    roi[0].label.data = "roi0";
    roi[0].label.length = 4;
    roi[0].x = 0u;
    roi[0].y = 0u;
    roi[0].width = 400;
    roi[0].height = 300;
    roi[1].label.data = "roi1";
    roi[1].label.length = 4;
    roi[1].x = 0u;
    roi[1].y = 0u;
    roi[1].width = 400;
    roi[1].height = 300;
    std::unique_ptr<uint8_t> pSideDataBuffer(new uint8_t[512]);
    while (true)
    {
        if (next <= std::chrono::steady_clock::now())
        {
            next += freq;
            image.info.time = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
            wave.info.time = image.info.time;
            {
                const size_t szSideData = NVIMetaSetupROITable(roi, 2, pSideDataBuffer.get(), 512);
                image.side.bytes = pSideDataBuffer.get();
                image.side.size = szSideData;
                roi[0].x += 10;
                if (roi[0].x + roi[0].width > image.info.width)
                {
                    roi[0].x = 0;
                }
                roi[1].y += 10;
                if (roi[1].y + roi[1].height > image.info.height)
                {
                    roi[1].y = 0;
                }
            }
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
    SendVideoROI();
    return 0;
}
