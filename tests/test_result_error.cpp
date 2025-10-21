#include <gtest/gtest.h>

#include <array>
#include <memory>
#include <sstream>
#include <variant>

#include "player/common/error.h"

using namespace zenplay;

/**
 * @class ResultErrorTest
 * @brief 结果和错误处理系统的单元测试
 *
 * 测试范围：
 *   - ErrorCode 枚举和转换
 *   - Result<T> 基本功能（Ok, Err, IsOk, IsErr）
 *   - Result<T> 值访问方法
 *   - Result<void> 特化版本
 *   - 链式操作（AndThen, Map, OrElse, MapErr）
 *   - 移动语义和所有权转移
 *   - 便捷方法（ValueOr, FullMessage）
 */
class ResultErrorTest : public ::testing::Test {
 protected:
  void SetUp() override {}
  void TearDown() override {}
};

// ============================================================================
// 第一部分：ErrorCode 枚举和转换
// ============================================================================

TEST_F(ResultErrorTest, ErrorCodeToStringConversion) {
  // 测试通用错误
  EXPECT_STREQ(ErrorCodeToString(ErrorCode::kSuccess), "Success");
  EXPECT_STREQ(ErrorCodeToString(ErrorCode::kInvalidParameter),
               "InvalidParameter");
  EXPECT_STREQ(ErrorCodeToString(ErrorCode::kUnknown), "Unknown");

  // 测试解封装/IO 错误
  EXPECT_STREQ(ErrorCodeToString(ErrorCode::kIOError), "IOError");
  EXPECT_STREQ(ErrorCodeToString(ErrorCode::kInvalidFormat), "InvalidFormat");
  EXPECT_STREQ(ErrorCodeToString(ErrorCode::kStreamNotFound), "StreamNotFound");

  // 测试解码错误
  EXPECT_STREQ(ErrorCodeToString(ErrorCode::kDecoderError), "DecoderError");
  EXPECT_STREQ(ErrorCodeToString(ErrorCode::kUnsupportedCodec),
               "UnsupportedCodec");
  EXPECT_STREQ(ErrorCodeToString(ErrorCode::kDecoderInitFailed),
               "DecoderInitFailed");

  // 测试音频错误
  EXPECT_STREQ(ErrorCodeToString(ErrorCode::kAudioError), "AudioError");
  EXPECT_STREQ(ErrorCodeToString(ErrorCode::kAudioDeviceNotFound),
               "AudioDeviceNotFound");

  // 测试网络错误
  EXPECT_STREQ(ErrorCodeToString(ErrorCode::kNetworkError), "NetworkError");
  EXPECT_STREQ(ErrorCodeToString(ErrorCode::kConnectionTimeout),
               "ConnectionTimeout");

  // 测试渲染错误
  EXPECT_STREQ(ErrorCodeToString(ErrorCode::kRenderError), "RenderError");
}

TEST_F(ResultErrorTest, ErrorCodeToStringForUnknownCode) {
  // 测试未知错误码
  EXPECT_STREQ(ErrorCodeToString(static_cast<ErrorCode>(-999)),
               "UnknownErrorCode");
}

// ============================================================================
// 第二部分：Result<T> 基本构造和查询
// ============================================================================

TEST_F(ResultErrorTest, ResultOkConstruction) {
  Result<int> r = Result<int>::Ok(42);
  EXPECT_TRUE(r.IsOk());
  EXPECT_FALSE(r.IsErr());
  EXPECT_EQ(r.Code(), ErrorCode::kSuccess);
  EXPECT_EQ(r.Value(), 42);
  EXPECT_EQ(r.Message(), "");
}

TEST_F(ResultErrorTest, ResultErrConstruction) {
  Result<int> r =
      Result<int>::Err(ErrorCode::kInvalidParameter, "param must > 0");
  EXPECT_FALSE(r.IsOk());
  EXPECT_TRUE(r.IsErr());
  EXPECT_EQ(r.Code(), ErrorCode::kInvalidParameter);
  EXPECT_EQ(r.Message(), "param must > 0");
  EXPECT_STREQ(r.CodeString(), "InvalidParam");
}

TEST_F(ResultErrorTest, ResultErrWithoutMessage) {
  Result<std::string> r = Result<std::string>::Err(ErrorCode::kIOError);
  EXPECT_FALSE(r.IsOk());
  EXPECT_EQ(r.Code(), ErrorCode::kIOError);
  EXPECT_EQ(r.Message(), "");
}

TEST_F(ResultErrorTest, ResultDefaultConstruction) {
  // 默认构造得到 NOT_INITIALIZED 状态
  Result<double> r;
  EXPECT_FALSE(r.IsOk());
  EXPECT_EQ(r.Code(), ErrorCode::kNotInitialized);
}

// ============================================================================
// 第三部分：Result<T> 值访问
// ============================================================================

TEST_F(ResultErrorTest, ResultValueAccess) {
  Result<int> r = Result<int>::Ok(100);
  EXPECT_EQ(r.Value(), 100);
  EXPECT_EQ(r.ValueOr(99), 100);
}

TEST_F(ResultErrorTest, ResultValueOrDefault) {
  Result<int> r = Result<int>::Err(ErrorCode::kInvalidParameter);
  EXPECT_EQ(r.ValueOr(99), 99);
}

TEST_F(ResultErrorTest, ResultTakeValueForMove) {
  Result<std::string> r = Result<std::string>::Ok("hello world");
  EXPECT_TRUE(r.IsOk());

  // 转移所有权
  std::string value = r.TakeValue();
  EXPECT_EQ(value, "hello world");
}

TEST_F(ResultErrorTest, ResultWithPointer) {
  // 测试指针类型
  int* ptr = new int(42);
  Result<int*> r = Result<int*>::Ok(ptr);

  EXPECT_TRUE(r.IsOk());
  EXPECT_EQ(*r.Value(), 42);
  EXPECT_EQ(r.Value(), ptr);

  delete ptr;
}

TEST_F(ResultErrorTest, ResultWithUniquePtr) {
  // 测试 unique_ptr（所有权转移）
  auto ptr = std::make_unique<int>(123);
  Result<std::unique_ptr<int>> r =
      Result<std::unique_ptr<int>>::Ok(std::move(ptr));

  EXPECT_TRUE(r.IsOk());
  EXPECT_EQ(*r.Value(), 123);

  // 转移所有权
  auto taken = r.TakeValue();
  EXPECT_EQ(*taken, 123);
}

TEST_F(ResultErrorTest, ResultWithSharedPtr) {
  // 测试 shared_ptr
  auto ptr = std::make_shared<std::string>("test");
  Result<std::shared_ptr<std::string>> r =
      Result<std::shared_ptr<std::string>>::Ok(ptr);

  EXPECT_TRUE(r.IsOk());
  EXPECT_EQ(*r.Value(), "test");
}

// ============================================================================
// 第四部分：Result<void> 特化
// ============================================================================

TEST_F(ResultErrorTest, VoidResultOk) {
  VoidResult r = VoidResult::Ok();
  EXPECT_TRUE(r.IsOk());
  EXPECT_FALSE(r.IsErr());
  EXPECT_EQ(r.Code(), ErrorCode::kSuccess);
}

TEST_F(ResultErrorTest, VoidResultErr) {
  VoidResult r = VoidResult::Err(ErrorCode::kDecoderError, "decode failed");
  EXPECT_FALSE(r.IsOk());
  EXPECT_TRUE(r.IsErr());
  EXPECT_EQ(r.Code(), ErrorCode::kDecoderError);
  EXPECT_EQ(r.Message(), "decode failed");
}

TEST_F(ResultErrorTest, VoidResultTypeAlias) {
  // 测试 VoidResult 类型别名
  Result<void> r1 = Result<void>::Ok();
  VoidResult r2 = VoidResult::Ok();

  EXPECT_TRUE(r1.IsOk());
  EXPECT_TRUE(r2.IsOk());
}

// ============================================================================
// 第五部分：移动语义
// ============================================================================

TEST_F(ResultErrorTest, ResultMoveConstruction) {
  Result<std::string> r1 = Result<std::string>::Ok("hello");
  Result<std::string> r2 = std::move(r1);

  EXPECT_TRUE(r2.IsOk());
  EXPECT_EQ(r2.Value(), "hello");
}

TEST_F(ResultErrorTest, ResultMoveAssignment) {
  Result<std::vector<int>> r1 = Result<std::vector<int>>::Ok({1, 2, 3});
  Result<std::vector<int>> r2 =
      Result<std::vector<int>>::Err(ErrorCode::kUnknown);

  r2 = std::move(r1);

  EXPECT_TRUE(r2.IsOk());
  EXPECT_EQ(r2.Value().size(), 3);
  EXPECT_EQ(r2.Value()[0], 1);
}

TEST_F(ResultErrorTest, ResultCopyingIsDeleted) {
  Result<int> r1 = Result<int>::Ok(42);

  // 下面的代码应该编译失败（我们通过 static_assert 验证）
  // Result<int> r2 = r1;  // 编译错误
  // r1 = r2;              // 编译错误

  // 这个测试检查是否禁用了拷贝
  EXPECT_TRUE(std::is_move_constructible_v<Result<int>>);
  EXPECT_FALSE(std::is_copy_constructible_v<Result<int>>);
  EXPECT_TRUE(std::is_move_assignable_v<Result<int>>);
  EXPECT_FALSE(std::is_copy_assignable_v<Result<int>>);
}

// ============================================================================
// 第六部分：链式操作 - AndThen
// ============================================================================

TEST_F(ResultErrorTest, ResultAndThenSuccess) {
  auto r =
      Result<int>::Ok(5).AndThen([](int v) { return Result<int>::Ok(v * 2); });

  EXPECT_TRUE(r.IsOk());
  EXPECT_EQ(r.Value(), 10);
}

TEST_F(ResultErrorTest, ResultAndThenFailure) {
  auto r = Result<int>::Err(ErrorCode::kInvalidParameter, "bad param")
               .AndThen([](int v) { return Result<int>::Ok(v * 2); });

  EXPECT_FALSE(r.IsOk());
  EXPECT_EQ(r.Code(), ErrorCode::kInvalidParameter);
  EXPECT_EQ(r.Message(), "bad param");
}

TEST_F(ResultErrorTest, ResultAndThenChain) {
  auto r = Result<int>::Ok(2)
               .AndThen([](int v) { return Result<int>::Ok(v + 3); })
               .AndThen([](int v) { return Result<int>::Ok(v * 2); });

  EXPECT_TRUE(r.IsOk());
  EXPECT_EQ(r.Value(), 10);  // (2 + 3) * 2
}

TEST_F(ResultErrorTest, ResultAndThenChainWithError) {
  auto r = Result<int>::Ok(2)
               .AndThen([](int v) {
                 if (v < 5) {
                   return Result<int>::Err(ErrorCode::kInvalidParameter,
                                           "too small");
                 }
                 return Result<int>::Ok(v * 2);
               })
               .AndThen([](int v) { return Result<int>::Ok(v + 100); });

  EXPECT_FALSE(r.IsOk());
  EXPECT_EQ(r.Code(), ErrorCode::kInvalidParameter);
}

// ============================================================================
// 第七部分：链式操作 - Map
// ============================================================================

TEST_F(ResultErrorTest, ResultMapSuccess) {
  auto r = Result<int>::Ok(42).Map([](int v) { return std::to_string(v); });

  EXPECT_TRUE(r.IsOk());
  EXPECT_EQ(r.Value(), "42");
}

TEST_F(ResultErrorTest, ResultMapFailure) {
  auto r =
      Result<int>::Err(ErrorCode::kIOError, "file not found").Map([](int v) {
        return std::to_string(v);
      });

  EXPECT_FALSE(r.IsOk());
  EXPECT_EQ(r.Code(), ErrorCode::kIOError);
}

TEST_F(ResultErrorTest, ResultMapChain) {
  auto r = Result<int>::Ok(10).Map([](int v) { return v + 5; }).Map([](int v) {
    return std::to_string(v);
  });

  EXPECT_TRUE(r.IsOk());
  EXPECT_EQ(r.Value(), "15");
}

// ============================================================================
// 第八部分：链式操作 - OrElse
// ============================================================================

TEST_F(ResultErrorTest, ResultOrElseRecoveryFromError) {
  auto r = Result<int>::Err(ErrorCode::kInvalidParameter, "bad value")
               .OrElse([](ErrorCode e) {
                 if (e == ErrorCode::kInvalidParameter) {
                   return Result<int>::Ok(0);
                 }
                 return Result<int>::Err(e, "unexpected error");
               });

  EXPECT_TRUE(r.IsOk());
  EXPECT_EQ(r.Value(), 0);
}

TEST_F(ResultErrorTest, ResultOrElseWithoutError) {
  auto r = Result<int>::Ok(42).OrElse(
      [](ErrorCode e) { return Result<int>::Ok(0); });

  EXPECT_TRUE(r.IsOk());
  EXPECT_EQ(r.Value(), 42);  // 原值保留
}

// ============================================================================
// 第九部分：链式操作 - MapErr
// ============================================================================

TEST_F(ResultErrorTest, ResultMapErrSuccess) {
  auto r = Result<int>::Ok(42).MapErr(
      [](ErrorCode e) { return ErrorCode::kUnknown; });

  EXPECT_TRUE(r.IsOk());
  EXPECT_EQ(r.Value(), 42);
}

TEST_F(ResultErrorTest, ResultMapErrWithError) {
  auto r = Result<int>::Err(ErrorCode::kInvalidParameter, "original error")
               .MapErr([](ErrorCode e) { return ErrorCode::kUnknown; });

  EXPECT_FALSE(r.IsOk());
  EXPECT_EQ(r.Code(), ErrorCode::kUnknown);
  EXPECT_EQ(r.Message(), "original error");  // 消息保留
}

// ============================================================================
// 第十部分：便捷方法
// ============================================================================

TEST_F(ResultErrorTest, ResultFullMessage) {
  Result<int> r1 =
      Result<int>::Err(ErrorCode::kDecoderError, "ffmpeg init failed");
  EXPECT_EQ(r1.FullMessage(), "DecoderError: ffmpeg init failed");

  Result<int> r2 = Result<int>::Err(ErrorCode::kIOError);
  EXPECT_EQ(r2.FullMessage(), "IOError");
}

TEST_F(ResultErrorTest, ResultCodeString) {
  Result<int> r = Result<int>::Err(ErrorCode::kAudioDeviceNotFound);
  EXPECT_STREQ(r.CodeString(), "AudioDeviceNotFound");
}

TEST_F(ResultErrorTest, ResultOutputStreamOperator) {
  Result<int> r =
      Result<int>::Err(ErrorCode::kNetworkError, "connection failed");

  std::ostringstream oss;
  oss << r;
  EXPECT_EQ(oss.str(), "NetworkError: connection failed");
}

// ============================================================================
// 第十一部分：VoidResult 链式操作
// ============================================================================

TEST_F(ResultErrorTest, VoidResultAndThenSuccess) {
  int counter = 0;
  auto r = VoidResult::Ok().AndThen([&counter]() {
    counter++;
    return VoidResult::Ok();
  });

  EXPECT_TRUE(r.IsOk());
  EXPECT_EQ(counter, 1);
}

TEST_F(ResultErrorTest, VoidResultAndThenWithError) {
  int counter = 0;
  auto r = VoidResult::Err(ErrorCode::kIOError).AndThen([&counter]() {
    counter++;
    return VoidResult::Ok();
  });

  EXPECT_FALSE(r.IsOk());
  EXPECT_EQ(counter, 0);  // 不应执行
}

TEST_F(ResultErrorTest, VoidResultOrElseRecovery) {
  int recovery_called = 0;
  auto r = VoidResult::Err(ErrorCode::kIOError)
               .OrElse([&recovery_called](ErrorCode e) { recovery_called++; });

  EXPECT_TRUE(r.IsOk());
  EXPECT_EQ(recovery_called, 1);
}

// ============================================================================
// 第十二部分：实际场景模拟
// ============================================================================

// 模拟解码器工厂
class MockDecoderFactory {
 public:
  Result<std::string> CreateDecoder(const std::string& codec_name) {
    if (codec_name.empty()) {
      return Result<std::string>::Err(ErrorCode::kInvalidParameter,
                                      "codec name is empty");
    }
    if (codec_name == "h264") {
      return Result<std::string>::Ok("H264Decoder");
    }
    if (codec_name == "aac") {
      return Result<std::string>::Ok("AACDecoder");
    }
    return Result<std::string>::Err(ErrorCode::kUnsupportedCodec,
                                    "codec not supported: " + codec_name);
  }
};

TEST_F(ResultErrorTest, ScenarioDecoderFactorySuccess) {
  MockDecoderFactory factory;
  auto result = factory.CreateDecoder("h264");

  EXPECT_TRUE(result.IsOk());
  EXPECT_EQ(result.Value(), "H264Decoder");
}

TEST_F(ResultErrorTest, ScenarioDecoderFactoryUnsupportedCodec) {
  MockDecoderFactory factory;
  auto result = factory.CreateDecoder("vp9");

  EXPECT_FALSE(result.IsOk());
  EXPECT_EQ(result.Code(), ErrorCode::kUnsupportedCodec);
  EXPECT_EQ(result.Message(), "codec not supported: vp9");
}

TEST_F(ResultErrorTest, ScenarioDecoderFactoryInvalidParam) {
  MockDecoderFactory factory;
  auto result = factory.CreateDecoder("");

  EXPECT_FALSE(result.IsOk());
  EXPECT_EQ(result.Code(), ErrorCode::kInvalidParameter);
}

// 模拟音频设备初始化
class MockAudioDevice {
 public:
  Result<void> Initialize(int sample_rate) {
    if (sample_rate <= 0) {
      return Result<void>::Err(ErrorCode::kInvalidParameter,
                               "sample rate must > 0");
    }
    if (sample_rate > 192000) {
      return Result<void>::Err(ErrorCode::kAudioFormatNotSupported,
                               "sample rate too high");
    }
    sample_rate_ = sample_rate;
    return Result<void>::Ok();
  }

  int GetSampleRate() const { return sample_rate_; }

 private:
  int sample_rate_ = 0;
};

TEST_F(ResultErrorTest, ScenarioAudioDeviceInitSuccess) {
  MockAudioDevice device;
  auto result = device.Initialize(48000);

  EXPECT_TRUE(result.IsOk());
  EXPECT_EQ(device.GetSampleRate(), 48000);
}

TEST_F(ResultErrorTest, ScenarioAudioDeviceInitFailure) {
  MockAudioDevice device;
  auto result = device.Initialize(-1);

  EXPECT_FALSE(result.IsOk());
  EXPECT_EQ(result.Code(), ErrorCode::kInvalidParameter);
}

TEST_F(ResultErrorTest, ScenarioAudioDeviceInitHighSampleRate) {
  MockAudioDevice device;
  auto result = device.Initialize(256000);

  EXPECT_FALSE(result.IsOk());
  EXPECT_EQ(result.Code(), ErrorCode::kAudioFormatNotSupported);
}

// 模拟文件打开操作链
class MockFileReader {
 public:
  Result<std::string> Open(const std::string& filename) {
    if (filename.empty()) {
      return Result<std::string>::Err(ErrorCode::kInvalidParameter,
                                      "filename is empty");
    }
    if (filename == "missing.mp4") {
      return Result<std::string>::Err(ErrorCode::kIOError, "file not found");
    }
    return Result<std::string>::Ok(filename);
  }

  Result<int> GetFileSize(const std::string& filename) {
    if (filename == "test.mp4") {
      return Result<int>::Ok(1024000);
    }
    return Result<int>::Err(ErrorCode::kIOError, "cannot get file size");
  }
};

TEST_F(ResultErrorTest, ScenarioFileOperationChain) {
  MockFileReader reader;

  // 链式调用：打开文件 -> 获取文件大小 -> 计算时长
  auto result =
      reader.Open("test.mp4").AndThen([&reader](const std::string& filename) {
        return reader.GetFileSize(filename).Map(
            [](int size) { return size / 1024; });  // 转换为 KB
      });

  EXPECT_TRUE(result.IsOk());
  EXPECT_EQ(result.Value(), 1000);  // 1024000 / 1024
}

TEST_F(ResultErrorTest, ScenarioFileNotFound) {
  MockFileReader reader;

  auto result = reader.Open("missing.mp4")
                    .AndThen([&reader](const std::string& filename) {
                      return reader.GetFileSize(filename);
                    });

  EXPECT_FALSE(result.IsOk());
  EXPECT_EQ(result.Code(), ErrorCode::kIOError);
}

// ============================================================================
// 第十三部分：性能相关测试
// ============================================================================

TEST_F(ResultErrorTest, ResultNoCopyOverhead) {
  // 由于禁用了拷贝，应该没有拷贝开销
  // 这是一个编译期验证
  Result<std::vector<int>> r = Result<std::vector<int>>::Ok({1, 2, 3, 4, 5});

  auto r2 = std::move(r);
  EXPECT_EQ(r2.Value().size(), 5);
}

TEST_F(ResultErrorTest, ResultWithLargePayload) {
  // 测试大型数据结构的所有权转移
  std::vector<int> large_vec(10000);
  for (int i = 0; i < 10000; ++i) {
    large_vec[i] = i;
  }

  Result<std::vector<int>> r =
      Result<std::vector<int>>::Ok(std::move(large_vec));
  EXPECT_TRUE(r.IsOk());
  EXPECT_EQ(r.Value().size(), 10000);
  EXPECT_EQ(r.Value()[9999], 9999);
}

// ============================================================================
// 第十四部分：特殊类型测试
// ============================================================================

TEST_F(ResultErrorTest, ResultWithComplexType) {
  struct Config {
    std::string name;
    int port;
    bool ssl;
  };

  Result<Config> r = Result<Config>::Ok(Config{"localhost", 8080, true});

  EXPECT_TRUE(r.IsOk());
  EXPECT_EQ(r.Value().name, "localhost");
  EXPECT_EQ(r.Value().port, 8080);
  EXPECT_TRUE(r.Value().ssl);
}

TEST_F(ResultErrorTest, ResultWithArray) {
  using IntArray = std::array<int, 5>;
  IntArray arr = {1, 2, 3, 4, 5};

  Result<IntArray> r = Result<IntArray>::Ok(arr);

  EXPECT_TRUE(r.IsOk());
  EXPECT_EQ(r.Value()[0], 1);
  EXPECT_EQ(r.Value()[4], 5);
}

TEST_F(ResultErrorTest, ResultWithVariant) {
  using DataVariant = std::variant<int, std::string, double>;

  Result<DataVariant> r = Result<DataVariant>::Ok(std::string("test"));

  EXPECT_TRUE(r.IsOk());
  EXPECT_TRUE(std::holds_alternative<std::string>(r.Value()));
  EXPECT_EQ(std::get<std::string>(r.Value()), "test");
}

// ============================================================================
// 第十五部分：边界情况
// ============================================================================

TEST_F(ResultErrorTest, EmptyMessageError) {
  Result<int> r = Result<int>::Err(ErrorCode::kIOError, "");

  EXPECT_FALSE(r.IsOk());
  EXPECT_EQ(r.Message(), "");
  EXPECT_EQ(r.FullMessage(), "IOError");
}

TEST_F(ResultErrorTest, VeryLongErrorMessage) {
  std::string long_msg(10000, 'x');
  Result<int> r = Result<int>::Err(ErrorCode::kUnknown, long_msg);

  EXPECT_FALSE(r.IsOk());
  EXPECT_EQ(r.Message().size(), 10000);
}

TEST_F(ResultErrorTest, NestedResults) {
  // 测试 Result 嵌套（虽然不建议在实际代码中这样做）
  Result<Result<int>> nested = Result<Result<int>>::Ok(Result<int>::Ok(42));

  EXPECT_TRUE(nested.IsOk());
  EXPECT_TRUE(nested.Value().IsOk());
  EXPECT_EQ(nested.Value().Value(), 42);
}
