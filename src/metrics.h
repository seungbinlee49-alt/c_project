#pragma once
#include "core.h"
#include <optional>
struct Sample {
    bool genuine = false;
    std::string status, stage;
    double score = 0, ms = 0, matchingMs = 0;
};
struct Point {
    double threshold = 0, fmr = 0, fnmr = 0;
};
struct Metrics {
    size_t genuine = 0, impostor = 0, successG = 0, successI = 0, enrollFailedG = 0, failedG = 0,
           failedI = 0;
    std::vector<Point> curve, starCurve;
    std::optional<double> eer, eerStar, zeroFmr, zeroFnmr, fmr100, fmr1000;
    double eerLow = 0, eerHigh = 0;
};
inline std::vector<Point> makeCurve(const std::vector<double>& genuine,
                                    const std::vector<double>& impostor, size_t ng, size_t ni,
                                    size_t failedG) {
    std::vector<Point> points;
    if (!ng || !ni)
        return points;
    std::vector<double> g = genuine, i = impostor, thresholds{0, 1, std::nextafter(1.0, 2.0)};
    std::sort(g.begin(), g.end());
    std::sort(i.begin(), i.end());
    for (double s : g) {
        thresholds.push_back(s);
        thresholds.push_back(std::nextafter(s, 2.0));
    }
    for (double s : i) {
        thresholds.push_back(s);
        thresholds.push_back(std::nextafter(s, 2.0));
    }
    std::sort(thresholds.begin(), thresholds.end());
    thresholds.erase(std::unique(thresholds.begin(), thresholds.end()), thresholds.end());
    for (double t : thresholds) {
        auto rejected = std::lower_bound(g.begin(), g.end(), t) - g.begin();
        auto accepted = i.end() - std::lower_bound(i.begin(), i.end(), t);
        points.push_back({t, double(accepted) / ni, double(failedG + rejected) / ng});
    }
    return points;
}
inline std::optional<double> estimateEer(const std::vector<Point>& curve, double& low,
                                         double& high) {
    if (curve.empty())
        return {};
    std::optional<Point> t1, t2;
    for (auto p : curve) {
        if (p.fnmr <= p.fmr)
            t1 = p;
        if (p.fnmr >= p.fmr && !t2)
            t2 = p;
    }
    if (!t1 || !t2)
        return {};
    // Project p8: choose the bracket with smaller FMR+FNMR, average its bounds.
    auto p = (t1->fmr + t1->fnmr <= t2->fmr + t2->fnmr) ? *t1 : *t2;
    low = std::min(p.fmr, p.fnmr);
    high = std::max(p.fmr, p.fnmr);
    return (low + high) / 2;
}
inline Metrics calculate(const std::vector<Sample>& samples) {
    Metrics m;
    std::vector<double> g, i;
    for (auto& s : samples) {
        if (s.status == "CANCELLED" || s.status == "PENDING")
            throw std::runtime_error("Incomplete run cannot produce final metrics");
        if (s.genuine)
            ++m.genuine;
        else
            ++m.impostor;
        if (s.status == "OK") {
            if (!std::isfinite(s.score) || s.score < 0 || s.score > 1)
                throw std::runtime_error("Invalid metric score");
            (s.genuine ? g : i).push_back(s.score);
        } else if (s.genuine) {
            ++m.failedG;
            if (s.stage == "enroll")
                ++m.enrollFailedG;
        } else
            ++m.failedI;
    }
    m.successG = g.size();
    m.successI = i.size();
    m.curve = makeCurve(g, i, m.genuine, m.impostor, m.failedG);
    m.starCurve = makeCurve(g, i, g.size(), i.size(), 0);
    m.eer = estimateEer(m.curve, m.eerLow, m.eerHigh);
    double lo = 0, hi = 0;
    m.eerStar = estimateEer(m.starCurve, lo, hi);
    for (auto p : m.curve) {
        auto update = [](std::optional<double>& v, double x) {
            if (!v || x < *v)
                v = x;
        };
        if (p.fmr == 0)
            update(m.zeroFmr, p.fnmr);
        if (p.fnmr == 0)
            update(m.zeroFnmr, p.fmr);
        if (p.fmr <= 0.01)
            update(m.fmr100, p.fnmr);
        if (p.fmr <= 0.001)
            update(m.fmr1000, p.fnmr);
    }
    return m;
}
inline std::string number(std::optional<double> v) {
    if (!v)
        return "null";
    std::ostringstream s;
    s << std::setprecision(17) << *v;
    return s.str();
}
