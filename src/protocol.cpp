#include "image.h"
#include "provenance.h"
#include <iostream>
#include <set>
std::string pixelHash(const std::vector<unsigned char>& data) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    unsigned char digest[32];
    DWORD objectSize = 0, written = 0;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        throw std::runtime_error("Cannot open SHA256 provider");
    BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectSize),
                      sizeof(objectSize), &written, 0);
    std::vector<unsigned char> object(objectSize);
    bool ok = BCryptCreateHash(algorithm, &hash, object.data(), objectSize, nullptr, 0, 0) >= 0;
    if (ok)
        ok = BCryptHashData(hash, const_cast<PUCHAR>(data.data()), ULONG(data.size()), 0) >= 0;
    if (ok)
        ok = BCryptFinishHash(hash, digest, 32, 0) >= 0;
    if (hash)
        BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (!ok)
        throw std::runtime_error("Cannot hash pixels");
    std::ostringstream out;
    for (auto x : digest)
        out << std::hex << std::setw(2) << std::setfill('0') << int(x);
    return out.str();
}
struct Entry {
    std::string dataset, subject, pixels, file, path;
};
std::vector<Entry> inventory(const fs::path& file) {
    std::ifstream in(file);
    std::string line;
    std::getline(in, line);
    if (line != "dataset\tsubject\tpixel_sha256\tfile_sha256\tpath")
        throw std::runtime_error("Invalid inventory header");
    std::vector<Entry> rows;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        auto f = split(line);
        if (f.size() != 5 || f[2].size() != 64 || f[3].size() != 64)
            throw std::runtime_error("Invalid inventory row");
        rows.push_back({f[0], f[1], f[2], f[3], f[4]});
    }
    if (rows.empty())
        throw std::runtime_error("Empty inventory");
    return rows;
}
int wmain(int argc, wchar_t** argv) {
    try {
        if (argc == 6 && std::wstring(argv[1]) == L"scan") {
            fs::path db = fs::canonical(argv[2]), output = argv[4];
            int type = std::stoi(argv[3]);
            UINT w = 0, h = 0;
            if (!dimensions(type, w, h))
                throw std::runtime_error("Unsupported iType");
            if (fs::exists(output))
                throw std::runtime_error("Inventory already exists");
            std::string dataset = utf8(argv[5]);
            safeField(dataset);
            if (dataset.empty())
                throw std::runtime_error("Dataset name is required");
            hcheck(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
            std::vector<fs::path> paths;
            for (auto& item : fs::directory_iterator(db))
                if (item.is_regular_file()) {
                    auto ext = item.path().extension().wstring();
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
                    if (ext == L".tif" || ext == L".tiff" || ext == L".bmp" || ext == L".png" ||
                        ext == L".raw")
                        paths.push_back(item.path());
                }
            if (paths.empty())
                throw std::runtime_error("No images");
            std::sort(paths.begin(), paths.end());
            std::ostringstream body;
            body << "dataset\tsubject\tpixel_sha256\tfile_sha256\tpath\n";
            for (auto& path : paths) {
                auto stem = path.stem().u8string();
                auto separator = stem.find('_');
                if (separator == std::string::npos)
                    throw std::runtime_error("Use subject_capture filenames");
                safeField(utf8(path));
                auto pixels = loadImage(path, w, h);
                body << dataset << '\t' << stem.substr(0, separator) << '\t' << pixelHash(pixels)
                     << '\t' << sha256(path) << '\t' << utf8(fs::canonical(path)) << '\n';
            }
            std::ofstream out(output);
            out << body.str();
            if (!out)
                throw std::runtime_error("Cannot save inventory");
            CoUninitialize();
            std::cout << "Inventoried " << paths.size()
                      << " images. Role is not inferred from filenames.\n";
            return 0;
        }
        if (argc == 4 && std::wstring(argv[1]) == L"check") {
            auto exposed = inventory(argv[2]), candidate = inventory(argv[3]);
            std::set<std::string> pixels, subjects;
            for (auto& e : exposed) {
                pixels.insert(e.pixels);
                subjects.insert(e.dataset + '/' + e.subject);
            }
            size_t sharedImages = 0, sharedSubjects = 0;
            for (auto& e : candidate) {
                if (pixels.count(e.pixels))
                    ++sharedImages;
                if (subjects.count(e.dataset + '/' + e.subject))
                    ++sharedSubjects;
            }
            std::cout << "Shared pixel images=" << sharedImages
                      << ", shared dataset/subject rows=" << sharedSubjects << "\n";
            if (sharedImages || sharedSubjects) {
                std::cerr << "REJECT: candidate overlaps exposed data; it cannot be described as "
                             "unseen holdout.\n";
                return 1;
            }
            std::cout << "No detected overlap. This is not proof of person-disjointness or "
                         "independent provenance; verify source and acquisition identities.\n";
            return 0;
        }
        std::cerr << "Usage:\n fp_protocol scan DB iType new_inventory.tsv dataset_id\n "
                     "fp_protocol check exposed_inventory.tsv candidate_inventory.tsv\n";
        return 2;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 2;
    }
}
