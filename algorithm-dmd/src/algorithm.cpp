#include "api.h"
#include "fingerjet_loader.h"
#include "matcher.h"
#include "model_hash.h"
#include "model_identity.h"
#include "photometric.h"
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <mutex>
#include <onnxruntime_cxx_api.h>

namespace
{
const unsigned char anchor = 0;
std::filesystem::path directory()
{
    HMODULE self = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&anchor), &self))
        throw AlgorithmError(110, "Cannot locate DMD module");
    std::vector<wchar_t> name(32768);
    DWORD n = GetModuleFileNameW(self, name.data(), DWORD(name.size()));
    if (!n || n >= name.size())
        throw AlgorithmError(110, "Cannot read DMD module path");
    return std::filesystem::path(std::wstring(name.data(), n)).parent_path();
}
bool accessible(const void *ptr, size_t size, bool write)
{
    auto cur = reinterpret_cast<uintptr_t>(ptr);
    if (!cur || size > std::numeric_limits<uintptr_t>::max() - cur)
        return false;
    auto end = cur + size;
    while (cur < end)
    {
        MEMORY_BASIC_INFORMATION info{};
        if (!VirtualQuery(reinterpret_cast<void *>(cur), &info, sizeof(info)) || info.State != MEM_COMMIT ||
            (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)))
            return false;
        DWORD protection = info.Protect & 0xff;
        bool allowed = protection == PAGE_READWRITE || protection == PAGE_WRITECOPY ||
                       protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
        if (!write)
            allowed = allowed || protection == PAGE_READONLY || protection == PAGE_EXECUTE_READ;
        if (!allowed)
            return false;
        auto next = reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;
        if (next <= cur)
            return false;
        cur = next;
    }
    return true;
}
bool readable(const void *p, size_t n)
{
    return accessible(p, n, false);
}
bool writable(void *p, size_t n)
{
    return accessible(p, n, true);
}

uint32_t checksum(const unsigned char *p, size_t n)
{
    static const auto table = []() {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; ++i)
        {
            uint32_t v = i;
            for (int k = 0; k < 8; ++k)
                v = (v >> 1) ^ ((v & 1) ? 0xedb88320u : 0);
            t[i] = v;
        }
        return t;
    }();
    uint32_t crc = 0xffffffffu;
    for (size_t i = 0; i < n; ++i)
    {
        unsigned char b = (i >= 32 && i < 36) ? 0 : p[i];
        crc = table[(crc ^ b) & 255] ^ (crc >> 8);
    }
    return ~crc;
}
int encode(const dmd::Template &value, unsigned char *output)
{
    unsigned n = unsigned(value.points.size());
    uint32_t bytes = 40 + n * (3 + 768 + 64) * 4;
    std::vector<unsigned char> wire(bytes);
    uint32_t header[] = {identity::magic, identity::modelTag, bytes, n, value.width, value.height, 500, 768, 0, 64};
    std::memcpy(wire.data(), header, 40);
    size_t offset = 40;
    auto append = [&](const void *p, size_t count) {
        std::memcpy(wire.data() + offset, p, count);
        offset += count;
    };
    append(value.points.data(), n * 12);
    append(value.feature.data(), n * 768 * 4);
    append(value.mask.data(), n * 64 * 4);
    uint32_t hash = checksum(wire.data(), wire.size());
    std::memcpy(wire.data() + 32, &hash, 4);
    if (!writable(output, bytes))
        throw AlgorithmError(103, "Unwritable template output");
    std::memcpy(output, wire.data(), bytes);
    return int(bytes);
}
dmd::Template decode(const unsigned char *input)
{
    if (!readable(input, 40))
        throw AlgorithmError(200, "Unreadable DMD template header");
    uint32_t h[10];
    std::memcpy(h, input, 40);
    if (h[0] != identity::magic || h[1] != identity::modelTag)
        throw AlgorithmError(201, "Unsupported DMD template version");
    if (h[3] < 3 || h[3] > 64 || h[2] != 40 + h[3] * 835 * 4 || h[6] != 500 || h[7] != 768 || h[9] != 64 ||
        !readable(input, h[2]))
        throw AlgorithmError(200, "Invalid DMD template size");
    if (h[8] != checksum(input, h[2]))
        throw AlgorithmError(202, "DMD template checksum mismatch");
    dmd::Template t;
    t.width = h[4];
    t.height = h[5];
    if (!((t.width == 448 && t.height == 478) || (t.width == 300 && t.height == 300) ||
          (t.width == 640 && t.height == 480)))
        throw AlgorithmError(200, "Invalid template dimensions");
    unsigned n = h[3];
    t.points.resize(n);
    t.feature.resize(n * 768);
    t.mask.resize(n * 64);
    size_t offset = 40;
    auto take = [&](void *p, size_t bytes) {
        std::memcpy(p, input + offset, bytes);
        offset += bytes;
    };
    take(t.points.data(), n * 12);
    take(t.feature.data(), n * 768 * 4);
    take(t.mask.data(), n * 64 * 4);
    for (const auto &p : t.points)
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.theta) || p.x < -1 || p.x > t.width + 1 ||
            p.y < -1 || p.y > t.height + 1 || std::abs(p.theta) > 720)
            throw AlgorithmError(200, "Invalid point");
    for (float f : t.feature)
        if (!std::isfinite(f) || std::abs(f) > 1000)
            throw AlgorithmError(200, "Invalid descriptor");
    for (float m : t.mask)
        if (!std::isfinite(m) || m < 0 || m > 1.001f)
            throw AlgorithmError(200, "Invalid mask");
    return t;
}

struct Runtime
{
    HMODULE module = nullptr;
    DLL_DIRECTORY_COOKIE cookie = nullptr;
    std::unique_ptr<Ort::Env> env;
    std::unique_ptr<Ort::Session> session;
    std::mutex inferenceMutex;
    unsigned batchSize = 128;
    Runtime()
    {
        auto dir = directory();
        auto model = dir / L"dmd-int8.onnx";
        if (!std::filesystem::is_regular_file(model))
            throw AlgorithmError(110, "Missing sibling dmd-int8.onnx");
        if (modelhash::sha256(model) != identity::sha256)
            throw AlgorithmError(112, "DMD model integrity mismatch");
        cookie = AddDllDirectory(dir.c_str());
        if (!cookie)
            throw AlgorithmError(110, "Cannot register dependency directory");
        module = LoadLibraryExW((dir / L"onnxruntime.dll").c_str(), nullptr,
                                LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (!module)
            throw AlgorithmError(110, "Cannot load sibling onnxruntime.dll: " + std::to_string(GetLastError()));
        auto getApi = reinterpret_cast<const OrtApiBase *(ORT_API_CALL *)()>(GetProcAddress(module, "OrtGetApiBase"));
        if (!getApi)
            throw AlgorithmError(111, "OrtGetApiBase missing");
        const OrtApi *api = getApi()->GetApi(ORT_API_VERSION);
        if (!api)
            throw AlgorithmError(111, "ONNX Runtime API version mismatch");
        Ort::InitApi(api);
        batchSize = 8;
        env = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "DMD-CPU64-INT8");
        Ort::SessionOptions options;
        options.SetIntraOpNumThreads(4);
        options.SetInterOpNumThreads(1);
        options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        // Deliberately CPU-only. Backend/precision/cap are immutable release choices.
        session = std::make_unique<Ort::Session>(*env, model.c_str(), options);
    }
    ~Runtime()
    {
        session.reset();
        env.reset();
        if (cookie)
            RemoveDllDirectory(cookie); /* ORT/provider modules remain OS-managed until process termination. */
    }
    void infer(const std::vector<float> &patches, dmd::Template &t)
    {
        std::lock_guard<std::mutex> lock(inferenceMutex);
        auto memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        const char *ins[] = {"patches"};
        const char *outs[] = {"features", "masks"};
        size_t total = t.points.size();
        t.feature.resize(total * 768);
        t.mask.resize(total * 64);
        for (size_t begin = 0; begin < total; begin += batchSize)
        {
            size_t n = std::min(size_t(batchSize), total - begin);
            int64_t shape[] = {int64_t(n), 1, 128, 128};
            auto tensor = Ort::Value::CreateTensor<float>(memory, const_cast<float *>(patches.data()) + begin * 16384,
                                                          n * 16384, shape, 4);
            auto values = session->Run(Ort::RunOptions{nullptr}, ins, &tensor, 1, outs, 2);
            if (values[0].GetTensorTypeAndShapeInfo().GetElementCount() != n * 768 ||
                values[1].GetTensorTypeAndShapeInfo().GetElementCount() != n * 64)
                throw AlgorithmError(122, "Unexpected DMD output shape");
            std::copy_n(values[0].GetTensorData<float>(), n * 768, t.feature.data() + begin * 768);
            std::copy_n(values[1].GetTensorData<float>(), n * 64, t.mask.data() + begin * 64);
        }
    }
};
Runtime &runtime()
{
    // ONNX owns worker threads. Destroying a Session from DLL_PROCESS_DETACH
    // under the Windows loader lock can deadlock. The evaluator already uses a
    // dedicated Worker process; retain one session and pin this module until
    // that process exits. No inference/session destructor runs in DllMain.
    static Runtime *value = []() {
        auto p = std::make_unique<Runtime>();
        HMODULE pinned = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                                reinterpret_cast<LPCWSTR>(&anchor), &pinned))
            throw AlgorithmError(110, "Cannot pin DMD runtime module");
        return p.release();
    }();
    return *value;
}
std::vector<float> patches(const unsigned char *pixels, unsigned width, unsigned height,
                           const std::vector<dmd::Point> &points)
{
    std::vector<float> output(points.size() * 128 * 128);
    auto at = [&](int x, int y) {
        return (x < 0 || y < 0 || x >= int(width) || y >= int(height)) ? 127.5f : float(pixels[y * width + x]);
    };
    for (size_t n = 0; n < points.size(); ++n)
    {
        auto p = points[n];
        float angle = p.theta * 0.017453292519943295f, c = std::cos(angle), s = std::sin(angle);
        for (int y = 0; y < 128; ++y)
            for (int x = 0; x < 128; ++x)
            {
                float dx = float(x - 64), dy = float(y - 64);
                float xx = c * dx - s * dy + p.x, yy = s * dx + c * dy + p.y;
                int ix = int(std::floor(xx)), iy = int(std::floor(yy));
                float fx = xx - ix, fy = yy - iy;
                float v = (1 - fy) * ((1 - fx) * at(ix, iy) + fx * at(ix + 1, iy)) +
                          fy * ((1 - fx) * at(ix, iy + 1) + fx * at(ix + 1, iy + 1));
                output[n * 16384 + y * 128 + x] = (v - 127.5f) / 127.5f;
            }
    }
    return output;
}
template <class F> int boundary(int *error, char *msg, F work) noexcept
{
    if (!writable(error, sizeof(int)) || !writable(msg, 256))
        return 0;
    *error = 0;
    msg[0] = 0;
    try
    {
        work();
        return 1;
    }
    catch (const AlgorithmError &e)
    {
        *error = e.code;
        strncpy_s(msg, 256, e.what(), _TRUNCATE);
    }
    catch (const std::bad_alloc &)
    {
        *error = 900;
        strcpy_s(msg, 256, "Allocation failed");
    }
    catch (const std::exception &e)
    {
        *error = 901;
        strncpy_s(msg, 256, e.what(), _TRUNCATE);
    }
    catch (...)
    {
        *error = 999;
        strcpy_s(msg, 256, "Unknown exception");
    }
    return 0;
}
void extract(int type, unsigned char *image, unsigned char *output, int *size, std::unique_ptr<FingerJet> &owner)
{
    if (!image || !output || !size)
        throw AlgorithmError(100, "Null extraction argument");
    unsigned width = 0, height = 0;
    if (!dimensions(type, width, height))
        throw AlgorithmError(101, "Unsupported iType");
    if (!readable(image, size_t(width) * height))
        throw AlgorithmError(102, "Unreadable image");
    auto normalized = photometric::normalize(image, size_t(width) * height);
    owner = std::make_unique<FingerJet>();
    FingerJet &fj = *owner;
    FeatureHandle context{fj}, feature{fj};
    auto check = [](FRFXLL_RESULT status) {
        if (!FRFXLL_SUCCESS(status))
            throw AlgorithmError(120, "FingerJet extraction failed");
    };
    check(fj.createContext(&context.value));
    check(fj.createFeatures(context.value, normalized.data(), width * height, width, height, 500,
                            FRFXLL_FEX_ENABLE_ENHANCEMENT, &feature.value));
    unsigned count = 0, res = 0;
    check(fj.getInfo(feature.value, &count, &res));
    if (count < 3)
        throw AlgorithmError(121, "Too few minutiae");
    if (count > 65536 || res < 100 || res > 2000)
        throw AlgorithmError(122, "Invalid FingerJet output");
    std::vector<FRFXLL_Basic_19794_2_Minutia> pts(count);
    unsigned allocated = count;
    check(fj.getMinutiae(feature.value, BASIC_19794_2_MINUTIA_STRUCT, &count, pts.data()));
    if (count < 3 || count > allocated)
        throw AlgorithmError(122, "Invalid minutiae count");
    pts.resize(count);
    std::stable_sort(pts.begin(), pts.end(), [](auto a, auto b) { return a.q > b.q; });
    dmd::Template t;
    t.width = width;
    t.height = height;
    for (unsigned i = 0; i < std::min(count, 64u); ++i)
    {
        auto p = pts[i];
        float radians = float(-p.a * 2 * 3.14159265358979323846 / 256);
        t.points.push_back({float(p.x * 500.0 / res), float(p.y * 500.0 / res), radians * (180.f / dmd::pi)});
    }
    // DMD was validated on the original image; contrast correction is for FingerJet only.
    auto batch = patches(image, width, height, t.points);
    runtime().infer(batch, t);
    *size = encode(t, output);
}
} // namespace
extern "C" __declspec(dllexport) int __cdecl KISA_Enroll(int type, unsigned char *image, unsigned char *output,
                                                         int *size, int *error, char *message)
{
    std::unique_ptr<FingerJet> fj;
    return boundary(error, message, [&] {
        if (!writable(size, sizeof(int)))
            throw AlgorithmError(100, "Invalid size argument");
        *size = 0;
        extract(type, image, output, size, fj);
    });
}
extern "C" __declspec(dllexport) int __cdecl KISA_Attempt(int type, unsigned char *image, unsigned char *output,
                                                          int *size, int *error, char *message)
{
    std::unique_ptr<FingerJet> fj;
    return boundary(error, message, [&] {
        if (!writable(size, sizeof(int)))
            throw AlgorithmError(100, "Invalid size argument");
        *size = 0;
        extract(type, image, output, size, fj);
    });
}
extern "C" __declspec(dllexport) int __cdecl KISA_Compare(unsigned char *first, unsigned char *second, double *score,
                                                          int *error, char *message)
{
    return boundary(error, message, [&] {
        if (!writable(score, sizeof(double)))
            throw AlgorithmError(100, "Invalid score argument");
        *score = 0;
        if (!first || !second)
            throw AlgorithmError(100, "Null comparison argument");
        auto enroll = decode(first), attempt = decode(second);
        double value = dmd::compare(attempt, enroll);
        if (!std::isfinite(value))
            throw AlgorithmError(200, "Nonfinite score");
        *score = std::round(std::clamp(value, 0.0, 1.0) * 1000) / 1000;
    });
}
