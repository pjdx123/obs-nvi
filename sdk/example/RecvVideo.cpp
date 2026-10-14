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
    int nError = NVIRecvFrame(recver, &param);
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
            if (node.defined == NVIMeta_ROI)
            {
                size_t szROILength = NVIMetaParseROILength(node.value, node.length);
                if (szROILength > 0)
                {
                    std::vector<NVIRegionOfInterest> vecROI;
                    vecROI.resize(szROILength);
                    int32_t nValidLength = NVIMetaParseROITable(node.value, node.length, vecROI.data(), vecROI.size());
                    for (int32_t i = 0; i < nValidLength; ++i)
                    {
                        const auto& roi = vecROI[i];
                        printf("ROI[%s{%u+%u,%u+%u}] ", std::string(roi.label.data, roi.label.length).c_str(), roi.x, roi.width, roi.y, roi.height);
                    }
                }
            }
        }
        else
        {
            break;
        }
    }
}

void RecvVideo()
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
        NVIRecvFrameOut param{};
        param.timeout_ms = 40;
        int nError = NVIRecvFrame(recver, &param);
        if (nError < 0)
        {
            std::cout << "Recv Error " << nError << std::endl;
            break;
        }
        else
        {
            if (param.image_out)
            {
                std::cout << "Recv Video Image #" << param.image_out->info.tick.value << " ";
                ParseSideData(param.image_out->side);
                std::cout << std::endl;
            }
            if (param.wave_out)
            {
                std::cout << "Recv Audio Wave #" << param.wave_out->info.tick.value << std::endl;
            }
        }
    }
    NVIRecvFree(recver);
    NVIContextDestory(context);
}

int main(int argc, char** argv)
{
    RecvVideo();
    return 0;
}