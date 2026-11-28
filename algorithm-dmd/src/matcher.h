#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <vector>
namespace dmd
{
constexpr float pi = 3.14159265358979323846f;
struct Point
{
    float x, y, theta;
}; // degrees, image-coordinate clockwise
struct Template
{
    unsigned width = 0, height = 0;
    std::vector<Point> points;
    std::vector<float> feature, mask;
};
inline float wrap(float x)
{
    return x - 360.0f * std::floor((x + 180.0f) / 360.0f);
}
inline float sigmoid(float x, float center, float slope)
{
    return 1.f / (1.f + std::exp(-slope * std::clamp(x - center, -1e10f, 100.f)));
}
// Hungarian shortest-augmenting-path assignment. Costs are transposed to match
// the official padded LSAR branch (gallery columns assigned to search rows).
inline std::array<int, 128> assign(const std::array<std::array<float, 128>, 128> &cost)
{
    constexpr int n = 128;
    std::array<double, 129> u{}, v{};
    std::array<int, 129> p{}, way{};
    for (int i = 1; i <= n; ++i)
    {
        p[0] = i;
        int j0 = 0;
        std::array<double, 129> minv;
        minv.fill(1e100);
        std::array<bool, 129> used{};
        do
        {
            used[j0] = true;
            int i0 = p[j0], j1 = 0;
            double delta = 1e100;
            for (int j = 1; j <= n; ++j)
                if (!used[j])
                {
                    double cur = cost[i0 - 1][j - 1] - u[i0] - v[j];
                    if (cur < minv[j])
                    {
                        minv[j] = cur;
                        way[j] = j0;
                    }
                    if (minv[j] < delta)
                    {
                        delta = minv[j];
                        j1 = j;
                    }
                }
            for (int j = 0; j <= n; ++j)
                if (used[j])
                {
                    u[p[j]] += delta;
                    v[j] -= delta;
                }
                else
                    minv[j] -= delta;
            j0 = j1;
        } while (p[j0] != 0);
        do
        {
            int j1 = way[j0];
            p[j0] = p[j1];
            j0 = j1;
        } while (j0);
    }
    std::array<int, 128> result{};
    for (int j = 1; j <= n; ++j)
        result[p[j] - 1] = j - 1;
    return result;
}
inline float similarity(const Template &a, unsigned i, const Template &b, unsigned j)
{
    float x1 = 0, x2 = 0, x12 = 0, n12 = 0;
    for (unsigned k = 0; k < 768; ++k)
    {
        float m = a.mask[i * 64 + k % 64] * b.mask[j * 64 + k % 64];
        float x = a.feature[i * 768 + k], y = b.feature[j * 768 + k];
        x1 += m * x * x;
        x2 += m * y * y;
        x12 += m * x * y;
        n12 += m;
    }
    return x12 / std::max(std::sqrt(x1) * std::sqrt(x2), 1e-3f) * std::sqrt(n12 / 5.f);
}
inline float compare(const Template &search, const Template &gallery)
{
    const unsigned ns = unsigned(search.points.size()), ng = unsigned(gallery.points.size());
    std::array<std::array<float, 128>, 128> cost{}, scores{};
    for (unsigned g = 0; g < 128; ++g)
        for (unsigned s = 0; s < 128; ++s)
        {
            bool valid = s < ns && g < ng;
            float value = valid ? similarity(search, s, gallery, g) : 0;
            scores[g][s] = value;
            cost[g][s] = valid ? 1 - value : 2;
        }
    auto assignments = assign(cost);
    std::array<float, 128> original{}, lambda{};
    std::array<bool, 128> valid{};
    for (unsigned g = 0; g < 128; ++g)
    {
        unsigned s = assignments[g];
        valid[g] = g < ng && s < ns;
        original[g] = lambda[g] = valid[g] ? scores[g][s] : 0;
    }
    std::array<std::array<float, 128>, 128> compatibility{};
    for (unsigned i = 0; i < 128; ++i)
        if (valid[i])
            for (unsigned j = 0; j < 128; ++j)
                if (i != j && valid[j])
                {
                    Point a = search.points[assignments[i]], b = search.points[assignments[j]], c = gallery.points[i],
                          d = gallery.points[j];
                    float d1 = std::abs(std::hypot(a.x - b.x, a.y - b.y) - std::hypot(c.x - d.x, c.y - d.y));
                    float d2 = std::abs(wrap(wrap(a.theta - b.theta) - wrap(c.theta - d.theta))) * pi / 180;
                    float ra = wrap(a.theta + std::atan2(a.y - b.y, b.x - a.x) * 180 / pi),
                          rb = wrap(c.theta + std::atan2(c.y - d.y, d.x - c.x) * 180 / pi);
                    float d3 = std::abs(wrap(ra - rb)) * pi / 180;
                    compatibility[i][j] =
                        sigmoid(d1, 5, -8.f / 5) * sigmoid(d2, pi / 12, -30) * sigmoid(d3, pi / 12, -30);
                }
    unsigned minimum = std::min(ns, ng);
    int nPair = 4 + int(std::nearbyint(sigmoid(float(minimum), 20, .4f) * 8));
    for (int iter = 0; iter < 5; ++iter)
    {
        std::array<float, 128> next{};
        for (unsigned i = 0; i < 128; ++i)
        {
            float support = 0;
            for (unsigned j = 0; j < 128; ++j)
                support += compatibility[i][j] * lambda[j];
            next[i] = .5f * lambda[i] + .5f * support / float(minimum - 1);
        }
        lambda = next;
    }
    std::array<int, 128> indices{};
    std::iota(indices.begin(), indices.end(), 0);
    auto efficiency = [&](int i) { return valid[i] ? lambda[i] / std::max(original[i], 1e-6f) : -INFINITY; };
    std::stable_sort(indices.begin(), indices.end(), [&](int a, int b) { return efficiency(a) > efficiency(b); });
    float score = 0;
    for (int k = 0; k < nPair; ++k)
        score += lambda[indices[k]];
    return score / nPair;
}
} // namespace dmd
