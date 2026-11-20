#include "platform/Process.h"

#include "platform/Shell.h"
#include "platform/Win.h"

#include <thread>
#include <vector>

namespace rm::platform {

std::map<std::wstring, std::wstring> CurrentEnvironment() {
    std::map<std::wstring, std::wstring> out;
    wchar_t* block = GetEnvironmentStringsW();
    if (!block) return out;
    for (const wchar_t* p = block; *p; p += wcslen(p) + 1) {
        const std::wstring entry = p;
        const size_t eq = entry.find(L'=', 1);  // ("=C:=C:\..." は先頭の '=' を残す)
        if (eq == std::wstring::npos) continue;
        out[entry.substr(0, eq)] = entry.substr(eq + 1);
    }
    FreeEnvironmentStringsW(block);
    return out;
}

std::string ConsoleTextToUtf8(const std::string& bytes) {
    if (bytes.empty()) return {};
    // もう UTF-8 (頼めばツールは UTF-8 で出す): そのまま使う。
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0) > 0) return bytes;
    const UINT cp = GetOEMCP();
    const int n = MultiByteToWideChar(cp, 0, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(cp, 0, bytes.data(), static_cast<int>(bytes.size()), w.data(), n);
    const int m = WideCharToMultiByte(CP_UTF8, 0, w.data(), n, nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(m), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), n, out.data(), m, nullptr, nullptr);
    return out;
}

ProcessResult RunProcess(const std::wstring& commandLine, const std::filesystem::path& workingDir,
                         const std::map<std::wstring, std::wstring>& environment, std::string* error) {
    ProcessResult r;
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE readPipe = nullptr, writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &sa, 0)) {
        if (error) *error = ErrorText(GetLastError());
        return r;
    }
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);
    // パイプだけを渡す (PROC_THREAD_ATTRIBUTE_HANDLE_LIST): 子に、このプロセスの継承できるハンドルを
    // 全部渡してはいけない - 別の実行のパイプをこの子が持っていると、その実行は、この子が終わるまで
    // 終端を待ち続けてしまう (書き出しとスクリプトのビルドが同時のとき)。
    SIZE_T attrSize = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attrSize);
    std::vector<unsigned char> attrBuf(attrSize);
    auto* attrs = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attrBuf.data());
    HANDLE inherit[] = {writePipe};
    const bool listed = InitializeProcThreadAttributeList(attrs, 1, 0, &attrSize) &&
                        UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherit, sizeof(inherit), nullptr, nullptr);
    if (!listed) {
        if (error) *error = ErrorText(GetLastError());
        CloseHandle(readPipe);
        CloseHandle(writePipe);
        return r;
    }
    STARTUPINFOEXW six{};
    six.StartupInfo.cb = sizeof(six);
    STARTUPINFOW& si = six.StartupInfo;
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = writePipe;
    si.hStdError = writePipe;
    si.hStdInput = nullptr;  // (リストに入れない: 読むものが無い)
    six.lpAttributeList = attrs;
    std::vector<wchar_t> env;
    if (!environment.empty()) {
        for (const auto& [k, v] : environment) {
            const std::wstring e = k + L"=" + v;
            env.insert(env.end(), e.begin(), e.end());
            env.push_back(L'\0');
        }
        env.push_back(L'\0');
    }
    // それと、それが起こすもの全部のためのジョブ: それが終わると閉じる (JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE)。
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
            CloseHandle(job);
            job = nullptr;
        }
    }
    std::wstring cmd = commandLine;
    PROCESS_INFORMATION pi{};
    const BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE,
                                   CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT | EXTENDED_STARTUPINFO_PRESENT | CREATE_SUSPENDED,
                                   env.empty() ? nullptr : env.data(), workingDir.empty() ? nullptr : workingDir.c_str(), &si, &pi);
    DeleteProcThreadAttributeList(attrs);
    CloseHandle(writePipe);
    if (!ok) {
        if (error) *error = ErrorText(GetLastError());
        CloseHandle(readPipe);
        if (job) CloseHandle(job);
        return r;
    }
    // 動き出す前にジョブに入れる (起こすものもみなその中に入る)。ジョブに入れられなかったときは、
    // ジョブ無しで動かす。
    if (job && !AssignProcessToJobObject(job, pi.hProcess)) {
        CloseHandle(job);
        job = nullptr;
    }
    ResumeThread(pi.hThread);
    r.started = true;
    // 出力は専用のスレッドで読む。パイプを持ったまま残っているプロセス (バックグラウンドの子) が
    // これを待たせ続けないように: パイプは、下でジョブを閉じたときに閉じる。
    std::thread reader([&r, readPipe] {
        char buf[4096];
        DWORD got = 0;
        while (ReadFile(readPipe, buf, sizeof(buf), &got, nullptr) && got > 0) r.output.append(buf, got);
    });
    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &r.exitCode);
    if (job) {
        TerminateJobObject(job, 1);  // 残っているものは今終わる
        CloseHandle(job);
    } else {
        CancelIoEx(readPipe, nullptr);  // (ジョブが無い: 残ったものがパイプを持ち続ける - とにかく読むのをやめる)
    }
    reader.join();
    CloseHandle(readPipe);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return r;
}

}  // namespace rm::platform
