#pragma once

#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <wrl/client.h>

// Optional hardware check only. The ordinary app test does not create this
// probe or place an output device. Keep the default endpoint quiet while using it.
class WindowsPlaybackProbe {
public:
  WindowsPlaybackProbe() {
    require(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)), "COM initialization failed");
    require(SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
        IID_PPV_ARGS(&enumerator_))), "cannot enumerate the default device");
    require(SUCCEEDED(enumerator_->GetDefaultAudioEndpoint(eRender, eConsole, &device_)),
            "no Windows default rendering device");
    require(SUCCEEDED(device_->Activate(__uuidof(IAudioMeterInformation), CLSCTX_ALL,
        nullptr, reinterpret_cast<void**>(meter_.GetAddressOf()))), "endpoint meter unavailable");
    Microsoft::WRL::ComPtr<IAudioEndpointVolume> volume;
    require(SUCCEEDED(device_->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL,
        nullptr, reinterpret_cast<void**>(volume.GetAddressOf()))), "endpoint volume unavailable");
    BOOL muted = TRUE;
    float level = 0;
    require(SUCCEEDED(volume->GetMute(&muted)) &&
        SUCCEEDED(volume->GetMasterVolumeLevelScalar(&level)), "cannot read endpoint volume");
    require(!muted && level > 0, "default output is muted or at zero volume; no settings changed");
  }
  ~WindowsPlaybackProbe() {
    meter_.Reset(); device_.Reset(); enumerator_.Reset(); CoUninitialize();
  }
  // Return early on a signal so Stop is checked while the fixture is sounding.
  float peak(HWND window, int milliseconds, float returnAbove = 1.0f) {
    float maximum = 0;
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
    while (std::chrono::steady_clock::now() < end) {
      float value = 0;
      require(SUCCEEDED(meter_->GetPeakValue(&value)), "endpoint meter failed");
      maximum = std::max(maximum, value);
      send(window, WM_NULL); // the GUI must still answer while device buffers wait
      if (value > returnAbove) break;
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return maximum;
  }
private:
  Microsoft::WRL::ComPtr<IMMDeviceEnumerator> enumerator_;
  Microsoft::WRL::ComPtr<IMMDevice> device_;
  Microsoft::WRL::ComPtr<IAudioMeterInformation> meter_;
};
