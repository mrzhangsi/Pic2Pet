#include "TeacherCamera.h"

#ifdef Q_OS_WIN

// windows.h 里的 min/max 宏会破坏 std::min / std::max，必须先禁掉
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <mfapi.h>
#  include <mfidl.h>
#  include <mfreadwrite.h>

namespace pic2pet {

namespace {

/** COM 初始化一次（MTA）。MF 的 source reader 在 MTA 下工作得最好 */
bool ensureComInitialized() {
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    // S_FALSE = 本线程已是 MTA（重复调用），RPC_E_CHANGED_MODE = 已是 STA，
    // 两者都继续往下走：真跑不起来时 read() 会返回空图，主流程自动降级。
    return hr == S_OK || hr == S_FALSE || hr == RPC_E_CHANGED_MODE;
}

} // namespace

CameraReader::CameraReader(int index) {
    if (!ensureComInitialized()) return;
    if (FAILED(MFStartup(MF_VERSION))) return;

    IMFAttributes* attr = nullptr;
    if (FAILED(MFCreateAttributes(&attr, 1))) return;
    attr->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                  MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_HW_SOURCE);

    IMFActivate** devices = nullptr;
    UINT32 count = 0;
    if (FAILED(MFEnumDeviceSources(attr, &devices, &count)) || count == 0 || !devices) {
        attr->Release();
        return;
    }
    attr->Release();

    IMFActivate* device = devices[count > UINT32(index) ? UINT32(index) : 0];
    IMFMediaSource* source = nullptr;
    if (SUCCEEDED(device->ActivateObject(IID_PPV_ARGS(&source)))) {
        IMFAttributes* readerAttr = nullptr;
        if (SUCCEEDED(MFCreateAttributes(&readerAttr, 1))) {
            // 开启视频处理器：摄像头原生多半是 NV12/YUY2，靠它转成 RGB32
            readerAttr->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
            IMFSourceReader* sr = nullptr;
            if (SUCCEEDED(MFCreateSourceReaderFromMediaSource(source, readerAttr, &sr))) {
                reader_ = sr;
                valid_ = true;
            }
            readerAttr->Release();
        }
        source->Release();
    }
    for (UINT32 i = 0; i < count; ++i) devices[i]->Release();
    CoTaskMemFree(devices);
}

CameraReader::~CameraReader() {
    if (reader_) static_cast<IMFSourceReader*>(reader_)->Release();
    if (valid_) MFShutdown();
}

QImage CameraReader::read() {
    if (!reader_) return QImage();
    auto* reader = static_cast<IMFSourceReader*>(reader_);

    IMFSample* sample = nullptr;
    DWORD flags = 0;
    LONGLONG timestamp = 0;
    HRESULT hr = reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nullptr,
                                    &flags, &timestamp, &sample);
    // 没有新帧（摄像头刚启动 / 丢帧）：下次再读，不当失败
    if (FAILED(hr) || !sample) {
        if (sample) sample->Release();
        return QImage();
    }

    QImage img;
    IMFMediaBuffer* buffer = nullptr;
    if (SUCCEEDED(sample->ConvertToContiguousBuffer(&buffer))) {
        IMFMediaType* type = nullptr;
        UINT32 width = 0, height = 0;
        INT32 stride = 0;
        if (SUCCEEDED(reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &type))) {
            MFGetAttributeSize(type, MF_MT_FRAME_SIZE, &width, &height);
            // 个别驱动会给出负 stride（自底向上）；没给就按 4*width 算
            if (FAILED(type->GetUINT32(MF_MT_DEFAULT_STRIDE, reinterpret_cast<UINT32*>(&stride)))) {
                stride = 0;
            }
            type->Release();
        }
        if (width > 0 && height > 0) {
            BYTE* data = nullptr;
            DWORD maxLen = 0, curLen = 0;
            if (SUCCEEDED(buffer->Lock(&data, &maxLen, &curLen)) && data) {
                if (stride == 0) stride = INT32(width) * 4;

                // RGB32 的内存布局（LE）是 B,G,R,A，与 QImage::Format_ARGB32 一致
                img = QImage(int(width), int(height), QImage::Format_ARGB32);
                const int absStride = qAbs(stride);
                for (UINT32 y = 0; y < height; ++y) {
                    const BYTE* src = stride >= 0 ? data + y * absStride
                                                  : data + (height - 1 - y) * absStride;
                    memcpy(img.scanLine(int(y)), src, size_t(width) * 4);
                }
                buffer->Unlock();
            }
        }
        buffer->Release();
    }
    sample->Release();
    return img;
}

} // namespace pic2pet

#else

namespace pic2pet {

CameraReader::CameraReader(int) {}
CameraReader::~CameraReader() = default;
QImage CameraReader::read() { return QImage(); }

} // namespace pic2pet

#endif
