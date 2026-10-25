#include "Hmd.hpp"

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <string>

#include "prx/libc/include/General.hpp"
#include "prx/libSceUserService/UserService.hpp"

// Without a headset the title is told none is connected. RYGT_VR=1 reports a PlayStation VR
// as connected and ready, so the title offers its VR modes; the log then shows each headset
// call it makes. Nothing is shown in a headset yet.

namespace {

std::atomic<bool> initialized{false};
std::atomic<std::int32_t> openedUser{-1};
// The handle sceHmdOpen returns; libSceVrTracker needs it to differ from other devices' handles.
constexpr std::int32_t HeadsetHandle = 0x0f000000;

bool Present() {
    static const bool present = [] {
        const char* value = std::getenv("RYGT_VR");
        const bool on = value != nullptr && std::strcmp(value, "0") != 0;
        if (on) APS5_LOG_ERR("Hmd: RYGT_VR is set, reporting a %s headset as connected", "PlayStation VR");
        return on;
    }();
    return present;
}

// Logs the first calls of each function, so the log shows what the title asks of the headset:
// in stderr.log with RYGT_VR=1, otherwise in stdout.log at the Debug level.
void Called(const char* name, std::uint64_t a0, std::uint64_t a1, std::uint64_t a2, std::uint64_t a3) {
    static std::mutex lock;
    static std::map<std::string, int> counts;
    std::lock_guard guard(lock);
    const int count = ++counts[name];
    if (count > 3) return;
    const auto u = [](std::uint64_t value) { return static_cast<unsigned long long>(value); };
    const char* more = count == 3 ? " (further calls not logged)" : "";
    if (Present()) APS5_LOG_ERR("Hmd: %s(0x%llx, 0x%llx, 0x%llx, 0x%llx)%s", name, u(a0), u(a1), u(a2), u(a3), more);
    else APS5_LOG_OUT("Hmd: %s(0x%llx, 0x%llx, 0x%llx, 0x%llx)%s", name, u(a0), u(a1), u(a2), u(a3), more);
}

void Called(const char* name) {
    Called(name, 0, 0, 0, 0);
}

std::int32_t Initialize(const Hmd::InitializeParam* param) {
    if (initialized.load()) return Hmd::AlreadyInitialized;
    if (!param) return Hmd::ParameterNull;
    if (param->reserved0) return Hmd::UnsupportedFeature;
    return initialized.exchange(true) ? Hmd::AlreadyInitialized : 0;
}

void FillInformation(Hmd::DeviceInformation* info) {
    *info = {};
    if (!Present()) {
        info->status = Hmd::DeviceStatus::NotDetected;
        info->userId = -1;
        return;
    }
    // A PlayStation VR: one 1920x1080 panel shared by both eyes.
    info->status = Hmd::DeviceStatus::Ready;
    const std::int32_t user = openedUser.load();
    info->userId = user != -1 ? user : USER_SERVICE_INITIAL_USER_ID;
    info->panelWidth = 1920;
    info->panelHeight = 1080;
}

}

// The arguments are read as four integer registers whatever the real signature, for the log.
#define HMD_STUB_ARGS std::uint64_t a0, std::uint64_t a1, std::uint64_t a2, std::uint64_t a3
#define HMD_STUB(name)                                \
    int APS5_VABI name(HMD_STUB_ARGS) {               \
        Called(#name, a0, a1, a2, a3);                \
        return Hmd::DeviceDisconnected;               \
    }
// Reprojection setup and per-frame calls that succeed with RYGT_VR=1, so the title goes on to
// hand over its frames (and the log shows how).
#define HMD_STUB_OK_IN_VR(name)                       \
    int APS5_VABI name(HMD_STUB_ARGS) {               \
        Called(#name, a0, a1, a2, a3);                \
        return Present() ? 0 : Hmd::DeviceDisconnected; \
    }

extern "C" {

std::int32_t APS5_VABI sceHmdInternalMmapGetSensorCalibrationData(void* data, std::uint32_t* size) {
    Called(__func__);
    (void)data;
    if (size) *size = 0;
    return Hmd::DeviceDisconnected;
}

std::int32_t APS5_VABI sceHmdInternalGetPUVersion(std::uint32_t* version) {
    Called(__func__);
    if (version) *version = 0;
    return Hmd::DeviceDisconnected;
}

std::int32_t APS5_VABI sceHmdInternalGetDeviceInformation(Hmd::DeviceInformation* info) {
    Called(__func__);
    if (!info) return Hmd::ParameterNull;
    FillInformation(info);
    return 0;
}

std::int32_t APS5_VABI sceHmdInternalMmapGetCount(std::uint32_t* count) {
    Called(__func__);
    if (count) *count = 0;
    return 0;
}

std::int32_t APS5_VABI sceHmdInternalResetLedForVrTracker() {
    Called(__func__);
    return Hmd::DeviceDisconnected;
}

std::int32_t APS5_VABI sceHmdInternalSetUserType(std::int32_t userType) {
    Called(__func__, static_cast<std::uint64_t>(userType), 0, 0, 0);
    return Hmd::DeviceDisconnected;
}

std::int32_t APS5_VABI sceHmdInternalGetHmuSerialNumber(char* serial) {
    Called(__func__);
    if (serial) *serial = '\0';
    return Hmd::DeviceDisconnected;
}

std::int32_t APS5_VABI sceHmdInternalGetEyeStatus(Hmd::DeviceStatus* status) {
    Called(__func__);
    if (status) *status = Hmd::DeviceStatus::NotDetected;
    return Hmd::DeviceDisconnected;
}

std::int32_t APS5_VABI sceHmdGetInertialSensorData(std::int32_t handle, void* data) {
    Called(__func__, static_cast<std::uint64_t>(handle), reinterpret_cast<std::uint64_t>(data), 0, 0);
    return Hmd::DeviceDisconnected;
}

std::int32_t APS5_VABI sceHmdGetFieldOfView(std::int32_t handle, Hmd::FieldOfView* fieldOfView) {
    Called(__func__, static_cast<std::uint64_t>(handle), 0, 0, 0);
    if (!fieldOfView) return Hmd::ParameterNull;
    if (!Present()) return Hmd::DeviceDisconnected;
    if (handle != HeadsetHandle) return Hmd::InvalidHandle;
    // What a PlayStation VR reports.
    *fieldOfView = {1.20743f, 1.181346f, 1.262872f, 1.262872f};
    return 0;
}

std::int32_t APS5_VABI sceHmdGet2DEyeOffset(std::int32_t handle, Hmd::EyeOffset* left, Hmd::EyeOffset* right) {
    Called(__func__, static_cast<std::uint64_t>(handle), 0, 0, 0);
    if (!Present()) return Hmd::DeviceDisconnected;
    if (handle != HeadsetHandle) return Hmd::InvalidHandle;
    if (!left || !right) return Hmd::ParameterNull;
    *left = {};
    *right = {};
    left->x = -0.0315f;
    right->x = 0.0315f;
    return 0;
}

std::int32_t APS5_VABI sceHmdInitialize(const Hmd::InitializeParam* param) {
    Called(__func__);
    Present();
    return Initialize(param);
}

std::int32_t APS5_VABI sceHmdInitialize315(const Hmd::InitializeParam* param) {
    Called(__func__);
    Present();
    return Initialize(param);
}

std::int32_t APS5_VABI sceHmdTerminate() {
    Called(__func__);
    openedUser.store(-1);
    return initialized.exchange(false) ? 0 : Hmd::NotInitialized;
}

std::int32_t APS5_VABI sceHmdOpen(std::int32_t userId, std::int32_t type, std::int32_t index,
                                Hmd::OpenParam* param) {
    Called(__func__, static_cast<std::uint64_t>(userId), static_cast<std::uint64_t>(type), static_cast<std::uint64_t>(index),
        reinterpret_cast<std::uint64_t>(param));
    if (!initialized.load()) return Hmd::NotInitialized;
    if (type != 0 || index != 0 || param || userId == -1 || userId == 0xff)
        return Hmd::ParameterInvalid;
    if (!Present()) return Hmd::DeviceDisconnected;
    std::int32_t none = -1;
    if (!openedUser.compare_exchange_strong(none, userId)) return Hmd::AlreadyOpened;
    return HeadsetHandle;
}

std::int32_t APS5_VABI sceHmdClose(std::int32_t handle) {
    Called(__func__, static_cast<std::uint64_t>(handle), 0, 0, 0);
    if (!initialized.load()) return Hmd::NotInitialized;
    if (!Present() || handle != HeadsetHandle || openedUser.exchange(-1) == -1) return Hmd::InvalidHandle;
    return 0;
}

std::int32_t APS5_VABI sceHmdGetDeviceInformation(Hmd::DeviceInformation* info) {
    Called(__func__);
    if (!info) return Hmd::ParameterNull;
    if (!initialized.load()) return Hmd::NotInitialized;
    FillInformation(info);
    return 0;
}

std::int32_t APS5_VABI sceHmdGetDeviceInformationByHandle(std::int32_t handle,
                                                        Hmd::DeviceInformation* info) {
    Called(__func__, static_cast<std::uint64_t>(handle), 0, 0, 0);
    if (!initialized.load()) return Hmd::NotInitialized;
    if (!Present() || handle != HeadsetHandle || openedUser.load() == -1) return Hmd::InvalidHandle;
    if (!info) return Hmd::ParameterNull;
    FillInformation(info);
    return 0;
}

std::int32_t APS5_VABI sceHmdInternalGetDeviceStatus(Hmd::DeviceStatus* status) {
    Called(__func__);
    if (!status) return Hmd::ParameterNull;
    *status = Present() ? Hmd::DeviceStatus::Ready : Hmd::DeviceStatus::NotDetected;
    return 0;
}

// GT Sport imports this reprojection entry point even when no headset is present.
std::int32_t APS5_VABI sceHmdReprojectionSetOutputMinColor() {
    Called(__func__);
    return 0;
}

// The memory the title sets aside for reprojection (sizes as shadPS4 reports them).
std::int32_t APS5_VABI sceHmdReprojectionQueryGarlicBuffAlign() {
    Called(__func__);
    return Present() ? 0x100 : Hmd::DeviceDisconnected;
}

std::int32_t APS5_VABI sceHmdReprojectionQueryGarlicBuffSize() {
    Called(__func__);
    return Present() ? 0x100000 : Hmd::DeviceDisconnected;
}

std::int32_t APS5_VABI sceHmdReprojectionQueryOnionBuffAlign() {
    Called(__func__);
    return Present() ? 0x100 : Hmd::DeviceDisconnected;
}

std::int32_t APS5_VABI sceHmdReprojectionQueryOnionBuffSize() {
    Called(__func__);
    return Present() ? 0x810 : Hmd::DeviceDisconnected;
}

std::int32_t APS5_VABI sceHmdInternalMmapIsConnect() {
    Called(__func__);
    return Present() ? 1 : 0;
}

}

// The rest of the headset API. Each call is logged with its first four arguments; with no
// headset (and for most functions even with RYGT_VR=1) the title is told it is disconnected.
extern "C" {
HMD_STUB(sceHmd2Close)
HMD_STUB(sceHmd2GazeGetCalibrationData)
HMD_STUB(sceHmd2GazeGetCalibrationDataMaxSize)
HMD_STUB(sceHmd2GazeGetResult)
HMD_STUB(sceHmd2GazeGetResultForFoveatedRendering)
HMD_STUB(sceHmd2GetDeviceInformation)
HMD_STUB(sceHmd2GetDeviceInformationByHandle)
HMD_STUB(sceHmd2GetFieldOfView)
HMD_STUB(sceHmd2GetFieldOfViewWithoutHandle)
HMD_STUB(sceHmd2Initialize)
HMD_STUB(sceHmd2Open)
HMD_STUB(sceHmd2ReprojectionBeginFrame)
HMD_STUB(sceHmd2ReprojectionClearUserEventEnd)
HMD_STUB(sceHmd2ReprojectionClearUserEventStart)
HMD_STUB(sceHmd2ReprojectionDisableMirroring)
HMD_STUB(sceHmd2ReprojectionDisableVrMode)
HMD_STUB(sceHmd2ReprojectionEnableMirroring)
HMD_STUB(sceHmd2ReprojectionEnableVrMode)
HMD_STUB(sceHmd2ReprojectionGetCurrentOutput)
HMD_STUB(sceHmd2ReprojectionGetMirroringDisplayBufferSizeAlign)
HMD_STUB(sceHmd2ReprojectionGetMirroringWorkMemorySizeAlign)
HMD_STUB(sceHmd2ReprojectionGetPredictedDisplayTime)
HMD_STUB(sceHmd2ReprojectionGetStatus)
HMD_STUB(sceHmd2ReprojectionInitialize)
HMD_STUB(sceHmd2ReprojectionQueryBufferSizeAlign)
HMD_STUB(sceHmd2ReprojectionQueryDisplayBufferSizeAlign)
HMD_STUB(sceHmd2ReprojectionSetAllowPositionalReprojection)
HMD_STUB(sceHmd2ReprojectionSetMirroringOption)
HMD_STUB(sceHmd2ReprojectionSetParam)
HMD_STUB(sceHmd2ReprojectionSetParamWithBuffer)
HMD_STUB(sceHmd2ReprojectionSetRenderConfig)
HMD_STUB(sceHmd2ReprojectionSetTiming)
HMD_STUB(sceHmd2ReprojectionSetUserEventEnd)
HMD_STUB(sceHmd2ReprojectionSetUserEventStart)
HMD_STUB(sceHmd2ReprojectionTerminate)
HMD_STUB(sceHmd2SetVibration)
HMD_STUB(sceHmd2Terminate)
HMD_STUB(sceHmdDistortionGet2dVrCommand)
HMD_STUB(sceHmdDistortionGetCompoundEyeCorrectionCommand)
HMD_STUB(sceHmdDistortionGetCorrectionCommand)
HMD_STUB(sceHmdDistortionGetWideNearCorrectionCommand)
HMD_STUB(sceHmdDistortionGetWorkMemoryAlign)
HMD_STUB(sceHmdDistortionGetWorkMemorySize)
HMD_STUB(sceHmdDistortionInitialize)
HMD_STUB(sceHmdDistortionSetOutputMinColor)
HMD_STUB(sceHmdFillDistortionBuffer)
HMD_STUB(sceHmdGet2dDistortionMap)
HMD_STUB(sceHmdGet2dVrCommand)
HMD_STUB(sceHmdGetAssyError)
HMD_STUB(sceHmdGetDistortionCorrectionCommand)
HMD_STUB(sceHmdGetDistortionCorrectionCommand2d)
HMD_STUB(sceHmdGetDistortionCorrectionCommandApprox)
HMD_STUB(sceHmdGetDistortionMap)
HMD_STUB(sceHmdGetDistortionParams)
HMD_STUB(sceHmdGetDistortionWorkMemoryAlign)
HMD_STUB(sceHmdGetDistortionWorkMemoryAlignFor2d)
HMD_STUB(sceHmdGetDistortionWorkMemorySize)
HMD_STUB(sceHmdGetDistortionWorkMemorySizeFor2d)
HMD_STUB(sceHmdGetEyeStatus)
HMD_STUB(sceHmdGetFieldOfView2d)
HMD_STUB(sceHmdGetFieldOfViewApprox)
HMD_STUB(sceHmdGetViewStatus)
HMD_STUB(sceHmdGetWideNearDistortionCorrectionCommand)
HMD_STUB(sceHmdInternal3dAudioClose)
HMD_STUB(sceHmdInternal3dAudioOpen)
HMD_STUB(sceHmdInternal3dAudioSendData)
HMD_STUB(sceHmdInternalAnotherScreenClose)
HMD_STUB(sceHmdInternalAnotherScreenGetAudioStatus)
HMD_STUB(sceHmdInternalAnotherScreenGetFadeState)
HMD_STUB(sceHmdInternalAnotherScreenGetVideoStatus)
HMD_STUB(sceHmdInternalAnotherScreenOpen)
HMD_STUB(sceHmdInternalAnotherScreenSendAudio)
HMD_STUB(sceHmdInternalAnotherScreenSendVideo)
HMD_STUB(sceHmdInternalAnotherScreenSetFadeAndSwitch)
HMD_STUB(sceHmdInternalBindDeviceWithUserId)
HMD_STUB(sceHmdInternalCheckDeviceModelMk3)
HMD_STUB(sceHmdInternalCheckS3dPassModeAvailable)
HMD_STUB(sceHmdInternalCrashReportCancel)
HMD_STUB(sceHmdInternalCrashReportClose)
HMD_STUB(sceHmdInternalCrashReportOpen)
HMD_STUB(sceHmdInternalCrashReportReadData)
HMD_STUB(sceHmdInternalCrashReportReadDataSize)
HMD_STUB(sceHmdInternalCreateSharedMemory)
HMD_STUB(sceHmdInternalDfuCheckAfterPvt)
HMD_STUB(sceHmdInternalDfuCheckPartialUpdateAvailable)
HMD_STUB(sceHmdInternalDfuClose)
HMD_STUB(sceHmdInternalDfuGetStatus)
HMD_STUB(sceHmdInternalDfuOpen)
HMD_STUB(sceHmdInternalDfuReset)
HMD_STUB(sceHmdInternalDfuSend)
HMD_STUB(sceHmdInternalDfuSendSize)
HMD_STUB(sceHmdInternalDfuSetMode)
HMD_STUB(sceHmdInternalDfuStart)
HMD_STUB(sceHmdInternalEventInitialize)
HMD_STUB(sceHmdInternalGetBrightness)
HMD_STUB(sceHmdInternalGetCrashDumpInfo)
HMD_STUB(sceHmdInternalGetDebugMode)
HMD_STUB(sceHmdInternalGetDebugSocialScreenMode)
HMD_STUB(sceHmdInternalGetDebugTextMode)
HMD_STUB(sceHmdInternalGetDefaultLedData)
HMD_STUB(sceHmdInternalGetDemoMode)
HMD_STUB(sceHmdInternalGetDeviceInformationByHandle)
HMD_STUB(sceHmdInternalGetHmuOpticalParam)
HMD_STUB(sceHmdInternalGetHmuPowerStatusForDebug)
HMD_STUB(sceHmdInternalGetIPD)
HMD_STUB(sceHmdInternalGetIpdSettingEnableForSystemService)
HMD_STUB(sceHmdInternalGetPuBuildNumber)
HMD_STUB(sceHmdInternalGetPuPositionParam)
HMD_STUB(sceHmdInternalGetPuRevision)
HMD_STUB(sceHmdInternalGetPUSerialNumber)
HMD_STUB(sceHmdInternalGetRequiredPUPVersion)
HMD_STUB(sceHmdInternalGetSensorCalibrationData)
HMD_STUB(sceHmdInternalGetStatusReport)
HMD_STUB(sceHmdInternalGetTv4kCapability)
HMD_STUB(sceHmdInternalGetVirtualDisplayDepth)
HMD_STUB(sceHmdInternalGetVirtualDisplayHeight)
HMD_STUB(sceHmdInternalGetVirtualDisplaySize)
HMD_STUB(sceHmdInternalGetVr2dData)
HMD_STUB(sceHmdInternalIsCommonDlgMiniAppVr2d)
HMD_STUB(sceHmdInternalIsCommonDlgVr2d)
HMD_STUB(sceHmdInternalIsGameVr2d)
HMD_STUB(sceHmdInternalIsMiniAppVr2d)
HMD_STUB(sceHmdInternalMapSharedMemory)
HMD_STUB(sceHmdInternalMirroringModeSetAspect)
HMD_STUB(sceHmdInternalMirroringModeSetAspectDebug)
HMD_STUB(sceHmdInternalMmapGetModeId)
HMD_STUB(sceHmdInternalPushVr2dData)
HMD_STUB(sceHmdInternalRegisterEventCallback)
HMD_STUB(sceHmdInternalResetInertialSensor)
HMD_STUB(sceHmdInternalResetLedForVsh)
HMD_STUB(sceHmdInternalSeparateModeClose)
HMD_STUB(sceHmdInternalSeparateModeGetAudioStatus)
HMD_STUB(sceHmdInternalSeparateModeGetVideoStatus)
HMD_STUB(sceHmdInternalSeparateModeOpen)
HMD_STUB(sceHmdInternalSeparateModeSendAudio)
HMD_STUB(sceHmdInternalSeparateModeSendVideo)
HMD_STUB(sceHmdInternalSetBrightness)
HMD_STUB(sceHmdInternalSetCrashReportCommand)
HMD_STUB(sceHmdInternalSetDebugGpo)
HMD_STUB(sceHmdInternalSetDebugMode)
HMD_STUB(sceHmdInternalSetDebugSocialScreenMode)
HMD_STUB(sceHmdInternalSetDebugTextMode)
HMD_STUB(sceHmdInternalSetDefaultLedData)
HMD_STUB(sceHmdInternalSetDemoMode)
HMD_STUB(sceHmdInternalSetDeviceConnection)
HMD_STUB(sceHmdInternalSetForcedCrash)
HMD_STUB(sceHmdInternalSetHmuPowerControl)
HMD_STUB(sceHmdInternalSetHmuPowerControlForDebug)
HMD_STUB(sceHmdInternalSetIPD)
HMD_STUB(sceHmdInternalSetIpdSettingEnableForSystemService)
HMD_STUB(sceHmdInternalSetIPDSettingEnableForSystemService)
HMD_STUB(sceHmdInternalSetLedOn)
HMD_STUB(sceHmdInternalSetM2LedBrightness)
HMD_STUB(sceHmdInternalSetM2LedOn)
HMD_STUB(sceHmdInternalSetPortConnection)
HMD_STUB(sceHmdInternalSetPortStatus)
HMD_STUB(sceHmdInternalSetS3dPassMode)
HMD_STUB(sceHmdInternalSetSidetone)
HMD_STUB(sceHmdInternalSetVirtualDisplayDepth)
HMD_STUB(sceHmdInternalSetVirtualDisplayHeight)
HMD_STUB(sceHmdInternalSetVirtualDisplaySize)
HMD_STUB(sceHmdInternalSetVRMode)
HMD_STUB(sceHmdInternalSocialScreenGetFadeState)
HMD_STUB(sceHmdInternalSocialScreenSetFadeAndSwitch)
HMD_STUB(sceHmdInternalSocialScreenSetOutput)
HMD_STUB_OK_IN_VR(sceHmdReprojectionAddDisplayBuffer)
HMD_STUB_OK_IN_VR(sceHmdReprojectionClearUserEventEnd)
HMD_STUB_OK_IN_VR(sceHmdReprojectionClearUserEventStart)
HMD_STUB(sceHmdReprojectionDebugGetLastInfo)
HMD_STUB(sceHmdReprojectionDebugGetLastInfoMultilayer)
HMD_STUB_OK_IN_VR(sceHmdReprojectionFinalize)
HMD_STUB(sceHmdReprojectionFinalizeCapture)
HMD_STUB_OK_IN_VR(sceHmdReprojectionInitialize)
HMD_STUB(sceHmdReprojectionInitializeCapture)
HMD_STUB_OK_IN_VR(sceHmdReprojectionSetCallback)
HMD_STUB_OK_IN_VR(sceHmdReprojectionSetDisplayBuffers)
HMD_STUB_OK_IN_VR(sceHmdReprojectionSetUserEventEnd)
HMD_STUB_OK_IN_VR(sceHmdReprojectionSetUserEventStart)
HMD_STUB_OK_IN_VR(sceHmdReprojectionSetUserEventToFinish)
HMD_STUB_OK_IN_VR(sceHmdReprojectionSetUserEventToStart)
HMD_STUB_OK_IN_VR(sceHmdReprojectionStart)
HMD_STUB(sceHmdReprojectionStart2dVr)
HMD_STUB(sceHmdReprojectionStartCapture)
HMD_STUB(sceHmdReprojectionStartCompoundEye)
HMD_STUB(sceHmdReprojectionStartLiveCapture)
HMD_STUB_OK_IN_VR(sceHmdReprojectionStartMultilayer)
HMD_STUB_OK_IN_VR(sceHmdReprojectionStartMultilayer2)
HMD_STUB(sceHmdReprojectionStartWideNear)
HMD_STUB(sceHmdReprojectionStartWideNearWithOverlay)
HMD_STUB_OK_IN_VR(sceHmdReprojectionStartWithOverlay)
HMD_STUB_OK_IN_VR(sceHmdReprojectionStop)
HMD_STUB(sceHmdReprojectionStopCapture)
HMD_STUB(sceHmdReprojectionStopLiveCapture)
HMD_STUB_OK_IN_VR(sceHmdReprojectionUnsetCallback)
HMD_STUB_OK_IN_VR(sceHmdReprojectionUnsetDisplayBuffers)
HMD_STUB(sceHmdSetupDialogClose)
HMD_STUB(sceHmdSetupDialogGetResult)
HMD_STUB(sceHmdSetupDialogGetStatus)
HMD_STUB(sceHmdSetupDialogInitialize)
HMD_STUB(sceHmdSetupDialogOpen)
HMD_STUB(sceHmdSetupDialogTerminate)
HMD_STUB(sceHmdSetupDialogUpdateStatus)
}

