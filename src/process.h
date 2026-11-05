#pragma once
#include "core.h"
#include <atomic>
#include <functional>
#include <mutex>
#include <thread>
inline std::atomic<bool> cancelled{false};
inline BOOL WINAPI stopHandler(DWORD type) {
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT) {
        cancelled = true;
        return TRUE;
    }
    return FALSE;
}
// All arguments are quoted using the Windows CRT backslash/quote rules.
inline std::wstring winQuote(const std::wstring& s) {
    std::wstring out = L"\"";
    size_t slashes = 0;
    for (auto c : s) {
        if (c == L'\\') {
            ++slashes;
            continue;
        }
        if (c == L'"')
            out.append(slashes * 2 + 1, L'\\');
        else
            out.append(slashes, L'\\');
        slashes = 0;
        out += c;
    }
    out.append(slashes * 2, L'\\');
    out += L'"';
    return out;
}
inline Outcome runTask(const fs::path& executable, const std::vector<std::wstring>& args,
                       const fs::path& output, DWORD timeout) {
    Outcome r;
    if (cancelled) {
        r.status = "CANCELLED";
        return r;
    }
    std::wstring command = winQuote(executable.wstring());
    for (auto& a : args)
        command += L" " + winQuote(a);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (!job) {
        r.message = "Cannot create job object";
        return r;
    }
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
        CloseHandle(job);
        r.message = "Cannot configure job object";
        return r;
    }
    if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, nullptr, &si, &pi)) {
        CloseHandle(job);
        r.message = "Cannot start task process";
        return r;
    }
    if (!AssignProcessToJobObject(job, pi.hProcess)) {
        TerminateProcess(pi.hProcess, 125);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        CloseHandle(job);
        r.message = "Cannot isolate task process";
        return r;
    }
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    auto start = std::chrono::steady_clock::now();
    std::string forced;
    for (;;) {
        DWORD state = WaitForSingleObject(pi.hProcess, 20);
        if (state == WAIT_OBJECT_0)
            break;
        if (state == WAIT_FAILED) {
            forced = "FAIL";
            break;
        }
        if (cancelled) {
            forced = "CANCELLED";
            break;
        }
        if (std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                .count() >= timeout) {
            forced = "TIMEOUT";
            break;
        }
    }
    if (!forced.empty()) {
        TerminateJobObject(job, 124);
        WaitForSingleObject(pi.hProcess, 5000);
    }
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(job);
    r = readOutcome(output);
    r.exitCode = code;
    if (!forced.empty()) {
        r.status = forced;
        r.message = "Worker terminated by evaluator";
    } else if (code != 0) {
        r.status = "CRASH";
        r.message = "Worker exited abnormally";
    } else if (r.status == "RUNNING") {
        r.status = "FAIL";
        r.message = "Worker returned without final result";
    }
    return r;
}
inline void parallelFor(size_t count, unsigned workers, const std::function<void(size_t)>& work) {
    std::atomic<size_t> next{0};
    std::vector<std::thread> threads;
    std::exception_ptr error;
    std::mutex lock;
    for (unsigned t = 0; t < workers; ++t)
        threads.emplace_back([&] {
            try {
                for (;;) {
                    auto i = next++;
                    if (i >= count || cancelled)
                        break;
                    work(i);
                }
            } catch (...) {
                std::lock_guard<std::mutex> guard(lock);
                if (!error)
                    error = std::current_exception();
                cancelled = true;
            }
        });
    for (auto& t : threads)
        t.join();
    if (error)
        std::rethrow_exception(error);
}
