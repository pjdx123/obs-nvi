#include <NVI/API.h>
#include <cstdlib>
#include <string>
#include <iostream>

#ifdef _nlohmann_json
#define HAS_JSON_PARSER 1
#include <nlohmann/json.hpp>
using json = nlohmann::json;
#endif

/*

**注意**
NVIDeviceHandler相关的接口必须都在一个线程中调用。

NVI 设备事件处理示例：

1. 先创建一个NVI上下文，通常一个进程只有一个上下文实例：
  /// 不要忘了 #include <NVI/API.h>
  NVI_CONTEXT context = NVIContextCreate(nullptr);

2. 发送流
  NVISendAllocParam send{};
  send.alias = "PTZ Handler";
  send.caps_ptz = 1;
  NVI_SENDER sender = NVISendAlloc(context, &send);

3. 配置设备控制接口
  NVIDeviceHandlerAllocParam param{};
  param.sender = sender; // 或者可以配置成 nullptr
  param.caps_ptz1 = 1;
  NVI_DEVICE_HANDLER handler = NVIDeviceHandlerAlloc(context, &param);

4. 等待信号：
  NVIDeviceHandlerEventParam ep{};
  ep.timeout_ms = 10;//
  while (handler)
  {
    int nEvent = NVIDeviceHandlerEvent(handler, &ep);
    if (nEvent < 0)
    {
      break;
    }
    else
    {
      if (nEvent & NVIDeviceEvent_PTZ)
      {
        NVIDataPTZ1 ptz{};
        NVIDeviceHandlerPTZ1(handler, &ptz);
        // parse json data
        // do something
      }
      if (nEvent & NVIDeviceEvent_Tally)
      {
        NVITally tally{};
        NVIDeviceHandlerTally(handler, &tally);
        // do something
      }

      // or do something
    }
  }

5. 结束时不要忘了释放资源：
  NVIDeviceControllerFree(controller);
  NVIContextDestory(context);

*/

void PTZHandle()
{
    NVI_CONTEXT context = NVIContextCreate(nullptr);

    NVISendAllocParam send{};
    send.alias = "PTZ Handler";
    send.caps_ptz = 1;
    NVI_SENDER sender = NVISendAlloc(context, &send);

    NVIDeviceHandlerAllocParam param{};
    param.sender = sender;  // 或者可以配置成 nullptr
    param.caps_ptz1 = 1;
    NVI_DEVICE_HANDLER handler = NVIDeviceHandlerAlloc(context, &param);

    NVIDeviceHandlerEventParam ep{};
    ep.timeout_ms = 10;
    while (handler)
    {
        int nEvent = NVIDeviceHandlerEvent(handler, &ep);
        if (nEvent < 0)
        {
            break;
        }
        if (nEvent & NVIDeviceEvent_PTZ)
        {
            NVIDataPTZ1 ptz{};
            NVIDeviceHandlerPTZ1(handler, &ptz);
            std::string strPTZ((const char*)ptz.bytes, ptz.size);
            std::cout << "New ptz: " << strPTZ << std::endl;
#ifdef HAS_JSON_PARSER
            try
            {
                auto object = json::parse(strPTZ);
                const auto& ptz = object["NVI_PTZ"];
                for (auto it = ptz.begin(); it != ptz.end(); ++it)
                {
                    const auto& key = it.key();
                    if (it.key() == "ptz_zoom")
                    {
                        const float fZoom = it.value()["zoom"];
                        std::cout << "PTZ-Zoom=" << fZoom << std::endl;
                    }
                    if (it.key() == "ptz_zoom_speed")
                    {
                        const float fZoomSpeed = it.value()["zoom_speed"];
                        std::cout << "PTZ-ZoomSpeed=" << fZoomSpeed << std::endl;
                    }
                    if (it.key() == "ptz_pan_tilt")
                    {
                        const float fPan = it.value()["pan"];
                        const float fTilt = it.value()["tilt"];
                        std::cout << "PTZ-Pan=" << fPan << " PTZ-Tilt=" << fTilt << std::endl;
                    }
                    if (it.key() == "ptz_pan_tilt_speed")
                    {
                        const float fPanSpeed = it.value()["pan_speed"];
                        const float fTiltSpeed = it.value()["tilt_speed"];
                        std::cout << "PTZ-PanSpeed=" << fPanSpeed << " PTZ-TiltSpeed=" << fTiltSpeed << std::endl;
                    }
                    if (it.key() == "ptz_store_preset")
                    {
                        const uint32_t uPresetNo = it.value()["index"];
                        std::cout << "PTZ-PresetNo=" << uPresetNo << std::endl;
                    }
                    if (it.key() == "ptz_store_preset")
                    {
                        const uint32_t uPresetNo = it.value()["index"];
                        const float fSpeed = it.value()["speed"];
                        std::cout << "PTZ-PresetNo=" << uPresetNo << " PTZ-PresetSpeed=" << fSpeed << std::endl;
                    }
                    if (it.key() == "ptz_focus")
                    {
                        const std::string strMode = it.value()["mode"];
                        if (strMode == "manual")
                        {
                            const float fDistance = it.value()["distance"];
                            std::cout << "PTZ-FocusDistance=" << fDistance << std::endl;
                        }
                        else
                        {
                            std::cout << "PTZ-Focus " << strMode << std::endl;
                        }
                    }
                    if (it.key() == "ptz_focus")
                    {
                        const float fDistance = it.value()["distance"];
                        std::cout << "PTZ-FocusSpeed=" << fDistance << std::endl;
                    }
                    if (it.key() == "ptz_focus")
                    {
                        const std::string strMode = it.value()["mode"];
                        if (strMode == "manual")
                        {
                            const float fRed = it.value()["red"];
                            const float fBlue = it.value()["blue"];
                            std::cout << "PTZ-WhiteBalance Red=" << fRed << " Blue=" << fBlue << std::endl;
                        }
                        else
                        {
                            std::cout << "PTZ-WhiteBalance " << strMode << std::endl;
                        }
                    }
                    if (it.key() == "ptz_exposure")
                    {
                        const std::string strMode = it.value()["mode"];
                        if (strMode == "manual")
                        {
                            const float fIris = it.value()["iris"];
                            const float fGain = it.value()["gain"];
                            const float fShutter = it.value()["shutter"];
                            std::cout << "PTZ-WhiteBalance Iris=" << fIris << " Gain=" << fGain << " Shutter=" << fShutter << std::endl;
                        }
                        else
                        {
                            std::cout << "PTZ-Exposure " << strMode << std::endl;
                        }
                    }
                }
            }
            catch (const std::exception& e)
            {
                std::cout << "Parse PTZ failed!!!" << std::endl;
                break;
            }
#endif
        }
        if (nEvent & NVIDeviceEvent_Tally)
        {
            NVITally tally{};
            NVIDeviceHandlerTally(handler, &tally);
            std::cout << "New tally preview:" << unsigned(tally.preview) << " program:" << unsigned(tally.program) << std::endl;
        }
    }
    NVIDeviceControllerFree(handler);
    NVIContextDestory(context);
}

int main(int argc, char** argv)
{
    PTZHandle();
    return 0;
}
