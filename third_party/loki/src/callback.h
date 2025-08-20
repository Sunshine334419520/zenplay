/**
* @Author: YangGuang
* @Date:   2018-10-10
* @Email:  guang334419520@126.com
* @Filename: callback.h
* @Last modified by:  YangGuang
*/

#pragma once

#include <functional>
#include <memory>

#include "loki_export.h"

namespace loki {

class OnceClosure;

class LOKI_EXPORT Closure {
 public:
  Closure(const std::function<void()> fun) : fun_(fun) {}

  Closure(const Closure& other) : fun_(other.fun_) {}

  Closure(const Closure&& other) noexcept : fun_(std::move(other.fun_)) {}

  Closure() : fun_(nullptr) {}

  Closure& operator=(const Closure& other) = default;
  Closure& operator=(Closure&& other) = default;

  Closure& operator=(std::function<void()> fun) {
    this->fun_ = fun;
    return *this;
  }

  /* Closure& operator=(std::function<void()>&& fun) {
           fun_ = fun;
           fun = nullptr;
           return
   }*/

  ~Closure() = default;

  bool is_null() const { return fun_ == nullptr; }

  void Run() { fun_(); }

  void Reset() { fun_ = nullptr; }

  explicit operator bool() { return !is_null(); }

  template <typename Fty>
  operator std::function<Fty>() {
    return fun_;
  }

 private:
  std::function<void()> fun_;
};

class LOKI_EXPORT OnceClosure {
 public:
  OnceClosure() : fun_(nullptr) {}

  OnceClosure(std::function<void()>&& fun) : fun_(std::move(fun)) {}

  OnceClosure(Closure&& other) : fun_(std::move(other)) {}

  OnceClosure(const OnceClosure& other) = delete;
  OnceClosure& operator=(const OnceClosure& other) = delete;

  OnceClosure(OnceClosure&& other) = default;
  OnceClosure& operator=(OnceClosure&& other) = default;

  ~OnceClosure() = default;

  bool is_null() const { return fun_ == nullptr; }

  void Run() & {
    OnceClosure cb = std::move(*this);
    cb.fun_();
  }

  void Run() && {
    OnceClosure cb = std::move(*this);
    std::move(cb).fun_();
  }

  void Reset() { fun_ = nullptr; }

  explicit operator bool() { return !is_null(); }
  template <typename Fty>
  operator std::function<Fty>() {
    return fun_;
  }

 private:
  std::function<void()> fun_;
};

template <typename T>
class FunctionView;  // Undefined.

template <typename RetT, typename... ArgT>
class FunctionView<RetT(ArgT...)> final {
 public:
  // Constructor for lambdas and other callables; it accepts every type of
  // argument except those noted in its enable_if call.
  template <
      typename F,
      typename std::enable_if<
          // Not for function pointers; we have another constructor for that
          // below.
          !std::is_function<typename std::remove_pointer<
              typename std::remove_reference<F>::type>::type>::value &&

          // Not for nullptr; we have another constructor for that below.
          !std::is_same<std::nullptr_t,
                        typename std::remove_cv<F>::type>::value &&

          // Not for FunctionView objects; we have another constructor for that
          // (the implicitly declared copy constructor).
          !std::is_same<FunctionView,
                        typename std::remove_cv<typename std::remove_reference<
                            F>::type>::type>::value>::type* = nullptr>
  FunctionView(F&& f)
      : call_(CallVoidPtr<typename std::remove_reference<F>::type>) {
    f_.void_ptr = &f;
  }

  // Constructor that accepts function pointers. If the argument is null, the
  // result is an empty FunctionView.
  template <
      typename F,
      typename std::enable_if<std::is_function<typename std::remove_pointer<
          typename std::remove_reference<F>::type>::type>::value>::type* =
          nullptr>
  FunctionView(F&& f)
      : call_(f ? CallFunPtr<typename std::remove_pointer<F>::type> : nullptr) {
    f_.fun_ptr = reinterpret_cast<void (*)()>(f);
  }

  // Constructor that accepts nullptr. It creates an empty FunctionView.
  template <typename F,
            typename std::enable_if<std::is_same<
                std::nullptr_t,
                typename std::remove_cv<F>::type>::value>::type* = nullptr>
  FunctionView(F&& f) : call_(nullptr) {}

  // Default constructor. Creates an empty FunctionView.
  FunctionView() : call_(nullptr) {}

  RetT operator()(ArgT... args) const {
    return call_(f_, std::forward<ArgT>(args)...);
  }

  // Returns true if we have a function, false if we don't (i.e., we're null).
  explicit operator bool() const { return !!call_; }

 private:
  union VoidUnion {
    void* void_ptr;
    void (*fun_ptr)();
  };

  template <typename F>
  static RetT CallVoidPtr(VoidUnion vu, ArgT... args) {
    return (*static_cast<F*>(vu.void_ptr))(std::forward<ArgT>(args)...);
  }
  template <typename F>
  static RetT CallFunPtr(VoidUnion vu, ArgT... args) {
    return (reinterpret_cast<typename std::add_pointer<F>::type>(vu.fun_ptr))(
        std::forward<ArgT>(args)...);
  }

  // A pointer to the callable thing, with type information erased. It's a
  // union because we have to use separate types depending on if the callable
  // thing is a function pointer or something else.
  VoidUnion f_;

  // Pointer to a dispatch function that knows the type of the callable thing
  // that's stored in f_, and how to call it. A FunctionView object is empty
  // (null) iff call_ is null.
  RetT (*call_)(VoidUnion, ArgT...);
};

template <typename Fty>
using Callback = std::function<Fty>;

} // namesapcer loki

