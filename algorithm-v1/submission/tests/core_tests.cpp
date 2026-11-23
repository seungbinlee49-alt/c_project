#include "template_codec.h"
#include <iostream>
#include <memory>
#include <random>
#define minutiae legacy_minutiae
#include "../../src/minutiae_orientation.h"
#undef minutiae

void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

int main() {
    try {
        std::mt19937 random(20261123);
        for (unsigned i = 0; i < 100000; ++i) {
            double angle = (double(random()) / random.max() - .5) * 20;
            double direct = std::remainder(angle, 2 * minutiae::pi);
            require(minutiae::wrap(angle) == direct, "Fast angle wrap changed remainder");
        }
        for (double edge :
             {-2 * minutiae::pi, -minutiae::pi, -0., 0., minutiae::pi, 2 * minutiae::pi}) {
            for (double angle : {std::nextafter(edge, -100.), edge, std::nextafter(edge, 100.)}) {
                double direct = std::remainder(angle, 2 * minutiae::pi);
                double fast = minutiae::wrap(angle);
                require(std::memcmp(&direct, &fast, sizeof(double)) == 0,
                        "Fast angle boundary/sign changed");
            }
        }
        std::vector<unsigned char> pixels(300 * 300);
        for (auto& p : pixels)
            p = static_cast<unsigned char>(random());
        for (unsigned count : {4u, 12u, 32u, 64u, 128u}) {
            auto oldA = std::make_unique<legacy_minutiae::orientation::Template>();
            auto oldB = std::make_unique<legacy_minutiae::orientation::Template>();
            legacy_minutiae::Feature baseA, baseB;
            minutiae::Feature a, b;
            a.count = b.count = baseA.count = baseB.count = count;
            for (unsigned i = 0; i < count; ++i) {
                float x = 35.f + float(random() % 220), y = 35.f + float(random() % 220);
                float angle = float((int(random() % 600) - 300) / 100.);
                baseA.points[i] = {x, y, angle, .9f};
                baseB.points[i] = {x + 3, y - 2, angle, .9f};
                a.points[i] = {x, y, angle, .9f};
                b.points[i] = {x + 3, y - 2, angle, .9f};
            }
            legacy_minutiae::describe(baseA);
            legacy_minutiae::describe(baseB);
            minutiae::describe(a);
            minutiae::describe(b);
            require(std::memcmp(baseA.descriptors, a.descriptors.data(), count * 1536) == 0,
                    "Descriptor changed");
            legacy_minutiae::orientation::encode(*oldA, baseA, pixels.data(), 300, 300, 500);
            legacy_minutiae::orientation::encode(*oldB, baseB, pixels.data(), 300, 300, 500);
            minutiae::orientation::Template first, second;
            minutiae::orientation::encode(first, a, pixels.data(), 300, 300, 500);
            minutiae::orientation::encode(second, b, pixels.data(), 300, 300, 500);
            require(std::memcmp(oldA->cells, first.cells.data(), first.cells.size() * 8) == 0,
                    "Orientation field changed");
            auto original = legacy_minutiae::orientation::compare(*oldA, *oldB).score;
            auto updated = minutiae::orientation::compare(first, second).score;
            require(std::abs(original - updated) <= 1e-12, "Comparison score changed");
            std::vector<unsigned char> buffer(capacity);
            int bytes = wire::encode(first, buffer.data());
            require(bytes == int(40 + count * 1552 + first.cells.size() * 8), "Wire size");
            auto decoded = wire::decode(buffer.data());
            require(minutiae::orientation::compare(decoded, second).score == updated,
                    "Codec round trip changed score");
            buffer[bytes - 1] ^= 1;
            try {
                wire::decode(buffer.data());
                throw std::runtime_error("Checksum accepted");
            } catch (const AlgorithmError& e) {
                require(e.code == TemplateChecksum, "Checksum code");
            }
            minutiae::graph::GeometryCache cache(a, b);
            for (unsigned i = 0; i < count; ++i)
                for (unsigned j = 0; j < count; ++j) {
                    double direct = minutiae::graph::compatibility(a.points[i], a.points[j],
                                                                   b.points[i], b.points[j]);
                    double cached = cache.compatibility({int(i), int(i), 1}, {int(j), int(j), 1});
                    require(direct == cached, "Geometry cache changes arithmetic");
                }
        }
        SYSTEM_INFO info{};
        GetSystemInfo(&info);
        auto memory = static_cast<unsigned char*>(
            VirtualAlloc(nullptr, 2 * info.dwPageSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        require(memory != nullptr, "Guard allocation");
        DWORD previous = 0;
        require(VirtualProtect(memory + info.dwPageSize, info.dwPageSize, PAGE_NOACCESS, &previous),
                "Guard protection");
        try {
            wire::decode(memory + info.dwPageSize - 8);
            throw std::runtime_error("Truncated header accepted");
        } catch (const AlgorithmError& e) {
            require(e.code == InvalidTemplate, "Truncation code");
        }
        VirtualFree(memory, 0, MEM_RELEASE);
        std::cout << "PASS: legacy scoring, descriptor/field parity, compact codec, cache "
                     "arithmetic, checksum and guarded truncation\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
