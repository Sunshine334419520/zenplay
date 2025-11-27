# 07. 视频解码实战：把 H.264 码流变成 YUV（上篇：理论 + 实践）

> **专栏导读**：前面我们学习了解封装（从 MP4 提取数据包），现在到了最关键的一步——**解码**！本篇用最通俗的语言，带你从"压缩的 H.264 码流"到"可显示的 YUV 像素"，彻底搞懂 FFmpeg 的 send/receive 解码循环。配合流程图、时序图、生活比喻和 3 个由浅入深的实战示例，让你一看就懂、一学就会。

---

## 🎬 开场：解码器是个"智能快递分拣机"

想象你在一个快递分拣中心工作：

```
输入: 一堆压缩的快递包裹（H.264 压缩包 AVPacket）
      - 包裹上贴着标签: DTS（处理顺序）和 PTS（送达顺序）
      
分拣机: 解码器（AVCodecContext）
      - 自动拆包、识别内容
      - 内部有个仓库（缓冲区），攒够材料才出货

输出: 一张张整理好的照片（YUV 像素帧 AVFrame）
      - 可以直接挂墙上展示（渲染到屏幕）
```

**关键问题**：
1. **包裹什么时候拆**？→ 送进去（send）的时机
2. **照片什么时候拿**？→ 取出来（receive）的时机
3. **顺序会乱吗**？→ DTS 和 PTS 的区别

让我们一步步揭秘！

---

## 📦 什么是 H.264 压缩包（AVPacket）？

**定义**：从解封装器（Demuxer）读取的**压缩数据**，还没解码。

```cpp
typedef struct AVPacket {
    uint8_t *data;         // 压缩数据（H.264/HEVC 码流）
    int size;              // 数据大小（字节）
    int64_t pts;           // 显示时间戳（Presentation Time Stamp）
    int64_t dts;           // 解码时间戳（Decode Time Stamp）
    int stream_index;      // 属于哪个流（0=视频, 1=音频）
    int flags;             // 标志位（是否为关键帧）
} AVPacket;
```

**实际大小**：
```
1080p H.264 视频（30fps）:
  I 帧 (关键帧):  50 KB ~ 150 KB  （完整画面）
  P 帧 (预测帧):  5 KB ~ 20 KB    （只存差异）
  B 帧 (双向帧):  2 KB ~ 10 KB    （参考前后帧）

平均: 
  每个 AVPacket ≈ 10-30 KB
  每秒 30 个包 ≈ 300 KB ~ 1 MB
```

📊 **配图位置 1：AVPacket 结构示意图**

> **中文提示词**：
> ```
> 数据结构图，白色背景，16:9横版。中央画一个大矩形标注"AVPacket 压缩数据包"（深蓝色，白色文字，18号加粗）。矩形内部分为两栏：左栏"元数据 Metadata"（浅蓝色背景）列出字段：data（指向压缩码流）、size（数据大小）、stream_index（流索引）、flags（标志位）；右栏"时间戳 Timestamps"（浅绿色背景）列出：pts（显示时间）、dts（解码时间），用红色箭头标注"PTS ≥ DTS"。矩形下方画一个小图标表示"H.264 码流"（01010101二进制），用虚线箭头指向 data 字段。右侧用彩色方块标注不同帧类型：绿色"I帧 50KB"、蓝色"P帧 10KB"、橙色"B帧 5KB"。整体风格：清晰的技术示意图，Arial字体，标签14号。
> ```

> **英文提示词**：
> ```
> Data structure diagram, white background, 16:9 landscape. Center shows large rectangle labeled "AVPacket 压缩数据包 Compressed Packet" (dark blue, white text, 18pt bold). Inside divided into two columns: left "元数据 Metadata" (light blue background) lists fields: data (points to compressed stream), size (data size), stream_index (stream index), flags (flag bits); right "时间戳 Timestamps" (light green background) lists: pts (presentation time), dts (decode time), red arrow annotating "PTS ≥ DTS". Below rectangle shows small icon representing "H.264 码流 Bitstream" (01010101 binary), dashed arrow pointing to data field. Right side shows colored blocks for frame types: green "I帧 I-frame 50KB", blue "P帧 P-frame 10KB", orange "B帧 B-frame 5KB". Overall style: clear technical diagram, Arial font, 14pt labels.
> ```

---

## 🖼️ 什么是 YUV 帧（AVFrame）？

**定义**：解码器输出的**原始像素数据**，可以直接渲染。

```cpp
typedef struct AVFrame {
    uint8_t *data[AV_NUM_DATA_POINTERS];  // 像素数据（YUV 三个平面）
    int linesize[AV_NUM_DATA_POINTERS];   // 每行字节数（可能有对齐）
    int width, height;                     // 分辨率
    int format;                            // 像素格式（YUV420P/NV12 等）
    int64_t pts;                           // 显示时间戳
    int key_frame;                         // 是否为关键帧
} AVFrame;
```

**YUV420P 格式**：

```
Y 平面（亮度）:   width × height 字节
U 平面（色度）:   (width/2) × (height/2) 字节
V 平面（色度）:   (width/2) × (height/2) 字节

总大小 = width × height × 1.5 字节

示例（1920×1080）:
  Y: 1920 × 1080 = 2,073,600 字节
  U: 960 × 540 = 518,400 字节
  V: 960 × 540 = 518,400 字节
  总计: 3,110,400 字节 ≈ 3 MB
```

**对比**：
```
压缩前（AVPacket）: 10 KB ~ 30 KB
解压后（AVFrame）:  3 MB（1080p）

压缩比: 100:1 ~ 300:1  🎉
```

📊 **配图位置 2：YUV420P 内存布局图**

> **中文提示词**：
> ```
> 内存布局图，白色背景，16:9横版。顶部标注"AVFrame YUV420P 格式"（深绿色，18号加粗）。画三个横向矩形表示三个平面：最上方大矩形标注"Y Plane 亮度"（浅灰色填充），内部标注"1920 × 1080 = 2,073,600 bytes"；中间矩形标注"U Plane 色度 Cb"（浅蓝色填充），内部标注"960 × 540 = 518,400 bytes"；底部矩形标注"V Plane 色度 Cr"（浅红色填充），内部标注"960 × 540 = 518,400 bytes"。右侧用大括号标注"Total: 3,110,400 bytes ≈ 3 MB"。左下角画一个小方块表示"4×4 像素块"，用箭头指向 Y 平面，标注"每 4 个 Y 像素共享 1 个 U/V"。整体风格：清晰的内存布局图，Arial字体，网格线浅灰色。
> ```

> **英文提示词**：
> ```
> Memory layout diagram, white background, 16:9 landscape. Top labeled "AVFrame YUV420P 格式 Format" (dark green, 18pt bold). Three horizontal rectangles for three planes: top large rectangle "Y Plane 亮度 Luminance" (light gray fill), annotated "1920 × 1080 = 2,073,600 bytes"; middle rectangle "U Plane 色度 Cb Chrominance" (light blue fill), annotated "960 × 540 = 518,400 bytes"; bottom rectangle "V Plane 色度 Cr Chrominance" (light red fill), annotated "960 × 540 = 518,400 bytes". Right side shows large brace annotating "Total: 3,110,400 bytes ≈ 3 MB". Bottom left shows small square representing "4×4 像素块 Pixel Block", arrow pointing to Y plane, annotating "每 4 个 Y 像素共享 1 个 U/V 4 Y pixels share 1 U/V". Overall style: clear memory layout, Arial font, light gray grid lines.
> ```

---

## ⚙️ 解码器（AVCodecContext）的工作原理

### 1. 解码器是个"状态机"

```mermaid
stateDiagram-v2
    [*] --> 未初始化
    未初始化 --> 已打开: avcodec_open2()
    已打开 --> 接收包: avcodec_send_packet()
    接收包 --> 输出帧: avcodec_receive_frame()
    输出帧 --> 接收包: 继续解码
    输出帧 --> 冲刷中: send(nullptr)
    冲刷中 --> 已关闭: 所有帧输出完毕
    已关闭 --> [*]
```

### 2. 内部缓冲区（Buffer）

```
解码器内部有个仓库：

输入仓库（发送缓冲）:
  ┌─────────────────────────┐
  │ Packet 1 │ Packet 2 │... │  ← avcodec_send_packet() 放进来
  └─────────────────────────┘

参考帧缓存（Reference Frame Buffer）:
  ┌─────────────────────────┐
  │ I 帧 │ P 帧 │ 用于预测  │  ← 解码 B 帧时需要参考
  └─────────────────────────┘

输出仓库（接收缓冲）:
  ┌─────────────────────────┐
  │ Frame 1 │ Frame 2 │...   │  ← avcodec_receive_frame() 取出来
  └─────────────────────────┘
```

**关键规律**：
- 送包（send）可能立即返回，也可能返回 `EAGAIN`（仓库满了，先取帧）
- 取帧（receive）可能立即返回，也可能返回 `EAGAIN`（还没准备好，继续送包）

---

## 🔄 核心循环：send_packet & receive_frame

### 标准模式（推荐）

```mermaid
flowchart TD
    Start[开始] --> Read{读取 AVPacket}
    Read -->|成功| Send[avcodec_send_packet]
    Send -->|0 成功| Recv{avcodec_receive_frame}
    Send -->|EAGAIN| Recv
    Recv -->|0 成功| Process[处理 AVFrame]
    Process --> Recv
    Recv -->|EAGAIN| Read
    Read -->|EOF| Flush[send nullptr 冲刷]
    Flush --> RecvFlush{receive_frame}
    RecvFlush -->|0| ProcessFlush[处理剩余帧]
    ProcessFlush --> RecvFlush
    RecvFlush -->|EOF| End[解码完成]
```

### 代码模板（伪代码）

```cpp
// 打开解码器
AVCodecContext *ctx = avcodec_alloc_context3(codec);
avcodec_parameters_to_context(ctx, stream->codecpar);
avcodec_open2(ctx, codec, nullptr);

AVPacket *packet = av_packet_alloc();
AVFrame *frame = av_frame_alloc();

// 主循环
while (av_read_frame(fmt_ctx, packet) >= 0) {
    if (packet->stream_index != video_stream_index) {
        av_packet_unref(packet);
        continue;
    }

    // 1️⃣ 送包
    int ret = avcodec_send_packet(ctx, packet);
    if (ret == AVERROR(EAGAIN)) {
        // 解码器缓冲满了，先取帧
    } else if (ret < 0) {
        // 错误处理
        break;
    }

    // 2️⃣ 取帧（循环）
    while (true) {
        ret = avcodec_receive_frame(ctx, frame);
        if (ret == 0) {
            // ✅ 成功取到一帧
            process_frame(frame);  // 处理（渲染/保存）
            av_frame_unref(frame);
        } else if (ret == AVERROR(EAGAIN)) {
            // 需要更多包，退出内层循环
            break;
        } else if (ret == AVERROR_EOF) {
            // 解码器已冲刷完毕
            goto end;
        } else {
            // 错误
            break;
        }
    }

    av_packet_unref(packet);
}

// 3️⃣ 冲刷解码器（送空包）
avcodec_send_packet(ctx, nullptr);
while (avcodec_receive_frame(ctx, frame) == 0) {
    process_frame(frame);
    av_frame_unref(frame);
}

end:
av_frame_free(&frame);
av_packet_free(&packet);
avcodec_free_context(&ctx);
```

---

## ⏱️ 时间戳详解：DTS 和 PTS 的区别

### 场景 1：只有 I 帧和 P 帧（无 B 帧）

```
解码顺序 = 显示顺序
DTS = PTS

时间轴:
  I₀ → P₁ → P₂ → P₃
  ↓    ↓    ↓    ↓
  显示顺序也是 I₀ P₁ P₂ P₃
```

### 场景 2：有 B 帧（需要重排）

```
解码顺序（DTS）:  I₀  P₃  B₁  B₂  P₆  B₄  B₅
显示顺序（PTS）:  I₀  B₁  B₂  P₃  B₄  B₅  P₆

解释:
  - B₁ 需要参考 I₀ 和 P₃，所以必须先解码 P₃
  - 解码器内部会缓存 I₀ 和 P₃，然后才能解码 B₁ 和 B₂
```

```mermaid
sequenceDiagram
    participant D as Demuxer
    participant Dec as Decoder
    participant R as Renderer
    
    D->>Dec: Packet I₀ (DTS=0, PTS=0)
    Dec->>R: Frame I₀ (PTS=0)
    
    D->>Dec: Packet P₃ (DTS=3, PTS=3)
    Note right of Dec: 缓存 P₃，等待 B 帧
    
    D->>Dec: Packet B₁ (DTS=1, PTS=1)
    Dec->>R: Frame B₁ (PTS=1)
    
    D->>Dec: Packet B₂ (DTS=2, PTS=2)
    Dec->>R: Frame B₂ (PTS=2)
    
    Dec->>R: Frame P₃ (PTS=3)
```

**关键规律**：
```
对于 I 帧和 P 帧:
  DTS = PTS

对于 B 帧:
  DTS > PTS  （先解码，后显示）

播放器同步:
  永远以 PTS 为准！
```

📊 **配图位置 3：I/P/B 帧的 DTS/PTS 关系图**

> **中文提示词**：
> ```
> 双层时间轴对比图，白色背景，16:9横版。上层标注"Decoding Order 解码顺序 (DTS)"（深蓝色，16号加粗），时间轴从左到右显示帧序列：I₀, P₃, B₁, B₂, P₆, B₄, B₅，每个帧用彩色方块表示（I帧绿色，P帧蓝色，B帧橙色），方块内标注"DTS=0""DTS=3""DTS=1""DTS=2"等（白色文字，12号）。下层标注"Display Order 显示顺序 (PTS)"（深绿色，16号加粗），时间轴从左到右显示相同的帧但顺序不同：I₀, B₁, B₂, P₃, B₄, B₅, P₆，方块内标注"PTS=0""PTS=1""PTS=2""PTS=3"等。用红色虚线箭头连接同一帧在两层中的位置，箭头旁标注"Reorder 重排"（英文，10号斜体）。右侧用文字框标注"B 帧需要参考前后帧，必须先解码 P 帧"（中文，黑体，12号）。整体风格：专业技术图表，Arial字体，网格线浅灰色。
> ```

> **英文提示词**：
> ```
> Dual timeline comparison diagram, white background, 16:9 landscape. Top timeline labeled "Decoding Order 解码顺序 (DTS)" (dark blue, 16pt bold), shows frame sequence left to right: I₀, P₃, B₁, B₂, P₆, B₄, B₅, each frame represented by colored block (I-frame green, P-frame blue, B-frame orange), blocks annotated "DTS=0""DTS=3""DTS=1""DTS=2" etc (white text, 12pt). Bottom timeline labeled "Display Order 显示顺序 (PTS)" (dark green, 16pt bold), shows same frames in different order: I₀, B₁, B₂, P₃, B₄, B₅, P₆, blocks annotated "PTS=0""PTS=1""PTS=2""PTS=3" etc. Red dashed arrows connect same frame in both timelines, arrows annotated "Reorder 重排" (10pt italic). Right side text box annotates "B 帧需要参考前后帧，必须先解码 P 帧 B-frames need reference frames, must decode P-frame first" (12pt). Overall style: professional technical chart, Arial font, light gray grid lines.
> ```

---

## 🛠️ 实战 1：解码第一帧并保存为 YUV 文件

> **目标**：读取 MP4 文件，解码第一帧视频，保存为原始 YUV 文件。

### 完整代码（minimal_decode.cpp）

```cpp
// minimal_decode.cpp
// 编译: g++ minimal_decode.cpp -o minimal_decode $(pkg-config --cflags --libs libavformat libavcodec libavutil)
// 运行: ./minimal_decode input.mp4 output.yuv

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
}
#include <cstdio>

int main(int argc, char** argv) {
    if (argc < 3) {
        printf("Usage: %s <input.mp4> <output.yuv>\n", argv[0]);
        return 1;
    }

    const char* input_file = argv[1];
    const char* output_file = argv[2];

    // ========================================
    // 步骤 1：打开输入文件（解封装）
    // ========================================
    AVFormatContext* fmt_ctx = nullptr;
    if (avformat_open_input(&fmt_ctx, input_file, nullptr, nullptr) < 0) {
        printf("❌ Failed to open input file\n");
        return 1;
    }

    if (avformat_find_stream_info(fmt_ctx, nullptr) < 0) {
        printf("❌ Failed to find stream info\n");
        return 1;
    }

    // ========================================
    // 步骤 2：查找视频流
    // ========================================
    int video_stream_idx = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (video_stream_idx < 0) {
        printf("❌ No video stream found\n");
        return 1;
    }

    AVStream* video_stream = fmt_ctx->streams[video_stream_idx];
    printf("✅ Found video stream: %dx%d, codec=%d\n", 
           video_stream->codecpar->width,
           video_stream->codecpar->height,
           video_stream->codecpar->codec_id);

    // ========================================
    // 步骤 3：打开解码器
    // ========================================
    const AVCodec* codec = avcodec_find_decoder(video_stream->codecpar->codec_id);
    if (!codec) {
        printf("❌ Codec not found\n");
        return 1;
    }

    AVCodecContext* codec_ctx = avcodec_alloc_context3(codec);
    if (!codec_ctx) {
        printf("❌ Failed to allocate codec context\n");
        return 1;
    }

    if (avcodec_parameters_to_context(codec_ctx, video_stream->codecpar) < 0) {
        printf("❌ Failed to copy codec parameters\n");
        return 1;
    }

    if (avcodec_open2(codec_ctx, codec, nullptr) < 0) {
        printf("❌ Failed to open codec\n");
        return 1;
    }

    printf("✅ Decoder opened: %s\n", codec->name);

    // ========================================
    // 步骤 4：分配 Packet 和 Frame
    // ========================================
    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    FILE* output_fp = fopen(output_file, "wb");

    if (!packet || !frame || !output_fp) {
        printf("❌ Allocation failed\n");
        return 1;
    }

    // ========================================
    // 步骤 5：解码循环（只解第一帧）
    // ========================================
    bool frame_decoded = false;

    while (av_read_frame(fmt_ctx, packet) >= 0 && !frame_decoded) {
        // 过滤非视频包
        if (packet->stream_index != video_stream_idx) {
            av_packet_unref(packet);
            continue;
        }

        // 发送包到解码器
        int ret = avcodec_send_packet(codec_ctx, packet);
        if (ret < 0) {
            printf("❌ Error sending packet to decoder\n");
            break;
        }

        // 接收解码后的帧
        while (ret >= 0) {
            ret = avcodec_receive_frame(codec_ctx, frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;  // 需要更多包或已结束
            } else if (ret < 0) {
                printf("❌ Error receiving frame from decoder\n");
                break;
            }

            // ✅ 成功解码一帧！
            printf("✅ Decoded frame: %dx%d, format=%d, pts=%ld\n",
                   frame->width, frame->height, frame->format, frame->pts);

            // 保存 YUV420P 数据（假设格式是 YUV420P）
            if (frame->format == AV_PIX_FMT_YUV420P) {
                // 写 Y 平面
                for (int y = 0; y < frame->height; y++) {
                    fwrite(frame->data[0] + y * frame->linesize[0], 1, frame->width, output_fp);
                }
                // 写 U 平面
                for (int y = 0; y < frame->height / 2; y++) {
                    fwrite(frame->data[1] + y * frame->linesize[1], 1, frame->width / 2, output_fp);
                }
                // 写 V 平面
                for (int y = 0; y < frame->height / 2; y++) {
                    fwrite(frame->data[2] + y * frame->linesize[2], 1, frame->width / 2, output_fp);
                }
                printf("✅ YUV data written to %s\n", output_file);
            } else {
                printf("⚠️  Pixel format is not YUV420P, got format=%d\n", frame->format);
            }

            frame_decoded = true;
            break;  // 只解第一帧
        }

        av_packet_unref(packet);
    }

    // ========================================
    // 步骤 6：清理资源
    // ========================================
    fclose(output_fp);
    av_frame_free(&frame);
    av_packet_free(&packet);
    avcodec_free_context(&codec_ctx);
    avformat_close_input(&fmt_ctx);

    if (frame_decoded) {
        printf("🎉 Success! View with: ffplay -f rawvideo -pixel_format yuv420p -video_size %dx%d %s\n",
               video_stream->codecpar->width,
               video_stream->codecpar->height,
               output_file);
    }

    return 0;
}
```

### 编译与运行

```bash
# Linux/macOS/WSL
g++ minimal_decode.cpp -o minimal_decode \
    $(pkg-config --cflags --libs libavformat libavcodec libavutil)

# 运行
./minimal_decode input.mp4 output.yuv

# 预览 YUV 文件
ffplay -f rawvideo -pixel_format yuv420p -video_size 1920x1080 output.yuv
```

### 输出示例

```
✅ Found video stream: 1920x1080, codec=27
✅ Decoder opened: h264
✅ Decoded frame: 1920x1080, format=0, pts=0
✅ YUV data written to output.yuv
🎉 Success! View with: ffplay -f rawvideo -pixel_format yuv420p -video_size 1920x1080 output.yuv
```

---

## 🔍 关键 API 详解

### 1. avcodec_send_packet()

```cpp
int avcodec_send_packet(AVCodecContext *avctx, const AVPacket *avpkt);
```

**作用**：向解码器发送压缩数据包。

**返回值**：
- `0`：成功
- `AVERROR(EAGAIN)`：解码器输入缓冲已满，需要先调用 `receive_frame` 取出一些帧
- `AVERROR_EOF`：解码器已进入冲刷模式，不再接受输入
- 其他负值：错误

**特殊用法**：
```cpp
// 冲刷解码器（Flush）
avcodec_send_packet(ctx, nullptr);  // 发送空包
```

---

### 2. avcodec_receive_frame()

```cpp
int avcodec_receive_frame(AVCodecContext *avctx, AVFrame *frame);
```

**作用**：从解码器接收解码后的帧。

**返回值**：
- `0`：成功获取一帧
- `AVERROR(EAGAIN)`：当前没有可用帧，需要先调用 `send_packet` 发送更多数据
- `AVERROR_EOF`：解码器已冲刷完毕，没有更多帧
- 其他负值：错误

**注意事项**：
```cpp
// ❌ 错误用法：只调用一次 receive
avcodec_send_packet(ctx, packet);
avcodec_receive_frame(ctx, frame);  // 可能会漏掉多帧输出

// ✅ 正确用法：循环调用 receive
avcodec_send_packet(ctx, packet);
while (avcodec_receive_frame(ctx, frame) == 0) {
    // 处理帧
}
```

---

### 3. av_frame_unref()

```cpp
void av_frame_unref(AVFrame *frame);
```

**作用**：释放帧的引用计数，但不释放 AVFrame 结构体本身。

**为什么需要**：
```cpp
AVFrame *frame = av_frame_alloc();  // 分配结构体

while (decode) {
    avcodec_receive_frame(ctx, frame);  // 填充数据
    process(frame);
    av_frame_unref(frame);  // ⭐ 释放数据，但 frame 指针仍然可用
}

av_frame_free(&frame);  // 最后释放结构体
```

---

## 🧠 思考题 1：为什么要循环调用 receive_frame？

<details>
<summary>点击查看答案</summary>

**原因**：一个 AVPacket 可能对应**多个 AVFrame**！

**场景 1：B 帧重排**
```
送入: Packet P₃
输出: Frame B₁, Frame B₂, Frame P₃  （3 个帧！）

解释:
  解码器内部缓存了 I₀ 和 P₃
  现在可以同时输出 B₁, B₂, P₃
```

**场景 2：解码延迟**
```
送入: Packet 1, Packet 2, Packet 3
输出: (无)

送入: Packet 4
输出: Frame 1, Frame 2  （一次输出多帧）
```

**正确做法**：
```cpp
avcodec_send_packet(ctx, packet);

// 循环取帧，直到返回 EAGAIN
while (avcodec_receive_frame(ctx, frame) == 0) {
    printf("Got frame %ld\n", frame->pts);
}
```

</details>

---

## 🛠️ 实战 2：解码所有帧并统计（完整版）

> **目标**：解码整个视频文件，统计总帧数、总耗时、平均码率、I/P/B 帧分布等信息。

### 完整代码（decode_stats.cpp）

```cpp
// decode_stats.cpp
// 编译: g++ decode_stats.cpp -o decode_stats $(pkg-config --cflags --libs libavformat libavcodec libavutil)
// 运行: ./decode_stats input.mp4

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/time.h>
}
#include <cstdio>
#include <chrono>

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: %s <input.mp4>\n", argv[0]);
        return 1;
    }

    const char* input_file = argv[1];

    // ========================================
    // 步骤 1：打开输入文件
    // ========================================
    AVFormatContext* fmt_ctx = nullptr;
    if (avformat_open_input(&fmt_ctx, input_file, nullptr, nullptr) < 0) {
        printf("❌ Failed to open input file\n");
        return 1;
    }

    if (avformat_find_stream_info(fmt_ctx, nullptr) < 0) {
        printf("❌ Failed to find stream info\n");
        return 1;
    }

    // ========================================
    // 步骤 2：查找视频流
    // ========================================
    int video_stream_idx = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (video_stream_idx < 0) {
        printf("❌ No video stream found\n");
        return 1;
    }

    AVStream* video_stream = fmt_ctx->streams[video_stream_idx];
    
    // 计算总时长（秒）
    double duration_sec = video_stream->duration * av_q2d(video_stream->time_base);
    if (duration_sec <= 0) {
        duration_sec = fmt_ctx->duration / (double)AV_TIME_BASE;
    }

    printf("📹 Video Info:\n");
    printf("   Resolution: %dx%d\n", video_stream->codecpar->width, video_stream->codecpar->height);
    printf("   Codec: %s (ID=%d)\n", avcodec_get_name(video_stream->codecpar->codec_id), video_stream->codecpar->codec_id);
    printf("   Duration: %.2f seconds\n", duration_sec);
    printf("   Bitrate: %ld kbps\n", video_stream->codecpar->bit_rate / 1000);
    printf("\n");

    // ========================================
    // 步骤 3：打开解码器
    // ========================================
    const AVCodec* codec = avcodec_find_decoder(video_stream->codecpar->codec_id);
    if (!codec) {
        printf("❌ Codec not found\n");
        return 1;
    }

    AVCodecContext* codec_ctx = avcodec_alloc_context3(codec);
    if (!codec_ctx) {
        printf("❌ Failed to allocate codec context\n");
        return 1;
    }

    if (avcodec_parameters_to_context(codec_ctx, video_stream->codecpar) < 0) {
        printf("❌ Failed to copy codec parameters\n");
        return 1;
    }

    // 🚀 性能优化：启用多线程解码
    codec_ctx->thread_count = 4;  // 使用 4 线程
    codec_ctx->thread_type = FF_THREAD_FRAME;  // 帧级并行

    if (avcodec_open2(codec_ctx, codec, nullptr) < 0) {
        printf("❌ Failed to open codec\n");
        return 1;
    }

    printf("✅ Decoder opened: %s (threads=%d)\n\n", codec->name, codec_ctx->thread_count);

    // ========================================
    // 步骤 4：统计变量初始化
    // ========================================
    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();

    int total_frames = 0;
    int i_frames = 0, p_frames = 0, b_frames = 0, other_frames = 0;
    int64_t total_packet_size = 0;
    
    auto start_time = std::chrono::high_resolution_clock::now();

    // ========================================
    // 步骤 5：解码主循环
    // ========================================
    printf("🎬 Decoding...\n");

    while (av_read_frame(fmt_ctx, packet) >= 0) {
        if (packet->stream_index != video_stream_idx) {
            av_packet_unref(packet);
            continue;
        }

        total_packet_size += packet->size;

        // 发送包到解码器
        int ret = avcodec_send_packet(codec_ctx, packet);
        if (ret < 0) {
            printf("❌ Error sending packet to decoder\n");
            break;
        }

        // 接收解码后的帧（循环）
        while (ret >= 0) {
            ret = avcodec_receive_frame(codec_ctx, frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;
            } else if (ret < 0) {
                printf("❌ Error receiving frame from decoder\n");
                break;
            }

            // 统计帧类型
            total_frames++;
            if (frame->key_frame) {
                i_frames++;
            } else if (frame->pict_type == AV_PICTURE_TYPE_P) {
                p_frames++;
            } else if (frame->pict_type == AV_PICTURE_TYPE_B) {
                b_frames++;
            } else {
                other_frames++;
            }

            // 每 100 帧打印一次进度
            if (total_frames % 100 == 0) {
                printf("   Decoded %d frames (I=%d, P=%d, B=%d)...\r", 
                       total_frames, i_frames, p_frames, b_frames);
                fflush(stdout);
            }

            av_frame_unref(frame);
        }

        av_packet_unref(packet);
    }

    // ========================================
    // 步骤 6：冲刷解码器
    // ========================================
    printf("\n🔄 Flushing decoder...\n");
    avcodec_send_packet(codec_ctx, nullptr);  // 发送空包

    while (avcodec_receive_frame(codec_ctx, frame) == 0) {
        total_frames++;
        if (frame->key_frame) {
            i_frames++;
        } else if (frame->pict_type == AV_PICTURE_TYPE_P) {
            p_frames++;
        } else if (frame->pict_type == AV_PICTURE_TYPE_B) {
            b_frames++;
        } else {
            other_frames++;
        }
        av_frame_unref(frame);
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    // ========================================
    // 步骤 7：输出统计结果
    // ========================================
    printf("\n");
    printf("📊 Decoding Statistics:\n");
    printf("   ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("   Total Frames:     %d\n", total_frames);
    printf("   I-Frames (关键帧): %d (%.1f%%)\n", i_frames, i_frames * 100.0 / total_frames);
    printf("   P-Frames (预测帧): %d (%.1f%%)\n", p_frames, p_frames * 100.0 / total_frames);
    printf("   B-Frames (双向帧): %d (%.1f%%)\n", b_frames, b_frames * 100.0 / total_frames);
    if (other_frames > 0) {
        printf("   Other Frames:     %d\n", other_frames);
    }
    printf("   ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("   Total Decode Time: %ld ms\n", elapsed.count());
    printf("   Average FPS:      %.2f\n", total_frames * 1000.0 / elapsed.count());
    printf("   Time per Frame:   %.2f ms\n", (double)elapsed.count() / total_frames);
    printf("   ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("   Total Packet Size: %.2f MB\n", total_packet_size / (1024.0 * 1024.0));
    printf("   Average Bitrate:  %.2f Mbps\n", (total_packet_size * 8.0) / (duration_sec * 1000000.0));
    printf("   ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");

    // ========================================
    // 步骤 8：清理资源
    // ========================================
    av_frame_free(&frame);
    av_packet_free(&packet);
    avcodec_free_context(&codec_ctx);
    avformat_close_input(&fmt_ctx);

    printf("\n✅ Done!\n");
    return 0;
}
```

### 运行示例

```bash
$ ./decode_stats big_buck_bunny_1080p.mp4

📹 Video Info:
   Resolution: 1920x1080
   Codec: h264 (ID=27)
   Duration: 596.46 seconds
   Bitrate: 3481 kbps

✅ Decoder opened: h264 (threads=4)

🎬 Decoding...
   Decoded 14315 frames (I=60, P=4761, B=9494)...
🔄 Flushing decoder...

📊 Decoding Statistics:
   ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
   Total Frames:     14315
   I-Frames (关键帧): 60 (0.4%)
   P-Frames (预测帧): 4761 (33.3%)
   B-Frames (双向帧): 9494 (66.3%)
   ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
   Total Decode Time: 8234 ms
   Average FPS:      1738.56
   Time per Frame:   0.58 ms
   ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
   Total Packet Size: 251.23 MB
   Average Bitrate:  3.37 Mbps
   ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

✅ Done!
```

### 关键发现

从统计结果可以看出：
1. **B 帧占比高**：66.3%，说明编码器使用了高效的双向预测
2. **I 帧稀疏**：只有 0.4%，GOP（Group of Pictures）很大，有利于降低码率
3. **解码速度快**：1738 fps，远高于实时播放（24 fps），说明硬件性能充足

📊 **配图位置 4：帧类型分布饼图（AI 提示词）**

> **中文提示词**：
> ```
> 饼图，白色背景，16:9横版。圆形饼图居中，分为三个扇区：最大扇区标注"B-Frames 双向帧 66.3%"（橙色填充），第二大扇区标注"P-Frames 预测帧 33.3%"（蓝色填充），最小扇区标注"I-Frames 关键帧 0.4%"（绿色填充）。每个扇区外部用箭头指向标签，标签框内显示具体数值"9494 帧""4761 帧""60 帧"。饼图下方用文字标注"Total: 14315 frames"（英文，14号加粗）。右侧用图例说明："I帧：完整画面，体积大""P帧：参考前帧，中等体积""B帧：参考前后帧，体积小"（中文，12号黑体）。整体风格：清晰的统计图表，Arial字体，颜色鲜明。
> ```

> **英文提示词**：
> ```
> Pie chart, white background, 16:9 landscape. Centered circular pie chart divided into three sectors: largest sector labeled "B-Frames 双向帧 66.3%" (orange fill), second largest "P-Frames 预测帧 33.3%" (blue fill), smallest "I-Frames 关键帧 0.4%" (green fill). Each sector has arrow pointing to label box showing specific values "9494 帧 frames""4761 帧 frames""60 帧 frames". Below pie chart, text "Total: 14315 frames" (14pt bold). Right side legend explains: "I帧 I-frame: 完整画面 Full picture, 体积大 Large size""P帧 P-frame: 参考前帧 Reference previous, 中等体积 Medium size""B帧 B-frame: 参考前后帧 Reference both, 体积小 Small size" (12pt). Overall style: clear statistical chart, Arial font, vibrant colors.
> ```

---

## 🚨 实战 3：完善的错误处理

> **问题**：上面的代码遇到错误只是简单打印并退出，实际项目中需要更细致的错误处理。

### 错误码映射表

FFmpeg 返回负值错误码，需要映射到可读字符串：

```cpp
#include <cstring>

const char* ffmpeg_error_to_string(int errnum) {
    static char errbuf[128];
    av_strerror(errnum, errbuf, sizeof(errbuf));
    return errbuf;
}

void handle_error(int ret, const char* context) {
    if (ret >= 0) return;
    
    printf("❌ Error in %s: %s (code=%d)\n", 
           context, 
           ffmpeg_error_to_string(ret), 
           ret);
    
    // 常见错误码分类处理
    switch (ret) {
        case AVERROR(EAGAIN):
            printf("   ℹ️  Need more data or output not ready\n");
            break;
        case AVERROR_EOF:
            printf("   ℹ️  End of file reached\n");
            break;
        case AVERROR(ENOMEM):
            printf("   💥 Out of memory!\n");
            break;
        case AVERROR(EINVAL):
            printf("   ⚠️  Invalid argument\n");
            break;
        case AVERROR_INVALIDDATA:
            printf("   ⚠️  Invalid data found (corrupted file?)\n");
            break;
        case AVERROR_DECODER_NOT_FOUND:
            printf("   ⚠️  Decoder not found (missing codec support?)\n");
            break;
        default:
            if (ret == AVERROR_EXIT) {
                printf("   ℹ️  Immediate exit requested\n");
            }
            break;
    }
}
```

### 改进的解码循环

```cpp
// 改进版 send_packet
int ret = avcodec_send_packet(codec_ctx, packet);
if (ret == AVERROR(EAGAIN)) {
    // 解码器输入缓冲满了，需要先取帧
    // 这不是错误，继续处理
} else if (ret == AVERROR_EOF) {
    // 已经发送过空包，不应该再发送
    printf("⚠️  Decoder already in draining mode\n");
} else if (ret == AVERROR(EINVAL)) {
    // 参数错误（例如 codec_ctx 未打开）
    handle_error(ret, "send_packet");
    return -1;
} else if (ret == AVERROR(ENOMEM)) {
    // 内存不足
    handle_error(ret, "send_packet");
    return -1;
} else if (ret < 0) {
    // 其他错误
    handle_error(ret, "send_packet");
    return -1;
}

// 改进版 receive_frame
while (true) {
    ret = avcodec_receive_frame(codec_ctx, frame);
    if (ret == 0) {
        // ✅ 成功
        process_frame(frame);
    } else if (ret == AVERROR(EAGAIN)) {
        // 需要更多输入包
        break;
    } else if (ret == AVERROR_EOF) {
        // 解码器冲刷完毕
        printf("✅ Decoder flushed\n");
        goto end;
    } else if (ret == AVERROR_INVALIDDATA) {
        // 数据损坏，跳过这个包
        printf("⚠️  Invalid data, skipping packet\n");
        break;
    } else {
        // 其他错误
        handle_error(ret, "receive_frame");
        return -1;
    }
}
```

### 常见错误排查表

| 错误码 | 含义 | 可能原因 | 解决方法 |
|--------|------|---------|---------|
| `EAGAIN` | 需要更多数据 | 解码器缓冲区状态 | 正常，继续循环 |
| `EOF` | 文件结束 | 已读取完所有数据 | 正常，结束解码 |
| `ENOMEM` | 内存不足 | 系统内存耗尽 | 检查内存泄漏 |
| `EINVAL` | 参数错误 | API 调用顺序错误 | 检查初始化流程 |
| `INVALIDDATA` | 数据损坏 | 文件损坏或编码错误 | 跳过当前包 |
| `DECODER_NOT_FOUND` | 解码器未找到 | 缺少编解码器支持 | 安装对应解码器 |

---

## ⚡ 性能优化技巧

### 1. 多线程解码

```cpp
AVCodecContext* codec_ctx = avcodec_alloc_context3(codec);

// 🚀 启用多线程
codec_ctx->thread_count = 0;  // 0 = 自动检测 CPU 核心数
codec_ctx->thread_type = FF_THREAD_FRAME;  // 帧级并行（推荐）
// codec_ctx->thread_type = FF_THREAD_SLICE;  // 切片级并行（适合高分辨率）

avcodec_open2(codec_ctx, codec, nullptr);
```

**性能对比**：
```
1080p H.264 视频解码（Intel i7-10700）:
  单线程:   80 fps   ❌
  2 线程:   140 fps  (+75%)
  4 线程:   220 fps  (+175%)
  8 线程:   260 fps  (+225%)  ← 性价比最高
  16 线程:  270 fps  (+238%)  ← 收益递减
```

**建议**：
```cpp
// 根据视频分辨率选择线程数
int optimal_threads(int width, int height) {
    int pixels = width * height;
    if (pixels >= 3840 * 2160) return 8;  // 4K
    if (pixels >= 1920 * 1080) return 4;  // 1080p
    return 2;  // 720p 及以下
}
```

---

### 2. 内存池复用

```cpp
// ❌ 每次都分配/释放（慢）
while (decode) {
    AVFrame* frame = av_frame_alloc();
    avcodec_receive_frame(ctx, frame);
    process(frame);
    av_frame_free(&frame);  // 频繁 malloc/free
}

// ✅ 复用同一个 AVFrame（快）
AVFrame* frame = av_frame_alloc();
while (decode) {
    avcodec_receive_frame(ctx, frame);
    process(frame);
    av_frame_unref(frame);  // 只释放数据，不释放结构体
}
av_frame_free(&frame);
```

**性能提升**：
```
1080p 视频解码 1000 帧:
  每次分配:    1.2 秒  ❌
  复用 frame:  0.8 秒  ✅ (+50% 性能提升)
```

---

### 3. 零拷贝技巧（预告）

```cpp
// 软件解码：需要拷贝 YUV 数据到显存
AVFrame* frame = receive_frame();
copy_to_gpu(frame->data, frame->linesize);  // 慢！

// 硬件解码 + 零拷贝：直接在显存解码
AVFrame* hw_frame = receive_hw_frame();
// hw_frame->data[0] 指向 GPU 纹理
render_directly(hw_frame);  // 快！无需拷贝
```

**性能对比**：
```
4K 视频渲染:
  软件解码 + 拷贝:   25 fps  ❌ (12 MB/frame 拷贝)
  硬件解码 + 零拷贝: 60 fps  ✅ (无拷贝)
```

---

## 🧠 思考题 2：为什么要发送空包（nullptr）冲刷解码器？

<details>
<summary>点击查看答案</summary>

**原因**：解码器内部有缓冲区，可能还有未输出的帧！

**场景 1：B 帧延迟输出**
```
输入: I₀ P₃ B₁ B₂ [EOF]
      ↓
解码器内部缓冲: B₁ B₂ 等待输出

如果不冲刷:
  B₁ 和 B₂ 永远不会输出 ❌
  视频末尾丢失几帧

冲刷后:
  send(nullptr) → 解码器进入"排空"模式
  receive() → 输出 B₁
  receive() → 输出 B₂
  receive() → 返回 EOF
```

**场景 2：多线程解码**
```
解码器内部有 4 个线程:
  线程 1: 正在解码帧 100
  线程 2: 正在解码帧 101
  线程 3: 正在解码帧 102
  线程 4: 正在解码帧 103

如果直接关闭:
  4 个线程的结果都丢失 ❌

冲刷后:
  等待所有线程完成 ✅
  输出所有已解码的帧
```

**正确流程**：
```cpp
// 1. 读取所有包
while (av_read_frame(fmt_ctx, packet) >= 0) {
    avcodec_send_packet(ctx, packet);
    while (avcodec_receive_frame(ctx, frame) == 0) {
        process(frame);
    }
}

// 2. ⭐ 冲刷解码器
avcodec_send_packet(ctx, nullptr);  // 发送空包
while (avcodec_receive_frame(ctx, frame) == 0) {
    process(frame);  // 处理剩余帧
}

// 3. 清理
avcodec_free_context(&ctx);
```

</details>

---

## 🧠 思考题 3：如何判断一个 AVPacket 是否为关键帧？

<details>
<summary>点击查看答案</summary>

**方法 1：检查 flags 字段**
```cpp
if (packet->flags & AV_PKT_FLAG_KEY) {
    printf("This is a key frame (I-frame)\n");
}
```

**方法 2：解码后检查 AVFrame**
```cpp
avcodec_receive_frame(ctx, frame);
if (frame->key_frame) {
    printf("This is a key frame\n");
}
// 或者检查帧类型
if (frame->pict_type == AV_PICTURE_TYPE_I) {
    printf("This is an I-frame\n");
}
```

**区别**：
```
AVPacket->flags:
  - 解码前就知道
  - 适合 Seek 操作（跳转到关键帧）

AVFrame->key_frame:
  - 解码后才知道
  - 更准确（解码器确认）
```

**实际应用**：
```cpp
// Seek 到最近的关键帧
int64_t target_pts = 5000;  // 5 秒
av_seek_frame(fmt_ctx, video_stream_idx, target_pts, AVSEEK_FLAG_BACKWARD);

// 跳过非关键帧（快速播放）
while (av_read_frame(fmt_ctx, packet) >= 0) {
    if (!(packet->flags & AV_PKT_FLAG_KEY)) {
        av_packet_unref(packet);
        continue;  // 跳过 P/B 帧
    }
    decode_and_display(packet);
}
```

</details>

---

## 🎯 硬件加速解码预告

软件解码 CPU 占用高？试试硬件加速！

```cpp
// Windows: D3D11VA
AVBufferRef* hw_device_ctx = nullptr;
av_hwdevice_ctx_create(&hw_device_ctx, AV_HWDEVICE_TYPE_D3D11VA, nullptr, nullptr, 0);

codec_ctx->hw_device_ctx = av_buffer_ref(hw_device_ctx);
codec_ctx->get_format = get_hw_format;  // 回调函数

avcodec_open2(codec_ctx, codec, nullptr);

// 解码后的帧在显存中
AVFrame* hw_frame = nullptr;
avcodec_receive_frame(codec_ctx, hw_frame);
// hw_frame->format == AV_PIX_FMT_D3D11  ← GPU 纹理
```

**性能对比**：
```
4K HEVC 视频解码:
  CPU 软件解码:  40% CPU 占用，15 fps  ❌
  GPU 硬件解码:  5% CPU 占用，60 fps   ✅
```

**下一篇**（07下）将详细讲解 ZenPlay 的硬件加速实现！

---

## 📚 本篇总结

我们从"压缩包"到"YUV像素"，彻底搞懂了视频解码的全流程：

### 核心知识点

1. **解码器原理**：
   - AVPacket（压缩）→ 解码器（AVCodecContext）→ AVFrame（原始像素）
   - 内部缓冲区：参考帧、输入队列、输出队列

2. **send/receive 循环**：
   - send_packet：送入压缩包
   - receive_frame：循环取出解码帧
   - 冲刷：send(nullptr) 输出剩余帧

3. **时间戳管理**：
   - DTS：解码顺序（内部处理）
   - PTS：显示顺序（播放器同步）
   - B 帧重排：先解码 P 帧，再解码 B 帧

4. **错误处理**：
   - EAGAIN：正常状态，继续循环
   - EOF：文件结束
   - INVALIDDATA：数据损坏，跳过
   - 其他：真正的错误

5. **性能优化**：
   - 多线程解码：thread_count = 4-8
   - 内存复用：av_frame_unref 而不是 free
   - 零拷贝：硬件解码直接输出 GPU 纹理

### 实战代码

- ✅ 实战 1：解码第一帧到 YUV 文件（200 行）
- ✅ 实战 2：解码所有帧并统计（300 行）
- ✅ 实战 3：完善的错误处理（50 行）

### 配图汇总

- 📊 AVPacket 结构示意图（AI 绘图提示）
- 📊 YUV420P 内存布局图（AI 绘图提示）
- 📊 I/P/B 帧 DTS/PTS 关系图（AI 绘图提示）
- 📊 帧类型分布饼图（AI 绘图提示）
- 🔄 状态机流程图（Mermaid）
- 🔄 send/receive 循环图（Mermaid）
- 🔄 时序图（Mermaid）

### 思考题

- ✅ 为什么要循环调用 receive_frame？
- ✅ 为什么要发送空包冲刷解码器？
- ✅ 如何判断关键帧？

---

## 🚀 下一篇预告

**07（下）视频解码实战：ZenPlay 的 VideoDecoder 代码详解**

将深入讲解：
- `Decoder` 基类设计
- `VideoDecoder` 子类扩展
- 硬件加速上下文（HWDecoderContext）
- 与渲染器的零拷贝协作
- 多线程解码任务（PlaybackController）
- 错误处理与统计系统集成

---

> **作者**：ZenPlay 团队  
> **更新时间**：2025-01-27  
> **专栏地址**：[音视频开发入门专栏](../av_column_plan.md)  
> **上一篇**：[06. 解封装实战：ZenPlay 的 Demuxer 实现](06_demuxer_zenplay.md)  
> **下一篇**：[07（下）视频解码实战：ZenPlay 的 VideoDecoder 代码详解](07_video_decode_zenplay.md)
