//
// Created by luedi on 2025-08-14.
//

#pragma once
#define NOMINMAX

#include <windows.h>
#include <tchar.h>

#ifndef DETOURS_STATIC
#define DETOURS_STATIC
#endif
#include <detours/detours.h>
#include <iostream>
#include <stdio.h>
#include <mutex>
#include "spdlog/spdlog.h"
#include "spdlog/sinks/basic_file_sink.h"
#include <spdlog/sinks/stdout_color_sinks.h>


#define DEBUGAPI extern __declspec(dllexport)
static DWORD g_totalBytesRead = 0;
extern LPVOID g_buffer;
extern DWORD g_offset;
extern DWORD g_length;
std::mutex mx;
LPVOID g_buffer = nullptr;
DWORD g_offset = 0;
DWORD g_length = 0;

DWORD    GetFilePointer   (HANDLE hFile) {
    if (hFile == NULL || hFile == INVALID_HANDLE_VALUE) {
        SPDLOG_ERROR("Invalid file handle in GetFilePointer");
        return 0;
    }

    return SetFilePointer(hFile, 0, NULL, FILE_CURRENT);

}

DEBUGAPI void dump_file(DWORD offset,DWORD len){
    char currentDir[MAX_PATH];
    GetCurrentDirectoryA(MAX_PATH, currentDir);
    SPDLOG_DEBUG("当前工作目录: {}\n", currentDir);

    HANDLE Hfile = CreateFileA((const char*)"vdo.bin",GENERIC_WRITE,0,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    if(Hfile == INVALID_HANDLE_VALUE){
        DWORD error = GetLastError();
        // 根据错误码判断具体原因
        switch (error) {
            case ERROR_FILE_NOT_FOUND:
                SPDLOG_ERROR("NO FILE WRITE");
                break;
            case ERROR_ACCESS_DENIED:
                SPDLOG_ERROR("DENY FILE WRITE");
                break;
            case ERROR_SHARING_VIOLATION:
                SPDLOG_ERROR("CANNOT FILE WRITE");
                break;
            default:
                SPDLOG_ERROR("FILE WRITE?");
                break;
        }
        return;
    }
    SetFilePointer(Hfile,g_offset,NULL,FILE_BEGIN);

    DWORD dwWritten;
    WriteFile(Hfile,g_buffer,g_length,&dwWritten,NULL);
    SPDLOG_INFO("[DEUMP]WriteFile offset:{} || {} -> {}",g_offset,g_length,dwWritten);
    CloseHandle(Hfile);
    return;
}

void CreateConsoleWindow() {

    // 分配一个新的控制台
    AllocConsole();


    // 重定向标准输出流到控制台
    FILE* pCout;
    freopen_s(&pCout, "CONOUT$", "w", stdout);
    FILE* pCerr;
    freopen_s(&pCerr, "CONOUT$", "w", stderr);
    FILE* pCin;
    freopen_s(&pCin, "CONIN$", "r", stdin);

    // 使 iostream 与控制台同步
    std::ios::sync_with_stdio(true);
    std::wcout.clear();
    std::cout.clear();
    std::wcerr.clear();
    std::cerr.clear();
    std::wcin.clear();
    std::cin.clear();

    SetConsoleTitle("MastDebuger");
}

typedef BOOL (WINAPI *PFN_ReadFile)(
        HANDLE hFile,
        LPVOID lpBuffer,
        DWORD nNumberOfBytesToRead,
        LPDWORD lpNumberOfBytesRead,
        LPOVERLAPPED lpOverlapped
);

static PFN_ReadFile ORG_ReadFile = ReadFile;

DEBUGAPI
BOOL
WINAPI
MyReadFile(
        _In_ HANDLE hFile,
        _Out_writes_bytes_to_opt_(nNumberOfBytesToRead, *lpNumberOfBytesRead) __out_data_source(FILE) LPVOID lpBuffer,
        _In_ DWORD nNumberOfBytesToRead,
        _Out_opt_ LPDWORD lpNumberOfBytesRead,
        _Inout_opt_ LPOVERLAPPED lpOverlapped
){
    mx.lock();


    auto offset = GetFilePointer(hFile);

    SPDLOG_INFO("get Fileptr Success");
    auto aimLen = nNumberOfBytesToRead;
    DWORD readLen = -1;
    if(lpNumberOfBytesRead) readLen = *lpNumberOfBytesRead;
    char filePath[MAX_PATH]={0};
    auto fPsize = GetFinalPathNameByHandleA(hFile,filePath,MAX_PATH,0);
    char *key;
    char tmp;
    if ( strstr (filePath, "F:")){
        key = (char*)lpBuffer-16;
        tmp=*(char*)lpBuffer;
        *(char*)lpBuffer=0;
        SPDLOG_INFO("Video Key:{}",key);
        *(char*)lpBuffer=tmp;
        g_buffer=lpBuffer;
        g_offset=offset;
        g_length=readLen;
    }

    BOOL result = ORG_ReadFile(hFile, lpBuffer, nNumberOfBytesToRead, lpNumberOfBytesRead, lpOverlapped);
    SPDLOG_INFO("Buffer:{} ReadFile:{} -> {} -> {} -> {}",lpBuffer,offset,aimLen,readLen,filePath);
    mx.unlock();

    return result;
}
void HookOn(){

    //开始事务
    DetourTransactionBegin();
    //更新线程信息
    DetourUpdateThread(GetCurrentThread());
    //将拦截的函数附加到原函数的地址上
    DetourAttach(&(PVOID&)ORG_ReadFile, MyReadFile);
    //结束事务
    DetourTransactionCommit();

}
void EndHook() {
    //开始detours事务
    DetourTransactionBegin();
    //更新线程信息
    DetourUpdateThread(GetCurrentThread());
    //将拦截的函数从原函数的地址上解除
    DetourDetach(&(PVOID&)ORG_ReadFile, MyReadFile);
    //结束detours事务
    DetourTransactionCommit();
}
void InitLog(){
    try
    {
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("log.txt", true);
        file_sink->set_level(spdlog::level::err);

        std::vector<spdlog::sink_ptr> sinks {console_sink, file_sink};
        auto logger = std::make_shared<spdlog::logger>("multi_sink", sinks.begin(), sinks.end());

        spdlog::set_default_logger(logger);

        spdlog::flush_on(spdlog::level::info);
    }
    catch (const spdlog::spdlog_ex &ex)
    {
        std::cout << "Log init failed: " << ex.what() << std::endl;
    }
}
void EndLog(){
    spdlog::drop("basic_logger");
}

BOOL WINAPI DllMain(
        HINSTANCE hinstDLL,  // handle to DLL module
        DWORD fdwReason,     // reason for calling function
        LPVOID lpvReserved ) { // reserved

    HWND hwnd = GetActiveWindow();

    switch( fdwReason )
    {
        case DLL_PROCESS_ATTACH:
            // Initialize once for each new process.
            // Return FALSE to fail DLL load.
//            CreateThread(0, 0, (LPTHREAD_START_ROUTINE)initConsolo, 0, 0, 0);
            CreateConsoleWindow();
            InitLog();
            SPDLOG_INFO("OK!");
            HookOn();


            break;

        case DLL_THREAD_ATTACH:
            // Do thread-specific initialization.
            break;

        case DLL_THREAD_DETACH:
            // Do thread-specific cleanup.
            break;

        case DLL_PROCESS_DETACH:

            if (lpvReserved != nullptr)
            {
                break; // do not do cleanup if process termination scenario
            }
            EndHook();
            EndLog();

            // Perform any necessary cleanup.
            break;
    }
    return TRUE;  // Successful DLL_PROCESS_ATTACH.

}