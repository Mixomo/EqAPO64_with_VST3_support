#pragma once
#include <QSaveFile>
#include <QString>
#include <QByteArray>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

// Editor/control thread only. Existing Windows files use one exclusive handle,
// preserving their ACL and avoiding premature directory notifications. New
// files use atomic creation. Failed existing-file writes restore old bytes.
#ifdef Q_OS_WIN
inline bool writeExistingEditorConfiguration(const QString& path,const QByteArray& bytes)
{
    HANDLE handle=CreateFileW(path.toStdWString().c_str(),GENERIC_READ|GENERIC_WRITE,
        0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(handle==INVALID_HANDLE_VALUE)return false;
    LARGE_INTEGER size{};
    if(!GetFileSizeEx(handle,&size) || size.QuadPart<0 || size.QuadPart>64*1024*1024) {
        CloseHandle(handle);return false;
    }
    QByteArray previous(int(size.QuadPart),Qt::Uninitialized);DWORD count=0;
    if(!ReadFile(handle,previous.data(),DWORD(previous.size()),&count,nullptr)
        || count!=DWORD(previous.size())) {CloseHandle(handle);return false;}
    if(previous==bytes) {CloseHandle(handle);return true;}
    auto replace=[&](const QByteArray& content) {
        LARGE_INTEGER beginning{};DWORD written=0;
        return SetFilePointerEx(handle,beginning,nullptr,FILE_BEGIN)
            && WriteFile(handle,content.constData(),DWORD(content.size()),&written,nullptr)
            && written==DWORD(content.size()) && SetEndOfFile(handle) && FlushFileBuffers(handle);
    };
    const bool success=replace(bytes);
    if(!success)replace(previous);
    CloseHandle(handle);return success;
}
#endif
inline bool writeEditorConfiguration(const QString& path,const QByteArray& bytes,QString* error)
{
#ifdef Q_OS_WIN
    // Existing APO files are watched while the DSP is running. QSaveFile's
    // temporary-file creation/rename can wake that watcher before publication,
    // rebuilding the unchanged graph (and reloading a slow vendor DLL).
    // One exclusive existing-file handle keeps readers out until the whole
    // configuration is written; the existing rollback preserves failed saves
    // and the file's ACL. New files still use atomic QSaveFile creation.
    if(GetFileAttributesW(path.toStdWString().c_str())!=INVALID_FILE_ATTRIBUTES) {
        if(writeExistingEditorConfiguration(path,bytes))return true;
        if(error)*error=QStringLiteral("Could not obtain exclusive write access to the configuration file");
        return false;
    }
#endif
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if(!file.open(QIODevice::WriteOnly)) {
#ifdef Q_OS_WIN
        if(writeExistingEditorConfiguration(path,bytes))return true;
#endif
        if(error)*error=file.errorString();return false;
    }
    if(file.write(bytes)!=bytes.size()) {
        if(error)*error=file.errorString();file.cancelWriting();return false;
    }
    if(!file.commit()) {
#ifdef Q_OS_WIN
        if(writeExistingEditorConfiguration(path,bytes))return true;
#endif
        if(error)*error=file.errorString();return false;
    }
    return true;
}
