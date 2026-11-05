#include "image.h"
#include "process.h"
#include "provenance.h"
#include "report.h"
#include <iostream>
#include <random>
#include <regex>
#include <set>
struct Pair {
    std::string id, db;
    int type = 0;
    bool genuine = false;
    fs::path enroll, probe;
};
struct Image {
    std::string finger;
    int capture;
    fs::path path;
};
Image identify(const fs::path& path) {
    std::smatch match;
    std::string stem = path.stem().u8string();
    if (!std::regex_match(stem, match, std::regex("([0-9]+)_([0-9]+)")))
        throw std::runtime_error("Image filename must be finger_capture.tif");
    return {match[1], std::stoi(match[2]), fs::canonical(path)};
}
std::vector<Pair> readPlan(const fs::path& path) {
    std::ifstream file(path);
    std::string line;
    std::getline(file, line);
    if (line.rfind("FPPLAN\t1\t", 0) != 0)
        throw std::runtime_error("Unsupported plan format");
    std::vector<Pair> pairs;
    std::set<std::string> ids, duplicates;
    std::map<std::string, std::pair<int, std::string>> identities;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty())
            continue;
        auto f = split(line);
        if (f.size() != 7)
            throw std::runtime_error("Plan row must have 7 fields");
        Pair p;
        p.id = f[0];
        p.type = std::stoi(f[1]);
        p.db = f[2];
        p.genuine = f[3] == "G";
        if (p.id.empty() || p.db.empty() || (f[3] != "G" && f[3] != "I") || !supportedType(p.type))
            throw std::runtime_error("Invalid plan row");
        p.enroll = fs::canonical(fs::u8path(f[4]));
        p.probe = fs::canonical(fs::u8path(f[5]));
        if (f[6] != "1")
            throw std::runtime_error("Unsupported plan row version");
        if (fs::equivalent(p.enroll, p.probe))
            throw std::runtime_error("Same image in plan");
        if ((identify(p.enroll).finger == identify(p.probe).finger) != p.genuine)
            throw std::runtime_error("Plan label does not match image identities");
        if (!ids.insert(p.id).second ||
            !duplicates.insert(p.db + '\t' + utf8(p.enroll) + '\t' + utf8(p.probe)).second)
            throw std::runtime_error("Duplicate plan pair");
        for (auto& image : {p.enroll, p.probe}) {
            auto key = utf8(image);
            auto identity = std::make_pair(p.type, p.db);
            auto it = identities.find(key);
            if (it != identities.end() && it->second != identity)
                throw std::runtime_error("Image assigned conflicting DB identities");
            identities[key] = identity;
        }
        pairs.push_back(p);
    }
    if (pairs.empty())
        throw std::runtime_error("Empty plan");
    return pairs;
}
void createPlan(const fs::path& database, int type, const fs::path& output, unsigned seed,
                const std::string& policy) {
    if (!supportedType(type) || (policy != "first" && policy != "all"))
        throw std::runtime_error("Use a supported iType and policy first/all");
    if (fs::exists(output))
        throw std::runtime_error("Plan output already exists");
    std::vector<Image> images;
    for (auto& entry : fs::directory_iterator(database))
        if (entry.is_regular_file()) {
            auto ext = entry.path().extension().wstring();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
            if (ext == L".tif" || ext == L".tiff" || ext == L".bmp" || ext == L".png" ||
                ext == L".raw")
                images.push_back(identify(entry.path()));
        }
    std::sort(images.begin(), images.end(), [](auto& a, auto& b) {
        return std::tie(a.finger, a.capture) < std::tie(b.finger, b.capture);
    });
    std::vector<Pair> pairs;
    std::set<std::pair<std::string, int>> seen;
    std::map<std::string, int> first;
    for (auto& image : images) {
        if (!seen.emplace(image.finger, image.capture).second)
            throw std::runtime_error("Duplicate finger/capture");
        if (!first.count(image.finger))
            first[image.finger] = image.capture;
    }
    if (first.size() < 2)
        throw std::runtime_error("At least two finger identities are needed");
    for (auto& a : images)
        if (policy == "all" || a.capture == first[a.finger])
            for (auto& b : images)
                if (a.path != b.path)
                    pairs.push_back({std::to_string(pairs.size() + 1),
                                     utf8(fs::canonical(database).filename()), type,
                                     a.finger == b.finger, a.path, b.path});
    std::mt19937 random(seed);
    std::shuffle(pairs.begin(), pairs.end(), random);
    std::ofstream out(output);
    if (!out)
        throw std::runtime_error("Cannot create plan");
    out << "FPPLAN\t1\t" << seed << '\t' << policy << '\n';
    size_t g = 0;
    for (auto& p : pairs) {
        for (auto& s : {p.db, utf8(p.enroll), utf8(p.probe)})
            safeField(s);
        out << p.id << '\t' << type << '\t' << p.db << '\t' << (p.genuine ? 'G' : 'I') << '\t'
            << utf8(p.enroll) << '\t' << utf8(p.probe) << "\t1\n";
        if (p.genuine)
            ++g;
    }
    if (!out)
        throw std::runtime_error("Cannot save plan");
    std::cout << "Plan: " << pairs.size() << " pairs (" << g << " genuine, " << pairs.size() - g
              << " impostor)\n";
}
struct Extraction {
    fs::path image, feature;
    int type;
    Outcome outcome;
};
struct RunState {
    fs::path path;
    size_t planned = 0, completed = 0;
    bool finished = false;
    void write(const char* status) {
        std::ofstream out(path);
        out << "{\"status\":" << json(status) << ",\"completed_pairs\":" << completed
            << ",\"planned_pairs\":" << planned << "}\n";
        if (!out)
            throw std::runtime_error("Cannot save run state");
    }
    ~RunState() {
        if (!finished)
            try {
                write("FAILED");
            } catch (...) {
            }
    }
};
struct CancelMonitor {
    std::atomic<bool> stop{false};
    std::thread thread;
    explicit CancelMonitor(fs::path file)
        : thread([this, file] {
              while (!stop) {
                  std::error_code ec;
                  if (fs::exists(file, ec)) {
                      cancelled = true;
                      break;
                  }
                  std::this_thread::sleep_for(std::chrono::milliseconds(40));
              }
          }) {}
    ~CancelMonitor() {
        stop = true;
        thread.join();
    }
};
void run(const fs::path& plan, const fs::path& dll, const fs::path& output, DWORD timeout,
         unsigned workers, const std::string& noise, unsigned seed) {
    if (workers < 1 || workers > 64 || !timeout || timeout == INFINITE)
        throw std::runtime_error("Invalid workers/timeout");
    const std::set<std::string> profiles{"clean",    "gaussian15", "gaussian30",
                                         "impulse2", "blur1",      "brightness"};
    if (!profiles.count(noise))
        throw std::runtime_error("Unknown noise profile");
    auto pairs = readPlan(plan);
    auto algorithm = fs::canonical(dll);
    hcheck(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
    struct ComCleanup {
        ~ComCleanup() {
            CoUninitialize();
        }
    } comCleanup;
    std::map<fs::path, std::string> pixelHashes;
    for (auto& pair : pairs)
        for (auto& image : {pair.enroll, pair.probe})
            if (!pixelHashes.count(image)) {
                UINT w = 0, h = 0;
                dimensions(pair.type, w, h);
                try {
                    pixelHashes[image] = std::to_string(w) + "x" + std::to_string(h) + ":" +
                                         sha256Bytes(loadImage(image, w, h));
                } catch (const std::exception&) {
                    pixelHashes[image] = "";
                } // The worker records decoding failures as actual failed attempts.
            }
    for (auto& pair : pairs)
        if (!pixelHashes[pair.enroll].empty() &&
            pixelHashes[pair.enroll] == pixelHashes[pair.probe])
            throw std::runtime_error("Identical decoded image pixels in comparison plan");
    if (fs::exists(output))
        throw std::runtime_error("Run directory already exists; choose a new directory");
    fs::create_directories(output / "templates");
    fs::create_directory(output / "tasks");
    fs::copy_file(plan, output / "plan.tsv");
    RunState state{output / "run.json", pairs.size()};
    state.write("RUNNING");
    CancelMonitor cancellation(output / "cancel.request");
    wchar_t exe[32768];
    auto length = GetModuleFileNameW(nullptr, exe, 32768);
    if (!length || length >= 32768)
        throw std::runtime_error("Cannot find executable directory");
    auto task = fs::path(exe).parent_path() / "fp_task.exe";
    // Hash the exact inputs before execution, including each distinct image.
    std::ofstream provenance(output / "provenance.json");
    provenance << "{\"schema_version\":1,\"dll\":" << json(utf8(algorithm))
               << ",\"dll_sha256\":" << json(sha256(algorithm))
               << ",\"plan_sha256\":" << json(sha256(plan)) << ",\"noise\":" << json(noise)
               << ",\"noise_seed\":" << seed << ",\"workers\":" << workers
               << ",\"timeout_ms\":" << timeout << ",\"images\":[";
    std::set<fs::path> imagePaths;
    for (auto& p : pairs) {
        imagePaths.insert(p.enroll);
        imagePaths.insert(p.probe);
    }
    bool comma = false;
    for (auto& image : imagePaths) {
        if (comma)
            provenance << ',';
        comma = true;
        provenance << "{\"path\":" << json(utf8(image)) << ",\"sha256\":" << json(sha256(image))
                   << ",\"decoded_pixel_identity\":" << json(pixelHashes[image]) << "}";
    }
    provenance << "],\"worker_sha256\":" << json(sha256(task))
               << ",\"extractor_dependency_sha256\":";
    auto dependency = algorithm.parent_path() / "FRFXLL.dll";
    if (fs::exists(dependency))
        provenance << json(sha256(dependency));
    else
        provenance << "null";
    provenance << "}\n";
    provenance.close();
    if (!provenance)
        throw std::runtime_error("Cannot save provenance");
    std::map<fs::path, size_t> enrollIndex, attemptIndex;
    std::vector<Extraction> enrolls, attempts;
    for (auto& p : pairs) {
        if (!enrollIndex.count(p.enroll)) {
            auto i = enrolls.size();
            enrollIndex[p.enroll] = i;
            enrolls.push_back(
                {p.enroll, output / "templates" / ("e" + std::to_string(i) + ".bin"), p.type, {}});
        }
        if (!attemptIndex.count(p.probe)) {
            auto i = attempts.size();
            attemptIndex[p.probe] = i;
            attempts.push_back(
                {p.probe, output / "templates" / ("a" + std::to_string(i) + ".bin"), p.type, {}});
        }
    }
    std::ofstream records(output / "records.jsonl");
    if (!records)
        throw std::runtime_error("Cannot save records");
    std::mutex recordLock;
    auto record = [&](const std::string& text) {
        std::lock_guard<std::mutex> guard(recordLock);
        records << text << '\n';
        records.flush();
        if (!records)
            throw std::runtime_error("Cannot append record");
    };
    auto extract = [&](std::vector<Extraction>& list, const std::string& mode) {
        std::cout << mode << ": " << list.size() << " unique images\n";
        parallelFor(list.size(), workers, [&](size_t i) {
            auto& e = list[i];
            auto result = output / "tasks" / (mode + std::to_string(i) + ".tsv");
            // Stable per-image seed: persisted input path order is deterministic.
            unsigned imageSeed = seed;
            for (unsigned char c : utf8(e.image.filename()))
                imageSeed = (imageSeed ^ c) * 16777619u;
            e.outcome =
                runTask(task,
                        {fs::u8path(mode).wstring(), algorithm.wstring(), std::to_wstring(e.type),
                         e.image.wstring(), L"-", e.feature.wstring(), result.wstring(),
                         fs::u8path(noise).wstring(), std::to_wstring(imageSeed)},
                        result, timeout);
            record("{\"kind\":" + json(mode) + ",\"image\":" + json(utf8(e.image)) +
                   ",\"outcome\":" + outcomeJson(e.outcome) + "}");
        });
    };
    extract(enrolls, "enroll");
    if (!cancelled)
        extract(attempts, "attempt");
    std::vector<Sample> samples(pairs.size());
    for (auto& s : samples)
        s.status = "PENDING";
    std::atomic<size_t> done{0};
    std::cout << "compare: " << pairs.size() << " pairs\n";
    parallelFor(pairs.size(), workers, [&](size_t i) {
        auto& p = pairs[i];
        auto& e = enrolls[enrollIndex.at(p.enroll)];
        auto& a = attempts[attemptIndex.at(p.probe)];
        Outcome outcome;
        if (e.outcome.status != "OK") {
            outcome = e.outcome;
            outcome.stage = "enroll";
        } else if (a.outcome.status != "OK") {
            outcome = a.outcome;
            outcome.stage = "attempt";
        } else {
            auto result = output / "tasks" / ("c" + std::to_string(i) + ".tsv");
            outcome = runTask(task,
                              {L"compare", algorithm.wstring(), std::to_wstring(p.type),
                               e.feature.wstring(), a.feature.wstring(), L"-", result.wstring(),
                               L"clean", L"0"},
                              result, timeout);
        }
        samples[i] = {p.genuine,     outcome.status,
                      outcome.stage, outcome.score,
                      outcome.ms,    outcome.status == "OK" ? a.outcome.ms + outcome.ms : 0};
        record("{\"kind\":\"pair\",\"pair_id\":" + json(p.id) + ",\"database\":" + json(p.db) +
               ",\"genuine\":" + (p.genuine ? "true" : "false") +
               ",\"outcome\":" + outcomeJson(outcome) + "}");
        auto completed = ++done;
        if (completed % 250 == 0) {
            std::lock_guard<std::mutex> guard(recordLock);
            std::cout << completed << " / " << pairs.size() << "\n";
        }
    });
    state.completed = done;
    if (cancelled) {
        state.write("CANCELLED");
        state.finished = true;
        std::cout << "Cancelled; partial records kept; final metrics omitted.\n";
        return;
    }
    auto metrics = calculate(samples);
    size_t failedEnroll = 0;
    double enrollMs = 0, attemptMs = 0, compareMs = 0;
    size_t enrollOK = 0, attemptOK = 0, compareOK = 0, maxBytes = 0, totalBytes = 0;
    for (auto& e : enrolls) {
        if (e.outcome.status != "OK")
            ++failedEnroll;
        else {
            enrollMs += e.outcome.ms;
            ++enrollOK;
            totalBytes += e.outcome.bytes;
            maxBytes = std::max(maxBytes, size_t(e.outcome.bytes));
        }
    }
    for (auto& a : attempts)
        if (a.outcome.status == "OK") {
            attemptMs += a.outcome.ms;
            ++attemptOK;
        }
    size_t genuineAttempted = 0, impostorAttempted = 0, genuineRejected = 0, impostorRejected = 0;
    double genuineMatchingMs = 0, impostorMatchingMs = 0;
    for (auto& s : samples)
        if (s.status == "OK") {
            if (s.genuine)
                genuineMatchingMs += s.matchingMs;
            else
                impostorMatchingMs += s.matchingMs;
        }
    for (auto& s : samples) {
        if (s.stage != "enroll") {
            if (s.genuine) {
                ++genuineAttempted;
                if (s.status != "OK")
                    ++genuineRejected;
            } else {
                ++impostorAttempted;
                if (s.status != "OK")
                    ++impostorRejected;
            }
        }
        if (s.status == "OK") {
            compareMs += s.ms;
            ++compareOK;
        }
    }
    auto ratio = [](size_t a, size_t b) -> std::optional<double> {
        if (!b)
            return {};
        return double(a) / b;
    };
    std::ofstream m(output / "metrics.json");
    m << std::setprecision(17) << "{\n\"planned_genuine\":" << metrics.genuine
      << ",\"planned_impostor\":" << metrics.impostor
      << ",\n\"successful_genuine\":" << metrics.successG
      << ",\"successful_impostor\":" << metrics.successI
      << ",\n\"unique_enrollment_attempts\":" << enrolls.size()
      << ",\"enrollment_failures\":" << failedEnroll
      << ",\"FTE\":" << number(ratio(failedEnroll, enrolls.size()))
      << ",\n\"genuine_enrollment_blocked\":" << metrics.enrollFailedG
      << ",\"genuine_failed_total\":" << metrics.failedG
      << ",\"impostor_failed_total\":" << metrics.failedI
      << ",\n\"REJgenuine\":" << number(ratio(genuineRejected, genuineAttempted))
      << ",\"REJimpostor\":" << number(ratio(impostorRejected, impostorAttempted))
      << ",\n\"EER\":" << number(metrics.eer)
      << ",\"EER_low\":" << (metrics.eer ? number(metrics.eerLow) : "null")
      << ",\"EER_high\":" << (metrics.eer ? number(metrics.eerHigh) : "null")
      << ",\"EER_star\":" << number(metrics.eerStar) << ",\n\"ZeroFMR\":" << number(metrics.zeroFmr)
      << ",\"ZeroFNMR\":" << number(metrics.zeroFnmr) << ",\"FMR100\":" << number(metrics.fmr100)
      << ",\"FMR1000\":" << number(metrics.fmr1000) << ",\n\"mean_successful_enroll_ms\":"
      << number(enrollOK ? std::optional<double>(enrollMs / enrollOK) : std::nullopt)
      << ",\"mean_successful_attempt_ms\":"
      << number(attemptOK ? std::optional<double>(attemptMs / attemptOK) : std::nullopt)
      << ",\"mean_successful_compare_ms\":"
      << number(compareOK ? std::optional<double>(compareMs / compareOK) : std::nullopt)
      << ",\n\"mean_successful_template_bytes\":"
      << number(enrollOK ? std::optional<double>(double(totalBytes) / enrollOK) : std::nullopt)
      << ",\"max_template_bytes\":" << maxBytes << "\n}\n";
    if (!m)
        throw std::runtime_error("Cannot save metrics");
    writeReport(output, metrics, samples, utf8(plan.filename()) + " / " + noise);
    // Export in plan order, independent of thread completion order. This is the
    // canonical input for later offline analysis and GUI result inspection.
    std::ofstream pairsTable(output / "pairs.tsv");
    pairsTable << "pair_id\tkind\tstatus\tstage\tscore\tcompare_ms\tmatching_ms\n"
               << std::setprecision(17);
    for (size_t i = 0; i < pairs.size(); ++i) {
        auto& s = samples[i];
        pairsTable << pairs[i].id << '\t' << (s.genuine ? 'G' : 'I') << '\t' << s.status << '\t'
                   << s.stage << '\t';
        if (s.status == "OK")
            pairsTable << s.score;
        pairsTable << '\t' << s.ms << '\t' << s.matchingMs << '\n';
    }
    std::ofstream timing(output / "timing.json");
    timing
        << std::setprecision(17)
        << "{\"policy\":\"Successful logical matching time = cached attempt extraction call time + "
           "measured compare call time; excludes IO/process startup\",\"mean_matching_ms\":"
        << number(compareOK
                      ? std::optional<double>((genuineMatchingMs + impostorMatchingMs) / compareOK)
                      : std::nullopt)
        << ",\"mean_genuine_matching_ms\":"
        << number(metrics.successG ? std::optional<double>(genuineMatchingMs / metrics.successG)
                                   : std::nullopt)
        << ",\"mean_impostor_matching_ms\":"
        << number(metrics.successI ? std::optional<double>(impostorMatchingMs / metrics.successI)
                                   : std::nullopt)
        << "}\n";
    if (!pairsTable || !timing)
        throw std::runtime_error("Cannot save timing/pair tables");
    size_t acquisitionFailures = attempts.size() - attemptOK;
    auto fte = ratio(failedEnroll, enrolls.size()),
         fta = ratio(acquisitionFailures, attempts.size());
    std::ofstream readiness(output / "reference-checks.json");
    readiness << std::setprecision(17)
              << "{\"reference\":\"K-NBTC guide 2025-11, "
                 "pp15-16\",\"official_test\":false,\"certification_status\":\"NOT_ASSESSED\","
                 "\"data_role\":\"DEVELOPMENT_UNLESS_INDEPENDENT_PROVENANCE_ESTABLISHED\",\"FTE\":"
              << number(fte) << ",\"FTA\":" << number(fta)
              << ",\"unique_attempt_extractions\":" << attempts.size()
              << ",\"attempt_extraction_failures\":" << acquisitionFailures
              << ",\"local_eer_at_most_1_percent\":"
              << (metrics.eer ? (*metrics.eer <= 0.01 ? "true" : "false") : "null")
              << ",\"local_fte_fta_below_5_percent\":"
              << (fte && fta ? (*fte < 0.05 && *fta < 0.05 ? "true" : "false") : "null")
              << ",\"reason\":\"Official four-sensor datasets and independent assessment have not "
                 "been performed; local checks do not imply certification\"}\n";
    if (!readiness)
        throw std::runtime_error("Cannot save reference checks");
    state.write("COMPLETE");
    state.finished = true;
    std::cout << "Complete. EER=" << number(metrics.eer) << ", EER*=" << number(metrics.eerStar)
              << ", FTE=" << number(ratio(failedEnroll, enrolls.size())) << "\n";
}
int wmain(int argc, wchar_t** argv) {
    SetConsoleCtrlHandler(stopHandler, TRUE);
    try {
        if (argc == 7 && std::wstring(argv[1]) == L"plan")
            createPlan(argv[2], std::stoi(argv[3]), argv[4],
                       static_cast<unsigned>(std::stoul(argv[5])), utf8(argv[6]));
        else if (argc == 9 && std::wstring(argv[1]) == L"run")
            run(fs::absolute(argv[2]), argv[3], fs::absolute(argv[4]), std::stoul(argv[5]),
                static_cast<unsigned>(std::stoul(argv[6])), utf8(argv[7]),
                static_cast<unsigned>(std::stoul(argv[8])));
        else {
            std::cerr << "Usage:\n  fp_bench plan DB iType plan.tsv seed first|all\n  fp_bench run "
                         "plan.tsv DLL new_output_dir timeout_ms workers noise seed\n";
            return 2;
        }
        return cancelled ? 130 : 0;
    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << '\n';
        return 2;
    }
}
