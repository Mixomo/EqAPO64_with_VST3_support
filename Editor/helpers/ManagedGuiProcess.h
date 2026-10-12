#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <thread>
#include <fstream>
#include "outproc/OutProcGuiObjects.h"

// GUI-control ownership only. The canonical process handle is obtained at
// creation, never reconstructed from a PID file or a shared mapping. A job
// contains this GUI process only; private APO DSP hosts are independent.
class ManagedGuiProcess : public std::enable_shared_from_this<ManagedGuiProcess> {
public:
    struct Shutdown {bool requested=false,completed=false,forced=false;DWORD exitCode=STILL_ACTIVE;};
    static std::shared_ptr<ManagedGuiProcess> launch(const std::wstring& executable,
        const std::vector<std::wstring>& arguments,const std::wstring& workingDirectory,
        const std::wstring& session,const std::wstring& configPath,const std::wstring& pidPath) {
        auto result=std::shared_ptr<ManagedGuiProcess>(new ManagedGuiProcess(session,configPath,pidPath));
        result->job=CreateJobObjectW(nullptr,nullptr);if(!result->job)return {};
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if(!SetInformationJobObject(result->job,JobObjectExtendedLimitInformation,&limits,sizeof(limits)))return {};
        std::wstring line=quote(executable);for(const auto& argument:arguments)line+=L" "+quote(argument);
        std::vector<wchar_t> storage(line.begin(),line.end());storage.push_back(0);
        STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
        if(!CreateProcessW(executable.c_str(),storage.data(),nullptr,nullptr,FALSE,
            CREATE_SUSPENDED|CREATE_NO_WINDOW,nullptr,workingDirectory.c_str(),&startup,&process))return {};
        result->process=process.hProcess;result->pid=process.dwProcessId;
        if(!AssignProcessToJobObject(result->job,result->process)) {
            const auto error=GetLastError();TerminateProcess(result->process,23);CloseHandle(process.hThread);SetLastError(error);return {};
        }
        // Delegate while the new GUI process is still suspended so its first
        // panel can activate ahead of the foreground editor.
        AllowSetForegroundWindow(result->pid);
        if(ResumeThread(process.hThread)==DWORD(-1)) {
            const auto error=GetLastError();TerminateProcess(result->process,23);CloseHandle(process.hThread);SetLastError(error);return {};
        }
        CloseHandle(process.hThread);return result;
    }
    ~ManagedGuiProcess(){if(job)CloseHandle(job);if(process)CloseHandle(process);}
    DWORD processId() const noexcept {return pid;}
    bool alive() const noexcept {return process && WaitForSingleObject(process,0)==WAIT_TIMEOUT;}
    Shutdown shutdown() const noexcept {return {closing.load(),completed.load(),forced.load(),exitCode.load()};}
    void requestClose() noexcept {
        if(closing.exchange(true,std::memory_order_acq_rel))return;
        try {
        auto self=shared_from_this();
        std::thread([self]() {
            try {
            const auto deadline=GetTickCount64()+2000;
            while(self->alive() && GetTickCount64()<deadline) {
                HANDLE event=OpenEventW(EVENT_MODIFY_STATE,FALSE,OutProcGuiObjectName(self->session,L"GuiExit").c_str());
                if(event){SetEvent(event);CloseHandle(event);}
                WaitForSingleObject(self->process,50); // Watchdog only, never Qt/APO.
            }
            if(self->alive()) {
                self->forced.store(true,std::memory_order_release);
                TerminateProcess(self->process,23);
                WaitForSingleObject(self->process,1000); // Only this exact owned handle.
                if(self->job){CloseHandle(self->job);self->job=nullptr;}
            }
            DWORD code=STILL_ACTIVE;GetExitCodeProcess(self->process,&code);
            self->exitCode.store(code,std::memory_order_release);
            const bool ended=WaitForSingleObject(self->process,0)==WAIT_OBJECT_0;
            {
                std::ofstream report(self->configPath+L".shutdown.json");
                report<<"{\"process_id\":"<<self->pid<<",\"forced\":"<<(self->forced.load()?"true":"false")
                    <<",\"completed\":"<<(ended?"true":"false")<<",\"exit_code\":"<<code<<"}\n";
            }
            if(ended) {
                DeleteFileW(self->configPath.c_str());
                // A later process may have recreated the same session file.
                std::wifstream pidFile(self->pidPath);DWORD stored=0;pidFile>>stored;pidFile.close();
                if(stored==self->pid)DeleteFileW(self->pidPath.c_str());
            }
            self->completed.store(ended,std::memory_order_release);
            } catch(...) {
                self->emergencyClose();
            }
        }).detach();
        } catch(...) {
            // Thread creation can fail during shutdown. Never throw from a
            // row destructor or wait on Qt; terminate only our owned handle.
            emergencyClose();
        }
    }
private:
    void emergencyClose() noexcept {
        forced.store(true,std::memory_order_release);
        if(process && alive())TerminateProcess(process,23);
        DWORD code=STILL_ACTIVE;
        if(process)GetExitCodeProcess(process,&code);
        exitCode.store(code,std::memory_order_release);
        completed.store(process && WaitForSingleObject(process,0)==WAIT_OBJECT_0,std::memory_order_release);
    }
    ManagedGuiProcess(std::wstring session,std::wstring configPath,std::wstring pidPath)
        :session(std::move(session)),configPath(std::move(configPath)),pidPath(std::move(pidPath)){}
    static std::wstring quote(const std::wstring& value) {
        std::wstring result=L"\"";unsigned slashes=0;
        for(auto c:value) {
            if(c==L'\\'){++slashes;continue;}
            result.append(c==L'"'?2*slashes+1:slashes,L'\\');slashes=0;result+=c;
        }
        result.append(2*slashes,L'\\');return result+L'"';
    }
    HANDLE process=nullptr,job=nullptr;DWORD pid=0;
    std::wstring session,configPath,pidPath;
    std::atomic<bool> closing{false},completed{false},forced{false};
    std::atomic<DWORD> exitCode{STILL_ACTIVE};
};
