#include <thread>
#include <chrono>
#include <string>
#include <cstring>
#include <NVI/API.h>

void SendYUV422HF()
{
    NVI_CONTEXT context = NVIContextCreate(nullptr);
    NVISendAllocParam param{};
    param.alias = "Example NVI_YUV422_HF";
    param.caps_proxy_video = 1;  ///! Enable proxy video stream.
    NVI_SENDER sender = NVISendAlloc(context, &param);
    NVIVideoImageFrame image{};
    image.side.bytes = nullptr;
    image.side.size = 0;
    image.updated = nullptr;
    image.info.codec = NVICodec_HF;
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
    image.buffer.format = NVIPixel_422P;                                             /// see NVIPixelFormat
    image.buffer.type = NVIBuffer_HOST;                                              /// see NVIBufferType
    size_t szImageBuffer = (size_t)image.info.width * image.info.height;             //Y
    szImageBuffer += (((size_t)image.info.width + 1) >> 1) * image.info.height * 2;  //UV
    std::unique_ptr<uint8_t> pImageBuffer(new uint8_t[szImageBuffer]);
    {
        // Replace with your code for reading images!
        memset(pImageBuffer.get(), 0, szImageBuffer);
        image.buffer.strides[0] = image.info.width;
        image.buffer.strides[1] = (image.info.width + 1) >> 1;
        image.buffer.strides[2] = (image.info.width + 1) >> 1;
        image.buffer.strides[3] = 0;
        image.buffer.planes[0] = pImageBuffer.get();
        image.buffer.planes[1] = image.buffer.planes[0] + (size_t)image.buffer.strides[0] * image.info.height;
        image.buffer.planes[2] = image.buffer.planes[1] + (size_t)image.buffer.strides[1] * image.info.height;
        image.buffer.planes[3] = nullptr;
    }
    auto next = std::chrono::steady_clock::now();
    auto freq = std::chrono::milliseconds(40);
    while (true)
    {
        if (next <= std::chrono::steady_clock::now())
        {
            next += freq;
            image.info.time = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
            if (NVISendVideo(sender, &image) < 0)
            {
                break;
            }
            image.info.tick.value += 3600;
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
    SendYUV422HF();
    return 0;
}
