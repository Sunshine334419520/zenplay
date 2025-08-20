#include "thread_util.h"
#if defined(OS_WIN)
#include <windows.h>
#else
#include <sys/prctl.h>
#endif
#include <string>
#include <cctype>
#include <locale>
#include <cwchar>
#include <codecvt>

namespace {
bool UTF8ToUnicode(const std::string& strSource,
                   std::wstring& strDest) {
  std::wstring_convert<std::codecvt_utf8<wchar_t>> cv;
  strDest = cv.from_bytes(strSource);
  return true;
}

std::wstring UTF8ToUnicode(const std::string& strSource) {
  std::wstring result;
  UTF8ToUnicode(strSource, result);
  return result;
}
}

namespace loki {
#ifdef _WIN32

static bool NewWindowsSetThreadName(const std::string& threadName) {
  typedef HRESULT(WINAPI * SetThreadDescriptionFun)(HANDLE, PCWSTR);
  auto hmodule = LoadLibraryW(L"KernelBase.dll");
  if (!hmodule) {
    return false;
  }

    SetThreadDescriptionFun fun = (SetThreadDescriptionFun)GetProcAddress(hmodule, "SetThreadDescription");
    if (fun) {
      auto name = UTF8ToUnicode(threadName);
      fun(GetCurrentThread(), name.c_str());
      FreeLibrary(hmodule);
      return true;
    }

    FreeLibrary(hmodule);
    return true;
}

void SetThreadName(const std::string& threadName) {
  if (NewWindowsSetThreadName(threadName)) {
    return;
  }
#pragma pack(push, 8)
  typedef struct tagTHREADNAME_INFO {
    DWORD dwType;      // Must be 0x1000.
    LPCSTR szName;     // Pointer to name (in user addr space).
    DWORD dwThreadID;  // Thread ID (-1=caller thread).
    DWORD dwFlags;     // Reserved for future use, must be zero.
  } THREADNAME_INFO;
#pragma pack(pop)
  THREADNAME_INFO info;
  info.dwType = 0x1000;
  info.szName = threadName.c_str();
  info.dwThreadID = DWORD(-1);
  info.dwFlags = 0;
  const DWORD MS_VC_EXCEPTION = 0x406D1388;
  __try {
    RaiseException(MS_VC_EXCEPTION, 0, sizeof(info) / sizeof(ULONG_PTR),
                   (ULONG_PTR*)&info);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
  }
}
#else
// 只能显示16个字符，包含了'\0'
void SetThreadName(const std::string& threadName) {
  prctl(PR_SET_NAME, threadName.c_str());
}
#endif
}  // namespace loki
