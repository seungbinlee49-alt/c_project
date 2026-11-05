#include "../src/minutiae_graph.h"
#include <iostream>
#include <memory>
#include <random>
using namespace minutiae;
void check(bool yes, const char* why) {
    if (!yes)
        throw std::runtime_error(why);
}
int main() {
    try {
        auto a = std::make_unique<Feature>(), b = std::make_unique<Feature>(),
             unrelated = std::make_unique<Feature>();
        a->count = b->count = unrelated->count = 40;
        std::mt19937 random(20261104);
        double angle = 0.65, cs = std::cos(angle), sn = std::sin(angle);
        for (unsigned i = 0; i < a->count; ++i) {
            auto& p = a->points[i];
            p = {float(100 + random() % 220), float(50 + random() % 280),
                 float(double(random() % 10000) / 10000 * 2 * pi - pi), 0.8f};
            b->points[i] = {float(cs * p.x - sn * p.y + 220), float(sn * p.x + cs * p.y + 30),
                            float(wrap(p.theta + angle)), 0.8f};
            unrelated->points[i] = {float(100 + random() % 220), float(50 + random() % 280),
                                    float(double(random() % 10000) / 10000 * 2 * pi - pi), 0.8f};
        }
        describe(*a);
        describe(*b);
        describe(*unrelated);
        double same = graph::compare(*a, *a), rigid = graph::compare(*a, *b),
               different = graph::compare(*a, *unrelated);
        check(std::abs(same - rigid) < 1e-5, "Rigid invariance");
        check(rigid > different + 0.4, "Unrelated geometry separation");
        auto p = a->points[0], q = a->points[1], u = b->points[0], v = b->points[1];
        check(graph::compatibility(p, q, u, v) > 0.999, "Rigid edges must remain consistent");
        v.theta = float(wrap(v.theta + 1.2));
        check(graph::compatibility(p, q, u, v) < 0.05,
              "Inconsistent relative directions must lose support");
        Diagnostics trace;
        check(graph::compare(*a, *b, &trace) == rigid && trace.score == rigid,
              "Trace score consistency");
        std::cout << "PASS: graph invariance, inconsistent-edge suppression, unrelated geometry "
                     "and tracing. "
                  << same << ' ' << rigid << ' ' << different << '\n';
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
