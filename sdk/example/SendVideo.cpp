#include <thread>
#include <chrono>
#include <string>
#include <iostream>
#include <fstream>
#include <cstring>
#include <NVI/API.h>

/*

NVI 发送示例：

1. 先创建一个NVI上下文，通常一个进程只有一个上下文实例：
  /// 不要忘了 #include <NVI/API.h>
  NVI_CONTEXT context = NVIContextCreate(nullptr);

2. 配置发送接口：
  NVISendAllocParam param;
  param.alias = "NVI-BC";
  param.local = "";
  param.remote = "";
  NVI_SENDER sender = NVISendAlloc(context, &param);

3. 准备好音视频数据，按帧率循环发送：
  NVIVideoImageFrame image;
  NVIAudioWaveFrame wave;
  /// image和wave 中有对应的媒体信息需要准确填写
  while (true)
  {
    if (wait_next_av(image, wave))
    {
      if (NVISendVideo(sender, &image) < 0)
      {
        break;
      }
      if (NVISendAudio(sender, &wave) < 0)
      {
        break;
      }
    }
    else
    {
      break;
    }
  }

4. 结束时不要忘了释放资源：
  NVISendFree(sender);
  NVIContextDestory(context);


**注意**
1. 一路信号需要固定的一个线程中调用NVISendVideo，NVISendAudio。

*/

void SendVideo()
{
    NVI_CONTEXT context = NVIContextCreate(nullptr);
    NVISendAllocParam param{};
    param.alias = "Example";
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
    wave.buffer.samples = wave.info.sample_rate / image.info.frame_rate_num;
    size_t szWaveBuffer = (size_t)wave.buffer.samples * wave.buffer.align * wave.info.channels;
    std::unique_ptr<uint8_t> pWaveBuffer(new uint8_t[szWaveBuffer]);
    memset(pWaveBuffer.get(), 0, szWaveBuffer);
    wave.buffer.data = pWaveBuffer.get();
    wave.buffer.size = szWaveBuffer;
    auto next = std::chrono::steady_clock::now();
    auto freq = std::chrono::milliseconds(1000 / image.info.frame_rate_num);
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
            image.info.tick.value += image.info.tick.freq_den / image.info.frame_rate_num;
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
    SendVideo();
    return 0;
}
