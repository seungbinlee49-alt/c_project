#include "../src/image.h"
#include "../src/metrics.h"
#include <iostream>
void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
bool approx(double a, double b) {
    return std::abs(a - b) < 1e-10;
}
int main() {
    try {
        auto perfect = calculate({{true, "OK", "compare", 0.9},
                                  {true, "OK", "compare", 0.8},
                                  {false, "OK", "compare", 0.2},
                                  {false, "OK", "compare", 0.3}});
        require(perfect.eer && approx(*perfect.eer, 0), "Perfect separation EER");
        require(perfect.zeroFmr && approx(*perfect.zeroFmr, 0), "ZeroFMR");
        auto tied = calculate({{true, "OK", "compare", 0.5}, {false, "OK", "compare", 0.5}});
        require(tied.eer && approx(*tied.eer, 0.5), "Tied scores EER");
        require(tied.zeroFmr && approx(*tied.zeroFmr, 1), "Tied scores use >= threshold");
        auto failures = calculate({{true, "FAIL", "enroll", 0},
                                   {true, "OK", "compare", 0.9},
                                   {false, "FAIL", "attempt", 0},
                                   {false, "OK", "compare", 0.1}});
        require(failures.enrollFailedG == 1 && failures.failedG == 1 && failures.failedI == 1,
                "Failure counters");
        require(failures.eerStar && approx(*failures.eerStar, 0), "EER* excludes failures");
        bool checked = false;
        for (auto p : failures.curve)
            if (p.threshold == 0) {
                require(approx(p.fmr, 0.5) && approx(p.fnmr, 0.5),
                        "Planned denominator includes failures");
                checked = true;
            }
        require(checked, "Threshold zero present");
        require(failures.zeroFnmr == std::nullopt, "No zero FNMR with genuine failure");
        auto onlyG = calculate({{true, "OK", "compare", 0.5}});
        require(!onlyG.eer, "Missing impostor is unavailable");
        bool rejected = false;
        try {
            calculate({{true, "PENDING", "", 0}});
        } catch (...) {
            rejected = true;
        }
        require(rejected, "Incomplete metrics rejected");
        rejected = false;
        try {
            calculate({{true, "OK", "compare", 2}});
        } catch (...) {
            rejected = true;
        }
        require(rejected, "Invalid score rejected");
        std::vector<unsigned char> pixels(64 * 64, 128), a = pixels, b = pixels;
        addNoise(a, 64, 64, "gaussian15", 42);
        addNoise(b, 64, 64, "gaussian15", 42);
        require(a == b && a != pixels, "Noise deterministic");
        auto c = pixels;
        addNoise(c, 64, 64, "blur1", 42);
        require(c == pixels, "Blur preserves constant image");
        std::cout << "PASS: metric edge cases and deterministic noise\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
