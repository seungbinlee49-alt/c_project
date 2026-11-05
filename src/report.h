#pragma once
#include "metrics.h"
inline std::string htmlEscape(const std::string& text) {
    std::string o;
    for (char c : text) {
        if (c == '&')
            o += "&amp;";
        else if (c == '<')
            o += "&lt;";
        else if (c == '>')
            o += "&gt;";
        else if (c == '\"')
            o += "&quot;";
        else
            o += c;
    }
    return o;
}
inline void writeReport(const fs::path& folder, const Metrics& m,
                        const std::vector<Sample>& samples, const std::string& title) {
    std::ofstream csv(folder / "curve.csv");
    csv << "threshold,FMR,FNMR\n" << std::setprecision(17);
    for (auto p : m.curve)
        csv << p.threshold << ',' << p.fmr << ',' << p.fnmr << '\n';
    auto startSvg = [](std::ostream& out, const char* heading, const char* x, const char* y) {
        out << "<svg xmlns='http://www.w3.org/2000/svg' width='800' height='480' viewBox='0 0 800 "
               "480'><rect width='800' height='480' fill='white'/><g font-family='sans-serif' "
               "font-size='14' fill='#243347'><text x='65' y='28' font-size='20'>"
            << heading
            << "</text><path d='M65 55V410H755' fill='none' stroke='#243347'/><text x='360' "
               "y='460'>"
            << x << "</text><text x='8' y='48'>" << y << "</text>";
        for (int k = 0; k <= 5; ++k) {
            int xx = 65 + 138 * k, yy = 410 - 71 * k;
            out << "<path d='M" << xx << " 55V410 M65 " << yy
                << "H755' stroke='#e7ebf0' fill='none'/><text x='" << xx - 8 << "' y='432'>"
                << k * 20 << "%</text><text x='22' y='" << yy + 4 << "'>" << k * 20 << "%</text>";
        }
    };
    {
        std::ofstream svg(folder / "threshold.svg");
        startSvg(svg, "Error rates by threshold", "Similarity threshold", "Error rate");
        for (int n = 0; n < 2; ++n) {
            svg << "<polyline fill='none' stroke='" << (n ? "#087f5b" : "#d94841")
                << "' stroke-width='2' points='";
            for (auto p : m.curve)
                svg << 65 + 690 * std::clamp(p.threshold, 0.0, 1.0) << ','
                    << 410 - 355 * (n ? p.fnmr : p.fmr) << ' ';
            svg << "'/>";
        }
        svg << "<text x='540' y='28' fill='#d94841'>FMR</text><text x='620' y='28' "
               "fill='#087f5b'>FNMR</text></g></svg>";
    }
    {
        std::ofstream svg(folder / "det.svg");
        startSvg(svg, "DET trade-off (linear axes)", "FMR", "FNMR");
        svg << "<polyline fill='none' stroke='#4263eb' stroke-width='2' points='";
        for (auto p : m.curve)
            svg << 65 + 690 * p.fmr << ',' << 410 - 355 * p.fnmr << ' ';
        svg << "'/></g></svg>";
    }
    {
        std::ofstream svg(folder / "scores.svg");
        startSvg(svg, "Successful score distribution", "Similarity score",
                 "Relative bin frequency");
        int hist[2][40]{};
        int totals[2]{};
        for (auto& s : samples)
            if (s.status == "OK") {
                int k = s.genuine ? 0 : 1;
                ++hist[k][std::min(39, int(s.score * 40))];
                ++totals[k];
            }
        for (int k = 0; k < 2; ++k)
            for (int b = 0; b < 40; ++b) {
                double height = totals[k] ? 355.0 * hist[k][b] / totals[k] : 0;
                svg << "<rect x='" << 65 + 690.0 * b / 40 << "' y='" << 410 - height
                    << "' width='17.25' height='" << height << "' fill='"
                    << (k ? "#d94841" : "#087f5b") << "' opacity='0.55'/>";
            }
        svg << "<text x='500' y='28' fill='#087f5b'>Genuine</text><text x='620' y='28' "
               "fill='#d94841'>Impostor</text></g></svg>";
    }
    std::ofstream html(folder / "report.html");
    html << "<!doctype html><html lang='en'><meta charset='utf-8'><title>Fingerprint "
            "evaluation</title><style>body{font:16px/1.6 "
            "system-ui;color:#243347;max-width:1000px;margin:40px auto;padding:0 "
            "24px}table{border-collapse:collapse;width:100%}td,th{padding:10px;border-bottom:1px "
            "solid "
            "#ddd;text-align:left}img{width:100%}code{background:#f1f3f5;padding:3px}</"
            "style><h1>Fingerprint evaluation</h1><p>"
         << htmlEscape(title)
         << "</p><p>Enrollment images are clean; probe noise is applied once per image. Extracted "
            "templates are cached within this run. Each comparison runs in an isolated process. "
            "Rates below are fractions (0–1). This is a development "
            "report.</p><table><tr><th>Metric</th><th>Value</th></tr>";
    auto row = [&](const char* name, const std::string& value) {
        html << "<tr><td>" << name << "</td><td>" << value << "</td></tr>";
    };
    row("Planned genuine", std::to_string(m.genuine));
    row("Planned impostor", std::to_string(m.impostor));
    row("Successful genuine", std::to_string(m.successG));
    row("Successful impostor", std::to_string(m.successI));
    row("EER", number(m.eer));
    row("EER* (successful scores only)", number(m.eerStar));
    row("ZeroFMR", number(m.zeroFmr));
    row("ZeroFNMR", number(m.zeroFnmr));
    row("FMR100", number(m.fmr100));
    row("FMR1000", number(m.fmr1000));
    html
        << "</table><p>Unavailable metrics are null. Enrollment and matching failures remain in "
           "the planned denominators. Consult metrics.json and records.jsonl for failures, "
           "timings, enrollment FTE and provenance.json for input hashes.</p><img src='scores.svg' "
           "alt='Score distribution'><img src='threshold.svg' alt='Error rates by threshold'><img "
           "src='det.svg' alt='Linear DET trade-off'><p>The DET plot currently uses linear axes; a "
           "logarithmic or normal-deviate view is a later presentation improvement. Zero error "
           "observed on this small database is not a population error guarantee.</p></html>";
    if (!html || !csv)
        throw std::runtime_error("Cannot save report");
}
