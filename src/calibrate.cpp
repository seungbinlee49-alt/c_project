#include "metrics.h"
#include "provenance.h"
#include <iostream>
#include <set>
std::vector<Sample> readSamples(const fs::path& path) {
    std::ifstream in(path);
    std::string line;
    std::getline(in, line);
    if (line != "pair_id\tkind\tstatus\tstage\tscore\tcompare_ms\tmatching_ms")
        throw std::runtime_error("Expected canonical pairs.tsv");
    std::vector<Sample> rows;
    std::set<std::string> ids;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        auto f = split(line);
        if (f.size() != 7 || (f[1] != "G" && f[1] != "I") || !ids.insert(f[0]).second)
            throw std::runtime_error("Invalid or duplicate sample row");
        if (f[2] != "OK" && f[2] != "FAIL" && f[2] != "TIMEOUT" && f[2] != "CRASH")
            throw std::runtime_error("Incomplete or unknown sample status");
        Sample s;
        s.genuine = f[1] == "G";
        s.status = f[2];
        s.stage = f[3];
        if (s.status == "OK")
            s.score = std::stod(f[4]);
        rows.push_back(s);
    }
    if (rows.empty())
        throw std::runtime_error("Empty sample file");
    calculate(rows);
    return rows;
}
int wmain(int argc, wchar_t** argv) {
    try {
        if (argc == 6 && std::wstring(argv[1]) == L"fit") {
            fs::path samplesFile = argv[2], output = argv[4];
            if (fs::exists(output))
                throw std::runtime_error("Threshold output exists");
            double target = std::stod(argv[3]);
            if (!std::isfinite(target) || target < 0 || target > 1)
                throw std::runtime_error("FMR target must be a fraction 0..1");
            auto samples = readSamples(samplesFile);
            auto metrics = calculate(samples);
            if (metrics.curve.empty())
                throw std::runtime_error(
                    "Both genuine and impostor calibration samples are required");
            auto it = std::find_if(metrics.curve.begin(), metrics.curve.end(),
                                   [&](auto p) { return p.fmr <= target; });
            if (it == metrics.curve.end())
                throw std::runtime_error("No admissible calibration threshold");
            std::string version = utf8(argv[5]);
            safeField(version);
            std::ofstream out(output);
            out << std::setprecision(17) << "FP_THRESHOLD\t1\nthreshold\t" << it->threshold
                << "\ntarget_fmr\t" << target << "\ncalibration_sha256\t" << sha256(samplesFile)
                << "\nalgorithm_release_id\t" << version << "\n";
            if (!out)
                throw std::runtime_error("Cannot save frozen threshold");
            std::cout << "Frozen threshold=" << it->threshold
                      << ". Observed calibration FMR=" << it->fmr << ", FNMR=" << it->fnmr
                      << ". This is not a population guarantee.\n";
            return 0;
        }
        if (argc == 6 && std::wstring(argv[1]) == L"evaluate") {
            fs::path thresholdFile = argv[2], samplesFile = argv[3], output = argv[4];
            if (fs::exists(output))
                throw std::runtime_error("Evaluation output exists");
            std::ifstream input(thresholdFile);
            std::string line;
            std::getline(input, line);
            if (line != "FP_THRESHOLD\t1")
                throw std::runtime_error("Invalid threshold format");
            std::map<std::string, std::string> fields;
            while (std::getline(input, line)) {
                auto f = split(line);
                if (f.size() != 2 || !fields.emplace(f[0], f[1]).second)
                    throw std::runtime_error("Invalid threshold field");
            }
            if (fields.at("calibration_sha256") == sha256(samplesFile))
                throw std::runtime_error(
                    "Cannot present calibration file itself as separate evaluation");
            if (fields.at("algorithm_release_id") != utf8(argv[5]))
                throw std::runtime_error("Threshold belongs to a different algorithm release");
            double threshold = std::stod(fields.at("threshold"));
            if (!std::isfinite(threshold) || threshold < 0 || threshold > std::nextafter(1.0, 2.0))
                throw std::runtime_error("Invalid threshold value");
            auto samples = readSamples(samplesFile);
            size_t ng = 0, ni = 0, falseMatches = 0, falseNonMatches = 0;
            for (auto& s : samples) {
                if (s.genuine) {
                    ++ng;
                    if (s.status != "OK" || s.score < threshold)
                        ++falseNonMatches;
                } else {
                    ++ni;
                    if (s.status == "OK" && s.score >= threshold)
                        ++falseMatches;
                }
            }
            std::ofstream out(output);
            out << std::setprecision(17) << "{\"threshold\":" << threshold
                << ",\"threshold_sha256\":" << json(sha256(thresholdFile))
                << ",\"evaluation_sha256\":" << json(sha256(samplesFile))
                << ",\"false_matches\":" << falseMatches << ",\"impostor_attempts\":" << ni
                << ",\"false_non_matches\":" << falseNonMatches << ",\"genuine_attempts\":" << ng
                << ",\"FMR\":"
                << number(ni ? std::optional<double>(double(falseMatches) / ni) : std::nullopt)
                << ",\"FNMR\":"
                << number(ng ? std::optional<double>(double(falseNonMatches) / ng) : std::nullopt)
                << ",\"threshold_refitted\":false,\"independence\":\"NOT_ESTABLISHED_BY_THIS_TOOL; "
                   "audit image/subject provenance separately\"}\n";
            if (!out)
                throw std::runtime_error("Cannot save fixed-threshold evaluation");
            std::cout << "Evaluated with frozen threshold; no threshold search was performed.\n";
            return 0;
        }
        std::cerr << "Usage:\n fp_calibrate fit development_pairs.tsv target_fmr new_threshold.tsv "
                     "release_id\n fp_calibrate evaluate threshold.tsv separate_pairs.tsv "
                     "new_result.json release_id\n";
        return 2;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 2;
    }
}
