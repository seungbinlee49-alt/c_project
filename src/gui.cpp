#include "process.h"
#include <atomic>
#include <commctrl.h>
#include <iostream>
#include <memory>
#include <shellapi.h>
#include <shobjidl.h>
#include <thread>
namespace {
constexpr UINT finishedMessage = WM_APP + 1;
constexpr int dllEdit = 101, dbEdit = 102, outEdit = 103, planEdit = 110, typeBox = 104,
              noiseBox = 105, workerEdit = 106, timeoutEdit = 107, seedEdit = 108, policyBox = 109,
              policyDetail = 111, genuineBox = 112, genuineDetail = 113, impostorBox = 114,
              impostorDetail = 115, startButton = 120, cancelButton = 121, reportButton = 122,
              inspectButton = 124, statusLabel = 130, progressBar = 131;
HWND window = nullptr;
HFONT font = nullptr;
std::thread background;
std::atomic<bool> active{false};
bool closing = false;
bool analysisOperation = false, cancelRequested = false;
fs::path runFolder, logPath, analysisOutput;
int runExit = 2;
std::wstring finalText;
HWND control(int id) {
    return GetDlgItem(window, id);
}
std::wstring value(int id) {
    auto h = control(id);
    int length = GetWindowTextLengthW(h);
    std::wstring text(length + 1, L'\0');
    GetWindowTextW(h, text.data(), length + 1);
    text.resize(length);
    return text;
}
void label(int id, const std::wstring& text) {
    SetWindowTextW(control(id), text.c_str());
}
HWND create(const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h,
            int id = 0) {
    auto c =
        CreateWindowExW((style & ES_AUTOHSCROLL) ? WS_EX_CLIENTEDGE : 0, cls, text,
                        WS_CHILD | WS_VISIBLE | style, x, y, w, h, window,
                        reinterpret_cast<HMENU>(INT_PTR(id)), GetModuleHandleW(nullptr), nullptr);
    SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    return c;
}
void browse(int id, bool folder) {
    IFileOpenDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&dialog))))
        return;
    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_FORCEFILESYSTEM | (folder ? FOS_PICKFOLDERS : 0));
    if (!folder) {
        COMDLG_FILTERSPEC filter = id == planEdit ? COMDLG_FILTERSPEC{L"저장된 시험 목록", L"*.tsv"}
                                                  : COMDLG_FILTERSPEC{L"지문인식 DLL", L"*.dll"};
        dialog->SetFileTypes(1, &filter);
    }
    if (SUCCEEDED(dialog->Show(window))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item))) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                label(id, path);
                if (id == dbEdit) {
                    std::wstring name = fs::path(path).filename().wstring();
                    int selected = name.find(L"2002") != std::wstring::npos   ? 1
                                   : name.find(L"2004") != std::wstring::npos ? 2
                                                                              : 0;
                    SendMessageW(control(typeBox), CB_SETCURSEL, selected, 0);
                }
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }
    dialog->Release();
}
std::wstring quoted(const std::wstring& s) {
    return winQuote(s);
}
int launch(const fs::path& exe, const std::vector<std::wstring>& args, HANDLE log) {
    std::wstring command = quoted(exe.wstring());
    for (auto& s : args)
        command += L" " + quoted(s);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = log;
    si.hStdError = log;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION pi{};
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (!job)
        throw std::runtime_error("Cannot create evaluation job");
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
        CloseHandle(job);
        throw std::runtime_error("Cannot configure evaluation job");
    }
    if (!CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, TRUE,
                        CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, nullptr, &si, &pi)) {
        CloseHandle(job);
        throw std::runtime_error("Cannot launch child process");
    }
    if (!AssignProcessToJobObject(job, pi.hProcess)) {
        TerminateProcess(pi.hProcess, 125);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        CloseHandle(job);
        throw std::runtime_error("Cannot isolate evaluation job");
    }
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 2;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(job);
    return int(code);
}
std::wstring selectionRule(int box, int detail) {
    int selected = int(SendMessageW(control(box), CB_GETCURSEL, 0, 0));
    if (selected == 0)
        return L"first";
    if (selected == 1)
        return L"all";
    if (selected == 2)
        return L"random:" + value(detail);
    if (selected == 3)
        return L"indices:" + value(detail);
    throw std::runtime_error("Choose a selection rule");
}
void ruleDetails(bool busy) {
    for (auto pair : {std::pair<int, int>{policyBox, policyDetail},
                      {genuineBox, genuineDetail},
                      {impostorBox, impostorDetail}})
        EnableWindow(control(pair.second),
                     !busy && SendMessageW(control(pair.first), CB_GETCURSEL, 0, 0) >= 2);
}
void setBusy(bool busy) {
    for (int id : {dllEdit, dbEdit, outEdit, typeBox, noiseBox, workerEdit, timeoutEdit, seedEdit,
                   policyBox, genuineBox, impostorBox, planEdit, startButton, 201, 202, 203, 204})
        EnableWindow(control(id), !busy);
    ruleDetails(busy);
    EnableWindow(control(cancelButton), busy);
    EnableWindow(control(reportButton), !busy && fs::exists(runFolder / "report.html"));
    EnableWindow(control(inspectButton), !busy && fs::exists(runFolder / "pairs.tsv"));
}
void start() {
    if (active)
        return;
    analysisOperation = false;
    try {
        bool reusePlan = !value(planEdit).empty();
        fs::path dll = fs::canonical(value(dllEdit)), db, out = fs::canonical(value(outEdit));
        if (!reusePlan)
            db = fs::canonical(value(dbEdit));
        if ((!reusePlan && !fs::is_directory(db)) || !fs::is_directory(out) ||
            !fs::is_regular_file(dll))
            throw std::runtime_error("Check input paths");
        unsigned workers = std::stoul(value(workerEdit)), timeout = std::stoul(value(timeoutEdit)),
                 seed = std::stoul(value(seedEdit));
        if (workers < 1 || workers > 64 || timeout < 100 || timeout >= INFINITE)
            throw std::runtime_error("Workers must be 1..64; timeout at least 100 ms");
        constexpr int types[] = {1, 2, 3, 5, 7, 8, 9};
        int selected = int(SendMessageW(control(typeBox), CB_GETCURSEL, 0, 0));
        if (selected < 0 || selected >= 7)
            throw std::runtime_error("Choose a sensor type");
        int type = types[selected];
        const wchar_t* profiles[] = {L"clean",    L"gaussian15", L"gaussian30",
                                     L"impulse2", L"blur1",      L"brightness"};
        int noiseIndex = int(SendMessageW(control(noiseBox), CB_GETCURSEL, 0, 0));
        if (noiseIndex < 0 || noiseIndex > 5)
            throw std::runtime_error("Choose noise profile");
        std::wstring noise = profiles[noiseIndex];
        auto policy = selectionRule(policyBox, policyDetail);
        auto genuine = selectionRule(genuineBox, genuineDetail);
        auto impostor = selectionRule(impostorBox, impostorDetail);
        runFolder = out / (L"run-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                           std::to_wstring(GetTickCount64()));
        logPath = runFolder;
        logPath += L".log";
        fs::path plan = runFolder;
        plan += L".plan.tsv";
        if (reusePlan)
            plan = fs::canonical(value(planEdit));
        wchar_t executable[32768];
        auto len = GetModuleFileNameW(nullptr, executable, 32768);
        if (!len || len >= 32768)
            throw std::runtime_error("Executable path unavailable");
        auto bench = fs::path(executable).parent_path() / L"fp_bench.exe";
        if (!fs::exists(bench))
            throw std::runtime_error("fp_bench.exe is missing");
        auto planner = fs::path(executable).parent_path() / L"fp_plan.exe";
        if (!reusePlan && !fs::exists(planner))
            throw std::runtime_error("fp_plan.exe is missing");
        active = true;
        setBusy(true);
        label(statusLabel,
              reusePlan ? L"저장된 시험 목록으로 실행합니다…" : L"시험 목록을 만들고 있습니다…");
        SendMessageW(control(progressBar), PBM_SETMARQUEE, TRUE, 40);
        SetTimer(window, 1, 750, nullptr);
        background = std::thread([=] {
            HANDLE log = INVALID_HANDLE_VALUE;
            try {
                SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
                log = CreateFileW(logPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_NEW,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
                if (log == INVALID_HANDLE_VALUE)
                    throw std::runtime_error("Cannot create run log");
                runExit = 0;
                if (!reusePlan)
                    runExit = launch(planner,
                                     {db.wstring(), std::to_wstring(type), plan.wstring(),
                                      std::to_wstring(seed), policy, genuine, impostor},
                                     log);
                if (runExit == 0)
                    runExit = launch(bench,
                                     {L"run", plan.wstring(), dll.wstring(), runFolder.wstring(),
                                      std::to_wstring(timeout), std::to_wstring(workers), noise,
                                      std::to_wstring(seed)},
                                     log);
                finalText = runExit == 0 ? L"시험 완료. 리포트에서 지표와 그래프를 확인하세요."
                            : runExit == 130
                                ? L"시험을 중단했습니다. 부분 기록을 보존했습니다."
                                : L"시험을 완료하지 못했습니다. 실행 로그를 확인하세요.";
            } catch (const std::exception& e) {
                auto s = std::string(e.what());
                finalText = L"실행 오류: " + fs::u8path(s).wstring();
                runExit = 2;
            }
            if (log != INVALID_HANDLE_VALUE)
                CloseHandle(log);
            PostMessageW(window, finishedMessage, 0, 0);
        });
    } catch (const std::exception& e) {
        active = false;
        setBusy(false);
        MessageBoxW(window, fs::u8path(e.what()).c_str(), L"입력 확인", MB_OK | MB_ICONWARNING);
    }
}
void startInspection() {
    if (active)
        return;
    try {
        if (!fs::exists(runFolder / "pairs.tsv"))
            throw std::runtime_error("Complete an evaluation first");
        wchar_t executable[32768];
        auto length = GetModuleFileNameW(nullptr, executable, 32768);
        if (!length || length >= 32768)
            throw std::runtime_error("Executable path unavailable");
        auto inspector = fs::path(executable).parent_path() / L"fp_inspect.exe";
        if (!fs::exists(inspector))
            throw std::runtime_error("fp_inspect.exe is missing");
        analysisOutput = runFolder / (L"inspection-" + std::to_wstring(GetTickCount64()));
        logPath = analysisOutput;
        logPath += L".log";
        analysisOperation = true;
        cancelRequested = false;
        active = true;
        setBusy(true);
        label(statusLabel, L"특징점·정합 분석을 만들고 있습니다…");
        SendMessageW(control(progressBar), PBM_SETMARQUEE, TRUE, 40);
        SetTimer(window, 1, 750, nullptr);
        background = std::thread([=] {
            HANDLE log = INVALID_HANDLE_VALUE;
            try {
                SECURITY_ATTRIBUTES attributes{sizeof(attributes), nullptr, TRUE};
                log = CreateFileW(logPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &attributes,
                                  CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (log == INVALID_HANDLE_VALUE)
                    throw std::runtime_error("Cannot create inspection log");
                runExit =
                    launch(inspector, {runFolder.wstring(), analysisOutput.wstring(), L"12"}, log);
                finalText = runExit == 0     ? L"정합 분석 완료. 분석 화면을 엽니다."
                            : runExit == 130 ? L"정합 분석을 중단했습니다."
                                             : L"분석을 생성하지 못했습니다. 실행 로그를 "
                                               L"확인하세요. 특징점 v2 결과가 필요합니다.";
            } catch (const std::exception& e) {
                runExit = 2;
                finalText = L"분석 실행 오류: " + fs::u8path(e.what()).wstring();
            }
            if (log != INVALID_HANDLE_VALUE)
                CloseHandle(log);
            PostMessageW(window, finishedMessage, 0, 0);
        });
    } catch (const std::exception& e) {
        active = false;
        setBusy(false);
        MessageBoxW(window, fs::u8path(e.what()).c_str(), L"분석 확인", MB_OK | MB_ICONWARNING);
    }
}
void cancel() {
    // Can be requested while the plan is still being created; the timer retries.
    auto folder = analysisOperation ? analysisOutput : runFolder;
    if (fs::is_directory(folder)) {
        std::ofstream request(folder / "cancel.request");
        request << "stop";
    }
    label(statusLabel, L"중단 요청 중… 실행 중인 작업을 종료하고 기록을 보존합니다.");
}
LRESULT CALLBACK procedure(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_CREATE) {
        window = hwnd;
        font = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                           DEFAULT_PITCH, L"Segoe UI");
        create(L"STATIC", L"지문인식 성능 평가", 0, 28, 20, 760, 32);
        create(L"STATIC", L"DLL과 지문 DB를 선택하고, 재현 가능한 시험과 리포트를 생성합니다.", 0,
               28, 58, 800, 25);
        const wchar_t* names[] = {L"알고리즘 DLL", L"지문 DB 폴더", L"결과 저장 폴더"};
        int ids[] = {dllEdit, dbEdit, outEdit};
        for (int i = 0; i < 3; ++i) {
            int y = 105 + i * 66;
            create(L"STATIC", names[i], 0, 28, y, 150, 22);
            create(L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, 180, y - 4, 540, 30, ids[i]);
            create(L"BUTTON", L"찾아보기", WS_TABSTOP, 735, y - 4, 105, 30, 201 + i);
        }
        create(L"STATIC", L"DB 타입", 0, 28, 310, 110, 24);
        auto types =
            create(L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST, 180, 306, 250, 180, typeBox);
        for (auto s :
             {L"1 · FVC2000 DB3", L"2 · FVC2002 DB3", L"3 · FVC2004 DB1", L"5 · DIG00 (280×320)",
              L"7 · PER00 (292×525)", L"8 · NIT00 (248×292)", L"9 · HUN00 (288×320)"})
            SendMessageW(types, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(s));
        SendMessageW(types, CB_SETCURSEL, 0, 0);
        create(L"STATIC", L"비교 이미지 조건", 0, 455, 310, 130, 24);
        auto noises =
            create(L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST, 595, 306, 245, 220, noiseBox);
        for (auto s : {L"원본", L"가우시안 잡음 σ=15", L"가우시안 잡음 σ=30", L"점 잡음 2%",
                       L"가우시안 흐림 3×3", L"밝기·대비 변화"})
            SendMessageW(noises, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(s));
        SendMessageW(noises, CB_SETCURSEL, 0, 0);
        create(L"STATIC", L"등록 규칙", 0, 28, 362, 130, 24);
        auto policy =
            create(L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST, 180, 358, 170, 160, policyBox);
        for (auto s : {L"첫 이미지", L"모든 이미지", L"무작위 N장", L"지정 순번 (1,3)"})
            SendMessageW(policy, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(s));
        SendMessageW(policy, CB_SETCURSEL, 0, 0);
        create(L"EDIT", L"1", ES_AUTOHSCROLL | WS_TABSTOP, 360, 358, 70, 30, policyDetail);
        create(L"STATIC", L"동시 작업 수", 0, 455, 362, 130, 24);
        create(L"EDIT", L"4", ES_AUTOHSCROLL | ES_NUMBER | WS_TABSTOP, 595, 358, 80, 30,
               workerEdit);
        create(L"STATIC", L"진짜 지문 선택", 0, 28, 414, 140, 24);
        create(L"STATIC", L"가짜 지문 선택", 0, 455, 414, 140, 24);
        for (auto pair :
             {std::pair<int, int>{genuineBox, genuineDetail}, {impostorBox, impostorDetail}}) {
            bool left = pair.first == genuineBox;
            auto box = create(L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST, left ? 180 : 595,
                              410, left ? 170 : 160, 160, pair.first);
            for (auto name : {L"첫 이미지", L"모든 이미지", L"무작위 N장", L"지정 순번 (1,3)"})
                SendMessageW(box, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name));
            SendMessageW(box, CB_SETCURSEL, 1, 0);
            create(L"EDIT", L"1", ES_AUTOHSCROLL | WS_TABSTOP, left ? 360 : 765, 410,
                   left ? 70 : 75, 30, pair.second);
        }
        ruleDetails(false);
        create(L"STATIC", L"제한 시간 (ms)", 0, 28, 466, 140, 24);
        create(L"EDIT", L"5000", ES_AUTOHSCROLL | ES_NUMBER | WS_TABSTOP, 180, 462, 140, 30,
               timeoutEdit);
        create(L"STATIC", L"무작위 시드", 0, 455, 466, 130, 24);
        create(L"EDIT", L"20261103", ES_AUTOHSCROLL | ES_NUMBER | WS_TABSTOP, 595, 462, 245, 30,
               seedEdit);
        create(L"STATIC", L"기존 시험 목록", 0, 28, 514, 150, 22);
        create(L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, 180, 510, 540, 30, planEdit);
        create(L"BUTTON", L"불러오기", WS_TABSTOP, 735, 510, 105, 30, 204);
        create(L"STATIC",
               L"기존 목록을 선택하면 DB 폴더·등록 규칙 대신 저장된 비교 목록을 사용합니다.", 0, 28,
               538, 812, 18);
        create(L"BUTTON", L"시험 시작", WS_TABSTOP | BS_DEFPUSHBUTTON, 28, 565, 150, 38,
               startButton);
        create(L"BUTTON", L"중단", WS_TABSTOP, 194, 565, 110, 38, cancelButton);
        create(L"BUTTON", L"리포트 열기", WS_TABSTOP, 610, 565, 130, 38, reportButton);
        create(L"BUTTON", L"특징점·정합 분석", WS_TABSTOP, 325, 565, 230, 38, inspectButton);
        create(L"BUTTON", L"실행 로그", WS_TABSTOP, 750, 565, 90, 38, 123);
        create(PROGRESS_CLASSW, L"", PBS_MARQUEE, 28, 623, 812, 18, progressBar);
        create(L"STATIC", L"준비됨 · 초기 개발 버전", 0, 28, 656, 812, 44, statusLabel);
        wchar_t executable[32768];
        GetModuleFileNameW(nullptr, executable, 32768);
        label(dllEdit,
              (fs::path(executable).parent_path() / L"fingerprint_minutiae.dll").wstring());
        label(outEdit, fs::path(executable).parent_path().wstring());
        setBusy(false);
        return 0;
    }
    if (message == WM_COMMAND) {
        int id = LOWORD(wParam);
        if ((id == policyBox || id == genuineBox || id == impostorBox) &&
            HIWORD(wParam) == CBN_SELCHANGE)
            ruleDetails(active);
        if (id >= 201 && id <= 203)
            browse(id - 100, id != 201);
        else if (id == 204)
            browse(planEdit, false);
        else if (id == startButton) {
            cancelRequested = false;
            start();
        } else if (id == cancelButton) {
            cancelRequested = true;
            cancel();
        } else if (id == inspectButton)
            startInspection();
        else if (id == reportButton)
            ShellExecuteW(hwnd, L"open", (runFolder / L"report.html").c_str(), nullptr, nullptr,
                          SW_SHOWNORMAL);
        else if (id == 123 && fs::exists(logPath))
            ShellExecuteW(hwnd, L"open", logPath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return 0;
    }
    if (message == WM_TIMER && active) {
        if (cancelRequested) {
            cancel();
            return 0;
        }
        if (analysisOperation) {
            label(statusLabel, L"특징점·정합 분석 생성 중… 원본과 기록된 점수를 확인합니다.");
            return 0;
        }
        std::ifstream records(runFolder / "records.jsonl");
        std::string line;
        size_t completed = 0, enroll = 0, attempt = 0;
        while (std::getline(records, line)) {
            if (line.find("\"kind\":\"pair\"") != std::string::npos)
                ++completed;
            else if (line.find("\"kind\":\"enroll\"") != std::string::npos)
                ++enroll;
            else if (line.find("\"kind\":\"attempt\"") != std::string::npos)
                ++attempt;
        }
        label(statusLabel, L"시험 진행 중 · 등록 " + std::to_wstring(enroll) + L" / 특징 추출 " +
                               std::to_wstring(attempt) + L" / 비교 " + std::to_wstring(completed));
        return 0;
    }
    if (message == finishedMessage) {
        if (background.joinable())
            background.join();
        active = false;
        KillTimer(hwnd, 1);
        SendMessageW(control(progressBar), PBM_SETMARQUEE, FALSE, 0);
        setBusy(false);
        label(statusLabel, finalText);
        if (analysisOperation && runExit == 0 && !closing && !cancelRequested) {
            auto opened = ShellExecuteW(hwnd, L"open", (analysisOutput / L"index.html").c_str(),
                                        nullptr, nullptr, SW_SHOWNORMAL);
            if (reinterpret_cast<INT_PTR>(opened) <= 32)
                label(
                    statusLabel,
                    L"분석은 생성됐습니다. 결과 폴더의 inspection-* 폴더에서 index.html을 여세요.");
        }
        if (closing)
            DestroyWindow(hwnd);
        return 0;
    }
    if (message == WM_CLOSE) {
        if (active) {
            closing = true;
            cancelRequested = true;
            cancel();
            return 0;
        }
        DestroyWindow(hwnd);
        return 0;
    }
    if (message == WM_DESTROY) {
        if (font)
            DeleteObject(font);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}
} // namespace
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    SetProcessDPIAware();
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr))
        return 2;
    INITCOMMONCONTROLSEX common{sizeof(common), ICC_PROGRESS_CLASS};
    InitCommonControlsEx(&common);
    WNDCLASSW cls{};
    cls.lpfnWndProc = procedure;
    cls.hInstance = instance;
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    cls.lpszClassName = L"FingerprintBenchWindow";
    if (!RegisterClassW(&cls)) {
        CoUninitialize();
        return 2;
    }
    RECT bounds{0, 0, 870, 720};
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    AdjustWindowRect(&bounds, style, FALSE);
    auto hwnd = CreateWindowW(cls.lpszClassName, L"지문인식 평가 · Fingerprint Bench", style,
                              CW_USEDEFAULT, CW_USEDEFAULT, bounds.right - bounds.left,
                              bounds.bottom - bounds.top, nullptr, nullptr, instance, nullptr);
    if (!hwnd) {
        CoUninitialize();
        return 2;
    }
    ShowWindow(hwnd, show);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(hwnd, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    CoUninitialize();
    return int(message.wParam);
}
