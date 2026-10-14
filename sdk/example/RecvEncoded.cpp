#include <NVI/API.h>
#include <iostream>
#include <thread>
#include <vector>
#include <chrono>

using namespace std::chrono_literals;

/*

**注意**
目前的发送接口还没有支持多线程安全，请在固定的一个线程中调用。

NVI 发送示例：

1. 先创建一个NVI上下文，通常一个进程只有一个上下文实例：
  /// 不要忘了 #include <NVI/API.h>
  NVI_CONTEXT context = NVIContextCreate(nullptr);

2. 循环获取网络中的媒体流信息（或者每隔一段时间获取一次）：
  std::vector<NVINetworkStream> streams;
  streams.resize(1);
  NVINetworkEnumParam enumparam{};
  enumparam.streams = streams.data();
  enumparam.streams_size = (uint32_t)streams.size();
  enumparam.timeout_ms = 500;
  while (true)
  {
      if (NVINetworkEnumStream(context, &enumparam) > 0)
      {
          break;
      }
  }

3. 配置接收的媒体流uri：
  NVIRecvAllocParam param{};
  param.local = nullptr;
  param.remote = streams[0].uri;
  NVI_RECVER recver = NVIRecvAlloc(context, &param);

4. 循环接收音视频数据：
  while (true)
  {
    NVIRecvFrameOut param{};
    param.timeout_ms = 40;
    int nError = NVIRecvEncoded(recver, &param);
    if (nError < 0)
    {
      std::cout << "Recv Error " << nError << std::endl;
      break;
    }
    else
    {
      if (param.image_out)
      {
          std::cout << "Recv Video Image #" << param.image_out->info.tick.value << std::endl;
      }
      if (param.wave_out)
      {
          std::cout << "Recv Audio Wave #" << param.wave_out->info.tick.value << std::endl;
      }
      /// NVIRecvFrame获取的数据在下次调用是会释放，
      /// 请不要直接引用它返回的指针。
      render_av(param.image_out, param.wave_out);
    }
  }

5. 结束时不要忘了释放资源：
  NVIRecvFree(sender);
  NVIContextDestory(context);

*/

void RecvEncoded()
{
    NVI_CONTEXT context = NVIContextCreate(nullptr);
    std::vector<NVINetworkStream> streams;
    streams.resize(1);
    NVINetworkEnumParam enumparam{};
    enumparam.streams = streams.data();
    enumparam.streams_size = (uint32_t)streams.size();
    enumparam.timeout_ms = 10;
    enumparam.filter = nullptr;
    while (true)
    {
        if (NVINetworkEnumStream(context, &enumparam) > 0)
        {
            break;
        }
        else
        {
            std::this_thread::sleep_for(10ms);
        }
    }
    NVIRecvAllocParam param{};
    param.local = nullptr;
    param.remote = streams[0].uri;
    NVI_RECVER recver = NVIRecvAlloc(context, &param);
    while (true)
    {
        NVIRecvEncodedOut param{};
        param.timeout_ms = 40;
        int nError = NVIRecvEncoded(recver, &param);
        if (nError < 0)
        {
            std::cout << "Recv Error " << nError << std::endl;
            break;
        }
        else
        {
            if (param.video_out || param.audio_out)
            {
                if (param.video_out)
                {
                    std::cout << "Recv Video #" << param.video_out->info.tick.value << std::endl;
                }
                if (param.audio_out)
                {
                    std::cout << "Recv Audio #" << param.audio_out->info.tick.value << std::endl;
                }
                /*
                {
                    /// 可以立即调用NVIRecvFrame获取解码后的音频或者图像
                    /// 注意：这时不一定可以正确解析出图像，视频需要等到关键帧后才能解码
                    NVIRecvFrameOut frame{};
                    nError = NVIRecvFrame(recver, &frame);
                    if (nError < 0)
                    {
                        std::cout << "Decode Error " << nError << std::endl;
                        break;
                    }
                }*/
                /*或者只使用码流，不解码.*/
            }
        }
    }
    NVIRecvFree(recver);
    NVIContextDestory(context);
}

int main(int argc, char** argv)
{
    RecvEncoded();
    return 0;
}