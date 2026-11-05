#include "../src/minutiae.h"
#include <iostream>
#include <memory>
#include <random>
using namespace minutiae;
void require(bool yes, const char* message) {
    if (!yes)
        throw std::runtime_error(message);
}
std::unique_ptr<Feature> synthetic(unsigned seed) {
    auto f = std::make_unique<Feature>();
    std::mt19937 rng(seed);
    f->count = 32;
    for (unsigned i = 0; i < f->count; ++i)
        f->points[i] = {float(70 + rng() % 260), float(70 + rng() % 300),
                        float(double(rng() % 10000) / 10000 * 2 * pi - pi), 0.8f};
    describe(*f);
    return f;
}
int main() {
    try {
        auto a = synthetic(17), b = std::make_unique<Feature>();
        b->count = a->count;
        double angle = 0.42, cs = std::cos(angle), sn = std::sin(angle);
        for (unsigned i = 0; i < a->count; ++i) {
            auto p = a->points[i];
            b->points[i] = {float(cs * p.x - sn * p.y + 160), float(sn * p.x + cs * p.y + 25),
                            float(wrap(p.theta + angle)), p.quality};
        }
        describe(*b);
        auto different = synthetic(99);
        double self = compare(*a, *a), rotated = compare(*a, *b), other = compare(*a, *different);
        require(self > 0.65, "Self-match should be strong");
        require(std::abs(self - rotated) < 0.02, "Rotation/translation invariance");
        require(rotated > other + 0.35, "Unrelated synthetic features should separate");
        double reverse = compare(*b, *a);
        require(std::abs(reverse - rotated) < 0.02, "Reverse comparison consistency");
        Diagnostics diagnostics;
        require(compare(*a, *b, &diagnostics) == rotated && diagnostics.score == rotated,
                "Tracing must not change the numerical score");
        require(diagnostics.hasAlignment && diagnostics.matches.size() == a->count,
                "Trace must retain the winning one-to-one correspondence");
        require(std::abs(std::atan2(diagnostics.transform.sn, diagnostics.transform.cs) - angle) <
                    1e-6,
                "Reported transform must be the actual winning transform");
        std::array<bool, maxPoints> usedA{}, usedB{};
        for (auto edge : diagnostics.matches) {
            require(!usedA[edge.a] && !usedB[edge.b], "Trace reused a point");
            usedA[edge.a] = usedB[edge.b] = true;
            auto p = a->points[edge.a], q = b->points[edge.b];
            auto t = diagnostics.transform;
            require(std::hypot(t.cs * p.x - t.sn * p.y + t.tx - q.x,
                               t.sn * p.x + t.cs * p.y + t.ty - q.y) <= positionTolerance,
                    "Reported pair violates matching geometry");
        }
        b->version = 999;
        require(!valid(*b), "Template version must be checked");
        b->version = 2;
        b->points[0].x = std::numeric_limits<float>::quiet_NaN();
        require(!valid(*b), "NaN coordinates must be rejected");
        a->count = 129;
        require(!valid(*a), "Template point count limit");
        std::cout << "PASS: rigid transformation, unrelated geometry, reverse order, malformed "
                     "templates. Scores "
                  << self << ' ' << rotated << ' ' << other << '\n';
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
