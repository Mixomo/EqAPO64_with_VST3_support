#pragma once
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include "outproc/OutProcControlEndpoint.h"

inline bool isUserBrokerConfigured() {
    return true; // Built-in engine policy, independent of legacy installer flags.
}
inline bool isUserBrokerRunning() {
    const auto sid=EqApoControl::currentUserSid();if(sid.empty())return false;
    HANDLE guard=OpenMutexW(SYNCHRONIZE|MUTEX_MODIFY_STATE,FALSE,(L"Local\\EqApoUserBroker_"+sid).c_str());
    if(!guard)return false;
    const auto state=WaitForSingleObject(guard,0);
    if(state==WAIT_OBJECT_0 || state==WAIT_ABANDONED)ReleaseMutex(guard);
    CloseHandle(guard);return state==WAIT_TIMEOUT;
}
struct UserBrokerStartupResult {bool configured=false,ordinaryUser=false,alreadyRunning=false,started=false;qint64 pid=0;QString error;};
inline UserBrokerStartupResult ensureSelectedUserBroker() {
    UserBrokerStartupResult result;result.configured=isUserBrokerConfigured();if(!result.configured)return result;
    DWORD session=0;HANDLE token=nullptr;TOKEN_ELEVATION elevation{};DWORD bytes=0;
    if(!ProcessIdToSessionId(GetCurrentProcessId(),&session) || !session || !OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token))return result;
    result.ordinaryUser=GetTokenInformation(token,TokenElevation,&elevation,sizeof(elevation),&bytes) && !elevation.TokenIsElevated;
    CloseHandle(token);if(!result.ordinaryUser)return result;
    result.alreadyRunning=isUserBrokerRunning();if(result.alreadyRunning)return result;
    const auto directory=QCoreApplication::applicationDirPath();const auto executable=QDir(directory).filePath("EqApoOutProcHost.exe");
    if(!QFileInfo::exists(executable)){result.error="EqApoOutProcHost.exe missing";return result;}
    QProcess process;process.setProgram(executable);process.setArguments({"--user-broker"});process.setWorkingDirectory(directory);
    process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* arguments){arguments->flags|=CREATE_NO_WINDOW;});
    result.started=process.startDetached(&result.pid);if(!result.started)result.error=process.errorString();return result;
}
