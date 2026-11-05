#include "image.h"
#include "minutiae.h"
#include "minutiae_graph.h"
#include "provenance.h"
#include <iostream>
#include <memory>
#include <regex>
#include <set>

namespace {
bool inspectionCancelled = false;
void checkCancellation(const fs::path& output) {
    if (fs::exists(output / "cancel.request")) {
        inspectionCancelled = true;
        throw std::runtime_error("Inspection cancelled");
    }
}
std::string readText(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        throw std::runtime_error("Cannot read " + utf8(path.filename()));
    return {std::istreambuf_iterator<char>(in), {}};
}
std::string base64(const std::string& bytes) {
    constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);
    for (size_t i = 0; i < bytes.size(); i += 3) {
        unsigned a = static_cast<unsigned char>(bytes[i]),
                 b = i + 1 < bytes.size() ? static_cast<unsigned char>(bytes[i + 1]) : 0,
                 c = i + 2 < bytes.size() ? static_cast<unsigned char>(bytes[i + 2]) : 0;
        unsigned v = (a << 16) | (b << 8) | c;
        out += alphabet[(v >> 18) & 63];
        out += alphabet[(v >> 12) & 63];
        out += i + 1 < bytes.size() ? alphabet[(v >> 6) & 63] : '=';
        out += i + 2 < bytes.size() ? alphabet[v & 63] : '=';
    }
    return out;
}
void writePng(const fs::path& file, const std::vector<unsigned char>& pixels, UINT w, UINT h) {
    ComPtr<IWICImagingFactory> factory;
    hcheck(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                            IID_PPV_ARGS(&factory)));
    ComPtr<IWICStream> stream;
    hcheck(factory->CreateStream(&stream));
    hcheck(stream->InitializeFromFilename(file.c_str(), GENERIC_WRITE));
    ComPtr<IWICBitmapEncoder> encoder;
    hcheck(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder));
    hcheck(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache));
    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> properties;
    hcheck(encoder->CreateNewFrame(&frame, &properties));
    hcheck(frame->Initialize(properties.Get()));
    hcheck(frame->SetSize(w, h));
    WICPixelFormatGUID format = GUID_WICPixelFormat8bppGray;
    hcheck(frame->SetPixelFormat(&format));
    if (!IsEqualGUID(format, GUID_WICPixelFormat8bppGray))
        throw std::runtime_error("PNG encoder changed pixel format");
    hcheck(frame->WritePixels(h, w, UINT(pixels.size()), const_cast<BYTE*>(pixels.data())));
    hcheck(frame->Commit());
    hcheck(encoder->Commit());
}
struct ImageEntry {
    fs::path path;
    int type = 0;
    size_t index = 0;
    bool probe = false;
    Outcome outcome;
    std::unique_ptr<minutiae::Feature> feature;
};
struct PairEntry {
    std::string id, db, status, stage;
    bool genuine = false;
    size_t a = 0, b = 0;
    double score = 0;
};
void loadFeature(const fs::path& run, ImageEntry& image) {
    if (image.feature)
        return;
    auto task = run / "tasks" /
                ((image.probe ? "attempt" : "enroll") + std::to_string(image.index) + ".tsv");
    if (!fs::exists(task))
        throw std::runtime_error("Extraction task metadata missing");
    image.outcome = readOutcome(task);
    if (image.outcome.status != "OK")
        return;
    auto path =
        run / "templates" / ((image.probe ? "a" : "e") + std::to_string(image.index) + ".bin");
    if (fs::file_size(path) != sizeof(minutiae::Feature))
        throw std::runtime_error("Only minutiae-v2 templates are supported");
    image.feature = std::make_unique<minutiae::Feature>();
    std::ifstream in(path, std::ios::binary);
    in.read(reinterpret_cast<char*>(image.feature.get()), sizeof(minutiae::Feature));
    if (!in || !minutiae::valid(*image.feature))
        throw std::runtime_error("Invalid/corrupt minutiae-v2 template");
}
std::string pointsJson(const minutiae::Feature* feature) {
    std::ostringstream out;
    out << std::setprecision(17) << '[';
    if (feature)
        for (unsigned i = 0; i < feature->count; ++i) {
            if (i)
                out << ',';
            auto p = feature->points[i];
            out << '[' << p.x << ',' << p.y << ',' << p.theta << ',' << p.quality << ']';
        }
    out << ']';
    return out.str();
}
int inspect(const fs::path& run, const fs::path& output, unsigned limit, bool auditOnly = false) {
    if (limit < 1 || limit > 100)
        throw std::runtime_error("Count must be 1..100 per category");
    if (!auditOnly && fs::exists(output))
        throw std::runtime_error("Output directory already exists");
    std::string provenance = readText(run / "provenance.json");
    // Known candidate filename selects tracing implementation; exact recorded-score verification
    // remains mandatory.
    bool graphMatcher = provenance.find("fingerprint_graph.dll\"") != std::string::npos;
    auto compare = [graphMatcher](const minutiae::Feature& a, const minutiae::Feature& b,
                                  minutiae::Diagnostics* d = nullptr) {
        return graphMatcher ? minutiae::graph::compare(a, b, d) : minutiae::compare(a, b, d);
    };
    if (provenance.find("\"schema_version\":1,") == std::string::npos)
        throw std::runtime_error("Canonical provenance schema 1 is required");
    std::smatch match;
    if (!std::regex_search(provenance, match, std::regex("\"plan_sha256\":\"([0-9a-f]{64})\"")) ||
        sha256(run / "plan.tsv") != match[1].str())
        throw std::runtime_error("Saved plan hash differs from evaluation provenance");
    if (!std::regex_search(provenance, match, std::regex("\"noise\":\"([a-z0-9]+)\"")))
        throw std::runtime_error("Noise metadata missing");
    std::string noise = match[1];
    if (!std::regex_search(provenance, match, std::regex("\"noise_seed\":([0-9]+)")))
        throw std::runtime_error("Noise seed missing");
    unsigned seed = static_cast<unsigned>(std::stoul(match[1]));
    std::map<fs::path, size_t> enrollIndex, probeIndex;
    std::vector<ImageEntry> images;
    std::vector<PairEntry> pairs;
    std::map<std::string, size_t> pairIndex;
    std::ifstream plan(run / "plan.tsv");
    std::string line;
    std::getline(plan, line);
    if (line.rfind("FPPLAN\t1\t", 0) != 0)
        throw std::runtime_error("Unsupported plan format");
    auto imageIndex = [&](const std::string& text, int type, bool probe) {
        auto path = fs::u8path(text);
        auto& indices = probe ? probeIndex : enrollIndex;
        auto found = indices.find(path);
        if (found != indices.end())
            return found->second;
        size_t index = indices.size(), result = images.size();
        indices[path] = result;
        images.push_back({path, type, index, probe, {}, nullptr});
        return result;
    };
    while (std::getline(plan, line)) {
        auto f = split(line);
        if (f.size() != 7 || f[6] != "1")
            throw std::runtime_error("Invalid plan row");
        if (f[3] != "G" && f[3] != "I")
            throw std::runtime_error("Invalid plan label");
        int type = std::stoi(f[1]);
        if (!supportedType(type))
            throw std::runtime_error("Unsupported image type");
        size_t a = imageIndex(f[4], type, false), b = imageIndex(f[5], type, true);
        if (!pairIndex.emplace(f[0], pairs.size()).second)
            throw std::runtime_error("Duplicate pair ID");
        pairs.push_back({f[0], f[2], "PENDING", "", f[3] == "G", a, b, 0});
    }
    if (pairs.empty())
        throw std::runtime_error("Empty plan");
    std::ifstream table(run / "pairs.tsv");
    std::getline(table, line);
    if (line != "pair_id\tkind\tstatus\tstage\tscore\tcompare_ms\tmatching_ms")
        throw std::runtime_error("Canonical pairs.tsv is required");
    std::set<std::string> seen;
    while (std::getline(table, line)) {
        auto f = split(line);
        if (f.size() != 7 || !pairIndex.count(f[0]) || !seen.insert(f[0]).second)
            throw std::runtime_error("Invalid pair result");
        auto& p = pairs[pairIndex.at(f[0])];
        if ((f[1] != "G" && f[1] != "I") || (f[1] == "G") != p.genuine)
            throw std::runtime_error("Result label mismatch");
        if (f[2] != "OK" && f[2] != "FAIL" && f[2] != "TIMEOUT" && f[2] != "CRASH")
            throw std::runtime_error("Incomplete run is not supported");
        p.status = f[2];
        p.stage = f[3];
        if (p.status == "OK") {
            p.score = std::stod(f[4]);
            if (!std::isfinite(p.score) || p.score < 0 || p.score > 1)
                throw std::runtime_error("Invalid recorded score");
        }
    }
    if (seen.size() != pairs.size())
        throw std::runtime_error("Incomplete pair table");
    if (auditOnly) {
        for (auto& image : images)
            loadFeature(run, image);
        size_t verified = 0, failures = 0;
        for (auto& pair : pairs) {
            if (pair.status != "OK") {
                ++failures;
                continue;
            }
            auto& a = images[pair.a].feature;
            auto& b = images[pair.b].feature;
            if (!a || !b)
                throw std::runtime_error("Successful pair has missing features");
            minutiae::Diagnostics diagnostic;
            double normal = compare(*a, *b), traced = compare(*a, *b, &diagnostic);
            if (normal != traced || std::abs(traced - pair.score) > 1e-12 ||
                diagnostic.score != traced)
                throw std::runtime_error("Trace/normal/recorded score disagreement");
            std::set<int> usedA, usedB;
            for (auto edge : diagnostic.matches) {
                if (!usedA.insert(edge.a).second || !usedB.insert(edge.b).second)
                    throw std::runtime_error("Trace violated one-to-one matching");
                auto p = a->points[edge.a], q = b->points[edge.b];
                auto t = diagnostic.transform;
                if (std::hypot(t.cs * p.x - t.sn * p.y + t.tx - q.x,
                               t.sn * p.x + t.cs * p.y + t.ty - q.y) >
                    minutiae::positionTolerance + 1e-9)
                    throw std::runtime_error("Trace geometry disagrees with matcher");
            }
            ++verified;
        }
        std::cout << "PASS: " << verified
                  << " recorded scores identical with and without diagnostics; " << failures
                  << " recorded failures retained.\n";
        return 0;
    }
    std::vector<size_t> selected;
    // Failures have their own quota; they must not crowd out high-scoring impostors.
    for (size_t i = 0; i < pairs.size() && selected.size() < limit; ++i)
        if (pairs[i].status != "OK")
            selected.push_back(i);
    for (bool genuine : {true, false}) {
        std::vector<size_t> group;
        for (size_t i = 0; i < pairs.size(); ++i)
            if (pairs[i].genuine == genuine && pairs[i].status == "OK")
                group.push_back(i);
        std::stable_sort(group.begin(), group.end(), [&](size_t a, size_t b) {
            auto& x = pairs[a];
            auto& y = pairs[b];
            if ((x.status != "OK") != (y.status != "OK"))
                return x.status != "OK";
            if (x.score != y.score)
                return genuine ? x.score < y.score : x.score > y.score;
            return x.id < y.id;
        });
        group.resize(std::min(group.size(), size_t(limit)));
        selected.insert(selected.end(), group.begin(), group.end());
    }
    hcheck(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
    struct Cleanup {
        ~Cleanup() {
            CoUninitialize();
        }
    } cleanup;
    fs::create_directories(output / "images");
    std::set<size_t> used;
    for (auto i : selected) {
        used.insert(pairs[i].a);
        used.insert(pairs[i].b);
    }
    std::ostringstream imageData;
    imageData << std::setprecision(17) << '{';
    bool comma = false;
    for (size_t index : used) {
        checkCancellation(output);
        auto& image = images[index];
        std::string entry = "\"path\":" + json(utf8(image.path)) + ",\"sha256\":\"";
        auto pos = provenance.find(entry);
        if (pos == std::string::npos)
            throw std::runtime_error("Canonical image provenance missing");
        auto expected = provenance.substr(pos + entry.size(), 64);
        if (sha256(image.path) != expected)
            throw std::runtime_error("Source image changed since evaluation: " +
                                     utf8(image.path.filename()));
        loadFeature(run, image);
        UINT w = 0, h = 0;
        dimensions(image.type, w, h);
        std::string png = "images/" + std::to_string(index) + ".png", decodeError, dataUrl;
        try {
            auto pixels = loadImage(image.path, w, h);
            unsigned imageSeed = seed;
            for (unsigned char c : utf8(image.path.filename()))
                imageSeed = (imageSeed ^ c) * 16777619u;
            if (image.probe)
                addNoise(pixels, w, h, noise, imageSeed);
            writePng(output / fs::u8path(png), pixels, w, h);
            dataUrl = "data:image/png;base64," + base64(readText(output / fs::u8path(png)));
        } catch (const std::exception& e) {
            decodeError = e.what();
            png.clear();
        }
        unsigned outside = 0;
        if (image.feature)
            for (unsigned i = 0; i < image.feature->count; ++i) {
                auto p = image.feature->points[i];
                if (p.x >= w || p.y >= h)
                    ++outside;
            }
        if (comma)
            imageData << ',';
        comma = true;
        imageData << json(std::to_string(index))
                  << ":{\"name\":" << json(utf8(image.path.filename())) << ",\"width\":" << w
                  << ",\"height\":" << h << ",\"url\":" << json(png)
                  << ",\"dataUrl\":" << json(dataUrl) << ",\"decodeError\":" << json(decodeError)
                  << ",\"points\":" << pointsJson(image.feature.get())
                  << ",\"outOfBounds\":" << outside
                  << ",\"extraction\":" << outcomeJson(image.outcome) << '}';
    }
    imageData << '}';
    std::ostringstream pairData;
    pairData << std::setprecision(17) << '[';
    comma = false;
    for (auto index : selected) {
        checkCancellation(output);
        auto& p = pairs[index];
        minutiae::Diagnostics diagnostics;
        if (p.status == "OK") {
            auto& a = images[p.a].feature;
            auto& b = images[p.b].feature;
            if (!a || !b)
                throw std::runtime_error("Successful pair is missing a successful extraction");
            double score = compare(*a, *b, &diagnostics);
            if (std::abs(score - p.score) > 1e-12)
                throw std::runtime_error("Analysis matcher differs from recorded score; use the "
                                         "matching source version");
        }
        if (comma)
            pairData << ',';
        comma = true;
        auto t = diagnostics.transform;
        pairData << "{\"id\":" << json(p.id) << ",\"database\":" << json(p.db)
                 << ",\"genuine\":" << (p.genuine ? "true" : "false") << ",\"a\":" << p.a
                 << ",\"b\":" << p.b << ",\"status\":" << json(p.status)
                 << ",\"stage\":" << json(p.stage) << ",\"score\":";
        if (p.status == "OK")
            pairData << p.score;
        else
            pairData << "null";
        pairData << ",\"hasAlignment\":" << (diagnostics.hasAlignment ? "true" : "false")
                 << ",\"candidateCount\":" << diagnostics.candidateCount
                 << ",\"hypotheses\":" << diagnostics.hypotheses << ",\"transform\":[" << t.cs
                 << ',' << t.sn << ',' << t.tx << ',' << t.ty << "],\"matches\":[";
        bool edgeComma = false;
        for (auto edge : diagnostics.matches) {
            auto a = images[p.a].feature->points[edge.a], b = images[p.b].feature->points[edge.b];
            double dx = t.cs * a.x - t.sn * a.y + t.tx - b.x,
                   dy = t.sn * a.x + t.cs * a.y + t.ty - b.y;
            double angle = std::abs(minutiae::wrap(a.theta + std::atan2(t.sn, t.cs) - b.theta)) *
                           180 / minutiae::pi;
            if (edgeComma)
                pairData << ',';
            edgeComma = true;
            pairData << '[' << edge.a << ',' << edge.b << ',' << edge.similarity << ','
                     << std::hypot(dx, dy) << ',' << angle << ']';
        }
        pairData << "]}";
    }
    pairData << ']';
    std::ostringstream data;
    data << "{\"matcher\":" << json(graphMatcher ? "graph-v1" : "minutiae-v2")
         << ",\"schemaVersion\":1,\"run\":" << json(utf8(run.filename()))
         << ",\"noise\":" << json(noise) << ",\"seed\":" << seed
         << ",\"plannedPairs\":" << pairs.size()
         << ",\"selection\":\"Separate failure quota, lowest successful genuine and highest "
            "successful impostor scores; no "
            "decision threshold fitted\",\"provenanceSha256\":"
         << json(sha256(run / "provenance.json")) << ",\"images\":" << imageData.str()
         << ",\"pairs\":" << pairData.str() << '}';
    std::ofstream jsonFile(output / "diagnostics.json", std::ios::binary);
    jsonFile << data.str();
    jsonFile.close();
    if (!jsonFile)
        throw std::runtime_error("Cannot save diagnostics");
    wchar_t exe[32768];
    auto length = GetModuleFileNameW(nullptr, exe, 32768);
    if (!length || length >= 32768)
        throw std::runtime_error("Cannot find assets");
    auto assets = fs::path(exe).parent_path() / "inspect-assets";
    auto html = readText(assets / "index.html");
    auto marker = html.find("__INSPECTION_DATA__");
    if (marker == std::string::npos)
        throw std::runtime_error("Invalid viewer template");
    std::string safe = data.str();
    size_t offset = 0;
    while ((offset = safe.find('<', offset)) != std::string::npos) {
        safe.replace(offset, 1, "\\u003c");
        offset += 6;
    }
    html.replace(marker, 19, safe);
    checkCancellation(output);
    std::ofstream page(output / "index.html", std::ios::binary);
    page << html;
    page.close();
    if (!page)
        throw std::runtime_error("Cannot save viewer");
    fs::copy_file(assets / "viewer.js", output / "viewer.js");
    std::cout << "Created " << selected.size()
              << " pair inspections with source hashes and exact recorded-score verification.\n";
    return 0;
}
} // namespace
int wmain(int argc, wchar_t** argv) {
    try {
        if (argc == 3 && std::wstring(argv[1]) == L"--verify")
            return inspect(fs::canonical(argv[2]), {}, 1, true);
        if (argc != 3 && argc != 4) {
            std::cerr << "Usage: fp_inspect RUN_FOLDER NEW_OUTPUT_FOLDER [count_per_category=10]\n"
                         "       fp_inspect --verify RUN_FOLDER\n";
            return 2;
        }
        return inspect(fs::canonical(argv[1]), fs::absolute(argv[2]),
                       argc == 4 ? static_cast<unsigned>(std::stoul(argv[3])) : 10);
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return inspectionCancelled ? 130 : 2;
    }
}
