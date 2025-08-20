/**
* @Author: YangGuang
* @Date:   2018-10-10
* @Email:  guang334419520@126.com
* @Filename: loki_export.h
* @Last modified by:  YangGuang
*/
#ifndef LOKI_BASE_EXPORT_H
#define LOKI_BASE_EXPORT_H



#if defined(COMPONENT_BUILD)
#if defined(OS_WIN)

#if defined(BASE_IMPLEMENTATION)
#define LOKI_EXPORT __declspec(dllexport)
#else
#define LOKI_EXPORT __declspec(dllimport)
#endif  // defined(BASE_IMPLEMENTATION)

#else  // defined(WIN32)
#if defined(BASE_IMPLEMENTATION)
#define LOKI_EXPORT __attribute__((visibility("default")))
#else
#define LOKI_EXPORT
#endif  // defined(BASE_IMPLEMENTATION)
#endif

#else  // defined(COMPONENT_BUILD)
#define LOKI_EXPORT
#endif



#endif // !LOKI_BASE_EXPORT_H
