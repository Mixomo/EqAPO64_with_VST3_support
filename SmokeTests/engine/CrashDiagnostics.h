#pragma once
#include <windows.h>
#include <dbghelp.h>
#include <iostream>
inline LONG WINAPI smokeCrashDiagnostic(EXCEPTION_POINTERS* exception) {
    HANDLE process=GetCurrentProcess();SymInitialize(process,nullptr,TRUE);
    CONTEXT context=*exception->ContextRecord;STACKFRAME64 frame{};
    frame.AddrPC={context.Rip,0,AddrModeFlat};frame.AddrStack={context.Rsp,0,AddrModeFlat};frame.AddrFrame={context.Rbp,0,AddrModeFlat};
    std::cerr<<"NATIVE_EXCEPTION "<<std::hex<<exception->ExceptionRecord->ExceptionCode<<'\n';
    for(unsigned i=0;i<20;++i) {
        char buffer[sizeof(SYMBOL_INFO)+1024]{};auto* symbol=reinterpret_cast<SYMBOL_INFO*>(buffer);
        symbol->SizeOfStruct=sizeof(SYMBOL_INFO);symbol->MaxNameLen=1023;DWORD64 displacement=0;
        MEMORY_BASIC_INFORMATION memory{};VirtualQuery(reinterpret_cast<void*>(frame.AddrPC.Offset),&memory,sizeof(memory));
        char module[MAX_PATH]{};GetModuleFileNameA(static_cast<HMODULE>(memory.AllocationBase),module,MAX_PATH);
        SymFromAddr(process,frame.AddrPC.Offset,&displacement,symbol);
        std::cerr<<module<<" + "<<frame.AddrPC.Offset-reinterpret_cast<DWORD64>(memory.AllocationBase)<<" "<<symbol->Name<<" + "<<displacement<<'\n';
        IMAGEHLP_LINE64 source{};source.SizeOfStruct=sizeof(source);DWORD lineOffset=0;
        if(SymGetLineFromAddr64(process,frame.AddrPC.Offset,&lineOffset,&source))std::cerr<<source.FileName<<":"<<std::dec<<source.LineNumber<<std::hex<<'\n';
        if(!StackWalk64(IMAGE_FILE_MACHINE_AMD64,process,GetCurrentThread(),&frame,&context,nullptr,SymFunctionTableAccess64,SymGetModuleBase64,nullptr))break;
    }
    return EXCEPTION_EXECUTE_HANDLER;
}
