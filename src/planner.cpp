#include "core.h"
#include <iostream>
#include <random>
#include <regex>
#include <set>

namespace {
struct Image {
    std::string finger;
    unsigned capture;
    fs::path path;
};
enum class Mode { All, First, Random, Indices, None };
struct Rule {
    Mode mode;
    unsigned count = 0;
    std::set<unsigned> indices;
};
unsigned positive(const std::string& value) {
    size_t end = 0;
    auto n = std::stoul(value, &end);
    if (end != value.size() || !n || n > 1000000)
        throw std::runtime_error("Expected a positive integer up to 1000000");
    return unsigned(n);
}
Rule parse(const std::string& value, bool enrollment) {
    if (value == "all")
        return {Mode::All};
    if (value == "first")
        return {Mode::First};
    if (value == "none" && !enrollment)
        return {Mode::None};
    if (value.rfind("random:", 0) == 0)
        return {Mode::Random, positive(value.substr(7))};
    if (value.rfind("indices:", 0) == 0) {
        Rule r{Mode::Indices};
        std::stringstream in(value.substr(8));
        std::string index;
        while (std::getline(in, index, ',')) {
            if (!r.indices.insert(positive(index)).second)
                throw std::runtime_error("Duplicate capture index");
        }
        if (r.indices.empty() || value.back() == ',')
            throw std::runtime_error("Empty capture index");
        return r;
    }
    throw std::runtime_error(
        "Rule must be first/all/random:N/indices:1,3 (or none for comparisons)");
}
// Specified 32-bit rejection sampling avoids std::shuffle implementation differences.
unsigned bounded(std::mt19937& rng, unsigned bound) {
    unsigned threshold = unsigned(0 - bound) % bound;
    for (;;) {
        unsigned x = rng();
        if (x >= threshold)
            return x % bound;
    }
}
template <class T> void shuffle(std::vector<T>& values, std::mt19937& rng) {
    for (size_t i = values.size(); i > 1; --i)
        std::swap(values[i - 1], values[bounded(rng, unsigned(i))]);
}
std::vector<Image> select(const std::vector<Image>& group, const Rule& rule, std::mt19937& rng,
                          const fs::path& excluded = {}) {
    std::vector<Image> choices;
    if (rule.mode == Mode::None)
        return choices;
    if (rule.mode == Mode::Indices) {
        for (auto capture : rule.indices) {
            auto it = std::find_if(group.begin(), group.end(),
                                   [&](auto& image) { return image.capture == capture; });
            if (it == group.end())
                throw std::runtime_error("Requested capture index is absent from a finger");
            if (it->path != excluded)
                choices.push_back(*it);
        }
        return choices;
    }
    for (auto& image : group)
        if (image.path != excluded)
            choices.push_back(image);
    if (rule.mode == Mode::First) {
        if (choices.size() > 1)
            choices.resize(1);
    }
    if (rule.mode == Mode::Random) {
        if (rule.count > choices.size())
            throw std::runtime_error(
                "Random count exceeds available images (self comparisons are excluded)");
        shuffle(choices, rng);
        choices.resize(rule.count);
    }
    return choices;
}
} // namespace
int wmain(int argc, wchar_t** argv) {
    try {
        if (argc != 8) {
            std::cerr
                << "Usage: fp_plan DB iType NEW_PLAN seed enroll_rule genuine_rule impostor_rule\n";
            return 2;
        }
        fs::path db = fs::canonical(argv[1]), output = fs::absolute(argv[3]);
        size_t parsed = 0;
        std::wstring typeText = argv[2];
        int type = std::stoi(typeText, &parsed);
        if (parsed != typeText.size())
            throw std::runtime_error("Invalid iType");
        if (!supportedType(type))
            throw std::runtime_error("Unsupported iType");
        if (fs::exists(output) || fs::exists(output.wstring() + L".rules.json"))
            throw std::runtime_error("Output already exists");
        std::string enrollment = utf8(argv[5]), genuine = utf8(argv[6]), impostor = utf8(argv[7]);
        auto er = parse(enrollment, true), gr = parse(genuine, false), ir = parse(impostor, false);
        std::wstring seedText = argv[4];
        if (seedText.empty() || seedText.find_first_not_of(L"0123456789") != std::wstring::npos)
            throw std::runtime_error("Seed must be an unsigned 32-bit integer");
        auto seedValue = std::stoull(seedText);
        if (seedValue > 0xffffffffULL)
            throw std::runtime_error("Seed must be an unsigned 32-bit integer");
        unsigned seed = static_cast<unsigned>(seedValue);
        std::mt19937 random(seed);
        std::map<std::string, std::vector<Image>> groups;
        std::set<std::pair<std::string, unsigned>> identities;
        for (auto& item : fs::directory_iterator(db))
            if (item.is_regular_file()) {
                auto ext = item.path().extension().wstring();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
                if (ext != L".tif" && ext != L".tiff" && ext != L".png" && ext != L".bmp" &&
                    ext != L".raw")
                    continue;
                std::smatch match;
                auto stem = item.path().stem().u8string();
                if (!std::regex_match(stem, match, std::regex("([0-9]+)_([0-9]+)")))
                    throw std::runtime_error("Use finger_capture image names");
                Image image{match[1], positive(match[2]), fs::canonical(item.path())};
                if (!identities.emplace(image.finger, image.capture).second)
                    throw std::runtime_error("Duplicate finger/capture");
                groups[image.finger].push_back(image);
            }
        if (groups.size() < 2)
            throw std::runtime_error("At least two finger identities are required");
        for (auto& item : groups)
            std::sort(item.second.begin(), item.second.end(),
                      [](auto& a, auto& b) { return a.capture < b.capture; });
        struct Pair {
            Image a, b;
            bool genuine;
            size_t id;
        };
        std::vector<Pair> pairs;
        size_t registrations = 0;
        for (auto& group : groups)
            for (auto& enrolled : select(group.second, er, random)) {
                ++registrations;
                for (auto& probe : select(group.second, gr, random, enrolled.path))
                    pairs.push_back({enrolled, probe, true, pairs.size() + 1});
                for (auto& other : groups)
                    if (other.first != group.first)
                        for (auto& probe : select(other.second, ir, random))
                            pairs.push_back({enrolled, probe, false, pairs.size() + 1});
            }
        if (pairs.empty())
            throw std::runtime_error("Rules produced no comparisons");
        shuffle(pairs, random);
        std::string dataset = utf8(db.filename());
        safeField(dataset);
        std::ostringstream body;
        body << "FPPLAN\t1\t" << seed << "\tcustom:" << enrollment << '|' << genuine << '|'
             << impostor << '\n';
        size_t ng = 0;
        for (auto& pair : pairs) {
            safeField(utf8(pair.a.path));
            safeField(utf8(pair.b.path));
            body << pair.id << '\t' << type << '\t' << dataset << '\t' << (pair.genuine ? 'G' : 'I')
                 << '\t' << utf8(pair.a.path) << '\t' << utf8(pair.b.path) << "\t1\n";
            if (pair.genuine)
                ++ng;
        }
        std::ofstream out(output);
        out << body.str();
        out.close();
        if (!out)
            throw std::runtime_error("Cannot write plan");
        std::ofstream manifest(fs::path(output.wstring() + L".rules.json"));
        manifest << "{\"schema_version\":1,\"seed\":" << seed
                 << ",\"enrollment\":" << json(enrollment) << ",\"genuine\":" << json(genuine)
                 << ",\"impostor\":" << json(impostor) << ",\"enrollment_images\":" << registrations
                 << ",\"genuine_pairs\":" << ng << ",\"impostor_pairs\":" << pairs.size() - ng
                 << ",\"genuine_sampling_unit\":\"per enrolled "
                    "image\",\"impostor_sampling_unit\":\"per enrolled image per other finger\"}\n";
        if (!manifest)
            throw std::runtime_error("Cannot write rule manifest");
        std::cout << "Plan: " << registrations << " enrolled images, " << ng << " genuine, "
                  << pairs.size() - ng << " impostor.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 2;
    }
}
