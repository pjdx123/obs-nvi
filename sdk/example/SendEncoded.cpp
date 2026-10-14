#include <thread>
#include <chrono>
#include <string>
#include <memory>
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
  param.alias = "NVI-Example-SendEncoded";
  param.local = "";
  param.remote = "";
  NVI_SENDER sender = NVISendAlloc(context, &param);

3. 准备好音视频数据，按帧率循环发送：
  NVIVideoEncodedPacket video;
  NVIAudioEncodedPacket audio;
  /// EncodedPacket中有对应的媒体信息需要准确填写
  while (true)
  {
    if (wait_next_av(video, audio))
    {
      if (NVISendVideoEncoded(sender, &video) < 0)
      {
        break;
      }
      if (NVISendAudioEncoded(sender, &audio) < 0)
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
1. 一路信号需要固定的一个线程中调用NVISendVideoEncoded，NVISendAudioEncoded。

*/

constexpr size_t kBitstreamBufferSize = 4ull * 1024 * 1024;

// you can export test stream file with ffmpeg
// ffmpeg -i "xxx" -an -c:v libx264 -g 25 -aud 1 -bf 0 -frames 100 xxx.h264
bool ReadNextPacket(uint8_t* pBuffer, size_t& szBuffer, std::ifstream& ifs, bool& isIntra)
{
    isIntra = false;
    szBuffer = 0ull;
    ifs.read(reinterpret_cast<char*>(pBuffer), 2048ull);
    size_t szRead = ifs.gcount();
    if (szRead > 5ull && pBuffer[0] == 0 && pBuffer[1] == 0 && pBuffer[2] == 0 && pBuffer[3] == 1)
    {
        if (szRead + szBuffer > kBitstreamBufferSize)
        {
            return false;
        }
        size_t szEnd = szRead;
        size_t index = 5ull;
        while (true)
        {
            for (; index < szEnd;)
            {
                if ((szEnd - index) >= 5)
                {
                    if (pBuffer[index] == 0 && pBuffer[index + 1] == 0 && (pBuffer[index + 2] == 1 || (pBuffer[index + 2] == 0 && pBuffer[index + 3] == 1)))
                    {
                        // found next nalu;
                        size_t szStartCodeLen = pBuffer[index + 2] == 1 ? 3 : 4;
                        uint8_t typ = pBuffer[index + szStartCodeLen] & 0x1f;
                        if (typ == 5)
                        {
                            isIntra = true;
                        }
                        else if (typ == 9)
                        {
                            //must have AUD
                            szBuffer = index;
                            ifs.seekg(std::streamoff(szEnd - index) * -1, std::ios_base::cur);
                            return true;
                        }
                        index += szStartCodeLen + 1;
                    }
                    else
                    {
                        ++index;
                    }
                }
                else
                {
                    //read next block
                    break;
                }
            }
            if (ifs.eof())
            {
                szBuffer = szEnd;
                ifs.clear();
                ifs.seekg(0, std::ios_base::beg);
                return true;
            }
            ifs.read(reinterpret_cast<char*>(pBuffer + szEnd), 2048ull);
            szRead = ifs.gcount();
            szEnd += szRead;
        }
    }
    else
    {
        return false;
    }
}

// you can export test stream file with ffmpeg, for example:
// ffmpeg -i "d:\media\prores.mov" -pix_fmt yuv420p10le -c:v hevc_nvenc -vframes 500 -b:v 20000k -g 50 -bf 0 -slices 4 -bsf:v hevc_mp4toannexb -aud 1
// f:\output.hevc
bool ReadNextH265Packet(uint8_t* pBuffer, size_t& szBuffer, std::ifstream& ifs, bool& isIntra)
{
    isIntra = false;
    szBuffer = 0ull;
    ifs.read(reinterpret_cast<char*>(pBuffer), 2048ull);
    size_t szRead = ifs.gcount();
    if (szRead > 5ull && pBuffer[0] == 0 && pBuffer[1] == 0 && pBuffer[2] == 0 && pBuffer[3] == 1)
    {
        if (szRead + szBuffer > kBitstreamBufferSize)
        {
            return false;
        }
        size_t szEnd = szRead;
        size_t index = 5ull;
        while (true)
        {
            for (; index < szEnd;)
            {
                if ((szEnd - index) >= 6)
                {
                    if (pBuffer[index] == 0 && pBuffer[index + 1] == 0 && (pBuffer[index + 2] == 1 || (pBuffer[index + 2] == 0 && pBuffer[index + 3] == 1)))
                    {
                        // found next nalu;
                        size_t szStartCodeLen = pBuffer[index + 2] == 1 ? 3 : 4;
                        uint8_t typ = (pBuffer[index + szStartCodeLen] & 0x7e) >> 1;
                        if (typ == 19)
                        {
                            isIntra = true;
                        }
                        else if (typ == 35)
                        {
                            //must have AUD
                            szBuffer = index;
                            ifs.seekg(std::streamoff(szEnd - index) * -1, std::ios_base::cur);
                            return true;
                        }
                        index += szStartCodeLen + 2;
                    }
                    else
                    {
                        ++index;
                    }
                }
                else
                {
                    //read next block
                    break;
                }
            }
            if (ifs.eof())
            {
                szBuffer = szEnd;
                ifs.clear();
                ifs.seekg(0, std::ios_base::beg);
                return true;
            }
            ifs.read(reinterpret_cast<char*>(pBuffer + szEnd), 2048ull);
            szRead = ifs.gcount();
            szEnd += szRead;
        }
    }
    else
    {
        return false;
    }
}

void SendH264Bitstream(const std::string& strBinFile)
{
    std::ifstream ifs(strBinFile, std::ios::binary);
    NVI_CONTEXT context = NVIContextCreate(nullptr);
    NVISendAllocParam param{};
    param.alias = "Example";
    NVI_SENDER sender = NVISendAlloc(context, &param);
    NVIVideoEncodedPacket packet{};
    packet.side.bytes = nullptr;
    packet.side.size = 0;
    packet.info.codec = NVICodec_AVC;
    packet.info.width = 1920u;
    packet.info.height = 1080u;
    packet.info.frame_rate_num = 25u;
    packet.info.frame_rate_den = 1u;  // 25fps
    packet.info.rotation = 0u;
    packet.info.frame_kind = NVIFrameKind_Intra;
    packet.info.colorspace.primary = NVIPrimary_BT709;
    packet.info.colorspace.transfer = NVITransfer_BT709;
    packet.info.colorspace.matrix = NVIMatrix_BT709;
    packet.info.colorspace.range = NVIRange_Limited;
    packet.info.tick.value = 0u;
    packet.info.tick.freq_num = 1u;
    packet.info.tick.freq_den = 90000u;
    packet.info.time = 0;
    packet.slice_mode = 0;
    packet.slice_count = 1;
    packet.slice_offset = 0;
    packet.slice_number = 1;
    packet.pixel_format = NVIPixel_I420;
    std::unique_ptr<uint8_t[]> pBuffer(new uint8_t[kBitstreamBufferSize]);
    packet.buffer.bytes = pBuffer.get();
    auto next = std::chrono::steady_clock::now();
    auto freq = std::chrono::milliseconds(40);
    bool isIntra = false;
    if (ReadNextPacket(pBuffer.get(), packet.buffer.size, ifs, isIntra))
    {
        while (true)
        {
            if (next <= std::chrono::steady_clock::now())
            {
                next += freq;
                packet.info.frame_kind = isIntra ? NVIFrameKind_Intra : NVIFrameKind_Delta;
                if (NVISendVideoEncoded(sender, &packet) < 0)
                {
                    break;
                }
                packet.info.tick.value += packet.info.tick.freq_den / packet.info.frame_rate_num * packet.info.frame_rate_den;
                if (!ReadNextPacket(pBuffer.get(), packet.buffer.size, ifs, isIntra))
                {
                    break;
                }
            }
            else
            {
                std::this_thread::sleep_until(next);
            }
        }
    }
    NVISendFree(sender);
    NVIContextDestory(context);
}

void SendH265Bitstream(const std::string& strBinFile)
{
    std::ifstream ifs(strBinFile, std::ios::binary);
    NVI_CONTEXT context = NVIContextCreate(nullptr);
    NVISendAllocParam param{};
    param.alias = "Example";
    NVI_SENDER sender = NVISendAlloc(context, &param);
    NVIVideoEncodedPacket packet{};
    packet.side.bytes = nullptr;
    packet.side.size = 0;
    packet.info.codec = NVICodec_HEVC;
    packet.info.width = 3840u;
    packet.info.height = 2160u;
    packet.info.frame_rate_num = 50u;
    packet.info.frame_rate_den = 1u;  // 50fps
    packet.info.rotation = 0u;
    packet.info.frame_kind = NVIFrameKind_Intra;
    packet.info.colorspace.primary = NVIPrimary_BT2020;
    packet.info.colorspace.transfer = NVITransfer_ARIB_STD_B67;
    packet.info.colorspace.matrix = NVITransfer_LOG;
    packet.info.colorspace.range = NVIRange_Limited;
    packet.info.tick.value = 0u;
    packet.info.tick.freq_num = 1u;
    packet.info.tick.freq_den = 90000u;
    packet.info.time = 0;
    packet.slice_mode = 0;
    packet.slice_count = 1;
    packet.slice_offset = 0;
    packet.slice_number = 1;
    packet.pixel_format = NVIPixel_I420;
    std::unique_ptr<uint8_t[]> pBuffer(new uint8_t[kBitstreamBufferSize]);
    packet.buffer.bytes = pBuffer.get();
    auto next = std::chrono::steady_clock::now();
    auto freq = std::chrono::milliseconds(20);
    bool isIntra = false;
    if (ReadNextH265Packet(pBuffer.get(), packet.buffer.size, ifs, isIntra))
    {
        while (true)
        {
            if (next <= std::chrono::steady_clock::now())
            {
                next += freq;
                packet.info.frame_kind = isIntra ? NVIFrameKind_Intra : NVIFrameKind_Delta;
                if (NVISendVideoEncoded(sender, &packet) < 0)
                {
                    break;
                }
                packet.info.tick.value += packet.info.tick.freq_den / packet.info.frame_rate_num * packet.info.frame_rate_den;
                if (!ReadNextH265Packet(pBuffer.get(), packet.buffer.size, ifs, isIntra))
                {
                    break;
                }
            }
            else
            {
                std::this_thread::sleep_until(next);
            }
        }
    }
    NVISendFree(sender);
    NVIContextDestory(context);
}

int main(int argc, char** argv)
{
    if (argc > 1 && argv[1])
    {
        // check h264 or h265
        std::ifstream ifs(argv[1], std::ios::binary);
        char pBuffer[10] = {0};
        ifs.read(reinterpret_cast<char*>(pBuffer), 10ull);
        size_t szRead = ifs.gcount();
        ifs.close();
        if (szRead > 5ull && pBuffer[0] == 0 && pBuffer[1] == 0 && pBuffer[2] == 0 && pBuffer[3] == 1)
        {
            if (0x09 == pBuffer[4])  // H264 AUD
            {
                SendH264Bitstream(argv[1]);
            }
            else
            {
                SendH265Bitstream(argv[1]);
            }
        }
        else
        {
            return -1;
        }
    }
    else
    {
        SendH264Bitstream("video.h264");
    }
    return 0;
}
