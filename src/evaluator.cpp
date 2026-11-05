#include "api.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <windows.h>
namespace fs = std::filesystem;
std::wstring quote(const fs::path& p) {
    return L"\"" + p.wstring() + L"\"";
}
int wmain(int argc, wchar_t** argv) {
    if (argc != 7) {
        std::wcerr << L"Usage: fp_eval DLL iType enroll.tif attempt.tif result.json timeout_ms\n";
        return 2;
    }
    try {
        auto dll = fs::absolute(argv[1]), a = fs::absolute(argv[3]), b = fs::absolute(argv[4]),
             result = fs::absolute(argv[5]);
        if (fs::exists(result) &&
            (fs::equivalent(result, a) || fs::equivalent(result, b) || fs::equivalent(result, dll)))
            throw std::runtime_error("Result must not overwrite an input");
        if (fs::equivalent(a, b))
            throw std::runtime_error("Same image is forbidden");
        int type = std::stoi(argv[2]);
        if (!supportedType(type))
            throw std::runtime_error("Invalid iType");
        auto timeout = std::stoul(argv[6]);
        if (timeout == 0 || timeout >= INFINITE)
            throw std::runtime_error("Invalid timeout");
        wchar_t exe[32768];
        DWORD len = GetModuleFileNameW(nullptr, exe, 32768);
        if (!len || len >= 32768)
            throw std::runtime_error("Executable path failed");
        fs::path worker = fs::path(exe).parent_path() / L"fp_worker.exe";
        fs::path temp = result;
        temp += L".worker." + std::to_wstring(GetCurrentProcessId());
        std::wstring command = quote(worker) + L" " + quote(dll) + L" " + std::to_wstring(type) +
                               L" " + quote(a) + L" " + quote(b) + L" " + quote(temp);
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        auto start = std::chrono::steady_clock::now();
        if (!CreateProcessW(worker.c_str(), command.data(), nullptr, nullptr, FALSE,
                            CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
            throw std::runtime_error("Cannot start worker");
        CloseHandle(pi.hThread);
        DWORD wait = WaitForSingleObject(pi.hProcess, DWORD(timeout)), code = 0;
        if (wait != WAIT_OBJECT_0) {
            TerminateProcess(pi.hProcess, 124);
            WaitForSingleObject(pi.hProcess, 5000);
        }
        GetExitCodeProcess(pi.hProcess, &code);
        CloseHandle(pi.hProcess);
        std::string payload;
        if (wait == WAIT_TIMEOUT)
            payload = "{\"status\":\"TIMEOUT\"}";
        else if (wait != WAIT_OBJECT_0)
            payload = "{\"status\":\"FAIL\",\"stage\":\"wait\"}";
        else if (code != 0)
            payload = "{\"status\":\"CRASH\",\"exit_code\":" + std::to_string(code) + "}";
        else {
            std::ifstream in(temp);
            payload.assign(std::istreambuf_iterator<char>(in), {});
            if (payload.empty())
                payload = "{\"status\":\"FAIL\",\"message\":\"Missing worker output\"}";
        }
        fs::remove(temp);
        double elapsed =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                .count();
        std::ofstream out(result);
        if (!out)
            throw std::runtime_error("Cannot write result");
        out << "{\"schema_version\":1,\"iType\":" << type << ",\"elapsed_ms\":" << elapsed
            << ",\"result\":" << payload << "}\n";
        std::cout << payload << "\n";
        return payload.find("\"OK\"") != std::string::npos ? 0 : 1;
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 2;
    }
}
