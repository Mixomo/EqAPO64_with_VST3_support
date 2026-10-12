#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QElapsedTimer>
#include <iostream>
#include <windows.h>
#include <sddl.h>
#include "outproc/OutProcControlEndpoint.h"
#include "Editor/helpers/ConfigurationWriter.h"

int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);QTemporaryDir directory;
    if(!directory.isValid())return 2;
    const auto path=directory.filePath("config.txt");QString error;
    const QByteArray original="Preamp: -3 dB\r\n",replacement="Preamp: -6 dB\r\n";
    if(!writeEditorConfiguration(path,original,&error))return 3;
    HANDLE lock=CreateFileW(path.toStdWString().c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,0,nullptr);
    if(lock==INVALID_HANDLE_VALUE)return 4;
    // Confirm the old MainWindow CREATE_ALWAYS operation fails with exactly
    // the sharing violation that drove its unbounded GUI-thread retry loop.
    HANDLE old=CreateFileW(path.toStdWString().c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,0,nullptr);
    const DWORD oldError=GetLastError();if(old!=INVALID_HANDLE_VALUE){CloseHandle(old);CloseHandle(lock);return 5;}
    QElapsedTimer timer;timer.start();const bool lockedSave=writeEditorConfiguration(path,replacement,&error);
    const auto duration=timer.elapsed();CloseHandle(lock);
    QFile verify(path);if(!verify.open(QIODevice::ReadOnly))return 6;
    const auto preserved=verify.readAll();verify.close();
    if(oldError!=ERROR_SHARING_VIOLATION || lockedSave || error.isEmpty() || duration>1000 || preserved!=original)return 7;
    HANDLE notification=FindFirstChangeNotificationW(directory.path().toStdWString().c_str(),TRUE,
        FILE_NOTIFY_CHANGE_FILE_NAME|FILE_NOTIFY_CHANGE_LAST_WRITE);
    if(notification==INVALID_HANDLE_VALUE)return 11;
    if(!writeEditorConfiguration(path,replacement,&error)){FindCloseChangeNotification(notification);return 8;}
    const DWORD changed=WaitForSingleObject(notification,1000); // Test controller only.
    FindCloseChangeNotification(notification);
    if(changed!=WAIT_OBJECT_0)return 12;
    if(!verify.open(QIODevice::ReadOnly) || verify.readAll()!=replacement)return 9;verify.close();
    HANDLE repeated=FindFirstChangeNotificationW(directory.path().toStdWString().c_str(),FALSE,
        FILE_NOTIFY_CHANGE_FILE_NAME|FILE_NOTIFY_CHANGE_LAST_WRITE);
    if(repeated==INVALID_HANDLE_VALUE)return 15;
    const bool repeatedSave=writeEditorConfiguration(path,replacement,&error);
    const DWORD repeatedEvent=WaitForSingleObject(repeated,100);
    FindCloseChangeNotification(repeated);
    if(!repeatedSave || repeatedEvent!=WAIT_TIMEOUT)return 16;
    // A failed destination also preserves the last valid configuration.
    if(writeEditorConfiguration(directory.filePath("missing/config.txt"),replacement,&error))return 10;
    // A real ACL fixture: content writes allowed, directory/file deletion
    // denied. Atomic replacement fails, while the bounded fallback preserves
    // both content-write permission and the existing security descriptor.
    const auto sid=EqApoControl::currentUserSid();
    auto secure=[&](const QString& name,const std::wstring& sddl){
        PSECURITY_DESCRIPTOR sd=nullptr;
        if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(),SDDL_REVISION_1,&sd,nullptr))return false;
        const bool ok=SetFileSecurityW(name.toStdWString().c_str(),DACL_SECURITY_INFORMATION|PROTECTED_DACL_SECURITY_INFORMATION,sd)!=FALSE;
        LocalFree(sd);return ok;
    };
    const auto full=L"D:P(A;;FA;;;"+sid+L")";
    if(!secure(directory.path(),L"D:P(D;;DC;;;"+sid+L")(A;;FA;;;"+sid+L")")
        || !secure(path,L"D:P(D;;SD;;;"+sid+L")(A;;FA;;;"+sid+L")"))return 13;
    const bool aclSaved=writeEditorConfiguration(path,original,&error);
    const bool restored=secure(path,full) && secure(directory.path(),full);
    if(!aclSaved || !restored || !verify.open(QIODevice::ReadOnly) || verify.readAll()!=original)return 14;
    verify.close();
    std::cout<<"PASS old_error="<<oldError<<" locked_save_ms="<<duration<<" preserved=true subsequent_save=true watcher_signaled=true\n";
    return 0;
}
