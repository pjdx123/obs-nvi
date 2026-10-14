#include <NVI/API.h>
#include <cstdio>
#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <algorithm>
#if _WIN32
#include <conio.h>
#else
#include <termios.h>

static struct termios old, current;

/* Initialize new terminal i/o settings */
void InitTermios(int echo)
{
    tcgetattr(0, &old);         /* grab old terminal i/o settings */
    current = old;              /* make new settings same as old settings */
    current.c_lflag &= ~ICANON; /* disable buffered i/o */
    if (echo)
    {
        current.c_lflag |= ECHO; /* set echo mode */
    }
    else
    {
        current.c_lflag &= ~ECHO; /* set no echo mode */
    }
    tcsetattr(0, TCSANOW, &current); /* use these new terminal i/o settings now */
}

void ResetTermios(void)
{
    tcsetattr(0, TCSANOW, &old);
}

int getch(void)
{
    int ch = 0;
    InitTermios(0);
    ch = getchar();
    ResetTermios();
    return ch;
}
#endif

using namespace std::chrono_literals;

/*

**注意**
NVIDeviceController相关的接口必须都在一个线程中调用。

NVI 设备控制示例：

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

3. 配置设备控制接口
  NVIDeviceControllerAllocParam param{};
  param.instance = streams[0].instance;
  param.number = streams[0].number;
  NVI_DEVICE_CONTROLLER controller = NVIDeviceControllerAlloc(context, &param);

4. 发送控制信号：
  // 可以先用 NVIDeviceControllerPtzIsSupport(controller) 判断设备是否支持PTZ。

  float zoom = 0.0f
  while (true)
  {
    int signal = wait_for_user_input();
    if (signal == ZOOM_IN)
    {
      zoom -= 0.1f; // 或者其它值
      NVIDeviceControllerPtzZoom(controller, zoom);
    }
    else if (signal == ZOOM_OUT)
    {
      zoom += 0.1f; // 或者其它值
      NVIDeviceControllerPtzZoom(controller, zoom);
    }
  }

5. 结束时不要忘了释放资源：
  NVIDeviceControllerFree(controller);
  NVIContextDestory(context);

*/

void PTZControl()
{
    NVI_CONTEXT context = NVIContextCreate(nullptr);
    std::vector<NVINetworkStream> streams;
    streams.resize(64);
    NVINetworkEnumParam enumparam{};
    enumparam.streams = streams.data();
    enumparam.streams_size = (uint32_t)streams.size();
    enumparam.timeout_ms = 10;
    enumparam.filter = nullptr;
    std::cout << "Begin enum network stream >" << std::endl;
    int32_t nSelected = 0;
    while (true)
    {
        const int32_t nEnum = NVINetworkEnumStream(context, &enumparam);
        if (nEnum > 0)
        {
            for (; nSelected < nEnum; ++nSelected)
            {
                if (streams[nSelected].caps_ptz == 1)
                {
                    break;
                }
            }
            break;
        }
        else
        {
            std::this_thread::sleep_for(10ms);
        }
    }
    if (nSelected >= 0)
    {
        std::cout << "Selected stream " << streams[nSelected].alias << std::endl;
        NVIDeviceControllerAllocParam param{};
        param.instance = streams[nSelected].instance;
        param.number = streams[nSelected].number;
        NVI_DEVICE_CONTROLLER controller = NVIDeviceControllerAlloc(context, &param);
        bool exit = false;
        float fZoom = 0.0f;
        float fPan = 0.0f;
        float fTilt = 0.0f;
        float fFocus = 0.0f;
        while (controller && !exit)
        {
            int input = getch();
            switch ((char)input)
            {
            case 'q': exit = true; break;
            case 'w':
                fTilt = (std::min)(1.0f, fTilt + 0.1f);
                NVIDeviceControllerPtzPanTilt(controller, fPan, fTilt);
                break;  // up
            case 's':
                fTilt = (std::max)(-1.0f, fTilt - 0.1f);
                NVIDeviceControllerPtzPanTilt(controller, fPan, fTilt);
                break;  // down
            case 'a':
                fPan = (std::max)(-1.0f, fPan - 0.1f);
                NVIDeviceControllerPtzPanTilt(controller, fPan, fTilt);
                break;  // left
            case 'd':
                fPan = (std::min)(1.0f, fPan + 0.1f);
                NVIDeviceControllerPtzPanTilt(controller, fPan, fTilt);
                break;  // right
            case '+':
                fZoom = (std::min)(1.0f, fZoom + 0.1f);
                NVIDeviceControllerPtzZoom(controller, fZoom);
                break;  // zoom out
            case '-':
                fZoom = (std::max)(0.0f, fZoom - 0.1f);
                NVIDeviceControllerPtzZoom(controller, fZoom);
                break;  // zoom in
            case ' ': NVIDeviceControllerPtzAutoFocus(controller); break;
            case 'f':
                fFocus = (std::min)(1.0f, fFocus + 0.1f);
                NVIDeviceControllerPtzFocus(controller, fFocus);
                break;  // zoom in
            case 'g':
                fFocus = (std::max)(0.0f, fFocus - 0.1f);
                NVIDeviceControllerPtzFocus(controller, fFocus);
                break;  // zoom in
            default: break;
            }
        }
        NVIDeviceControllerFree(controller);
    }
    NVIContextDestory(context);
}

int main(int argc, char** argv)
{
    PTZControl();
    return 0;
}
