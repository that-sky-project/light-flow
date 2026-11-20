# Light Flow

Light Flow 是一个 HTModLoader 模组，用于把游戏内部的 `rtScreenshot` 渲染目标截取出来，
通过 FFmpeg 编码为视频文件或网络视频流（RTMP / SRT / UDP / TCP 等）。

它不会去截取桌面窗口，而是直接调用游戏自己的 Vulkan 渲染管线读取 `rtScreenshot`，
因此录制画面与游戏内置截图完全一致，也不会受到其他窗口遮挡的影响。

## 工作原理

1. 所有链接的游戏函数都通过从 FLAME.cc 迁移过来的 **MetaBinary / BinaryDescriptor**
   系统声明和解析。函数地址来自 `Data/Manifests/BinaryDescriptor.json`（签名扫描优先，RVA 兜底），
   不在代码里散落签名或硬编码地址。Hook `Compositor::Render()` 也是通过这套反射式
   声明完成的（`METABINARY_LINK_MFN`）。
2. 启动时用 `HTGetGameStatus()` 读取当前游戏版本（网易国服 / TGC 国际服），
   `BinaryDescriptorBarn::Initialize()` 按版本选择对应的描述符列表；Compositor /
   Renderer 的成员偏移也按版本取自 `skylink/skyCompositor.hpp` 中的布局表。
3. 在渲染前把当前 `Compositor` 与帧序号记录下来。
4. Hook `SystemIO::IsCapturingMovie()`，但**只在该帧 `Compositor::Render()` 内部查询时**
   返回 `true`（通过线程局部标记）。游戏主循环、平台层等其它调用者仍然拿到原值，
   因此画面呈现不会被停止；同时渲染图会进入电影捕获分支并写入 `rtScreenshot`，而
   正常的 `Tonemap` 呈现分支在 `outputMode == 2` 时同样会执行。
   网易版只有一个捕获标志（slot 53）；TGC 版在 `Compositor::Render()` 中会把两个
   捕获标志做 OR（slot 85 / 90），因此两个都会安装同样的线程局部强制 Hook。
5. 渲染结束后遍历 `Compositor` 的 RenderTarget 名称表找到 `rtScreenshot`，
   通过 `VulkanRenderer::GetPixels()` 把 GPU 纹理读回 CPU。
   通道顺序以渲染器纹理记录里的**真实 VkFormat**（`record + 96`）为准，而不是
   逻辑 `GfxTexelFormat`（`record + 176`）：`SuitableTextureFormat()` 可能把
   `R8G8B8A8` 替换成 `B8G8R8A8`。

   另外，`Data/Resources/ShaderDefs.lua` 里 `Tonemap_movie` 使用了 `BGR`
   compile define（而 `Tonemap_screenshot` 没有）。所以强制电影模式写出的
   `rtScreenshot` 实际是 BGR 排列；模块会在 movie 模式下再交换一次 R/B 映射，
   两者叠加才能得到正确的 RGB。
6. 把原始像素放入队列，由独立线程用 FFmpeg（libx264 / h264_mf / libopenh264 / mpeg4）
   编码并写入输出文件或流。

两份版本的 `RenderTarget` 纹理槽大小不同（网易版 104 字节，TGC 版 128 字节），
因此纹理索引使用 `RenderTargetLayout` 中的 stride/count 偏移，而不是
`sizeof(Texture)`。

如果游戏本身请求了截图（`Compositor + 0x58`），该帧会临时放行游戏截图流程，
不会录制一张来自截图分支的画面。

## 依赖

| 依赖 | 说明 |
| --- | --- |
| HTModLoader | 已在 `libraries/htmodloader` 中提供（1.10.3 头文件/导入库） |
| FFmpeg | 已在 `libraries/ffmpeg` 中提供（avcodec/avformat/avutil/swscale 导入库与 DLL） |
| rapidjson | 已是 git 子模块，配置读写使用 |
| Dear ImGui | **需要你自行添加子模块**，版本必须为 `v1.92.2b`（与 HTModLoader 1.10.3 匹配） |

添加 ImGui 子模块：

```bash
git submodule add https://github.com/ocornut/imgui libraries/imgui
git -C libraries/imgui checkout v1.92.2b
git add libraries/imgui .gitmodules
```

模组会自行编译 ImGui（参考 `McPythonEngine` 的做法），并在 `HTModRenderGui()`
中通过 `HTImGuiDispatch()` 复用 HTModLoader 的 ImGui 上下文与分配器。
因此不会与加载器内部的 ImGui 冲突。

## 构建

用 Visual Studio 2022 打开 `project/light-flow.sln`，选择 `Release | x64` 构建。

或者命令行：

```bash
msbuild project/light-flow.vcxproj -p:Configuration=Release -p:Platform=x64
```

构建产物在 `dist/`：

```
dist/
  light-flow.dll
  manifest.json
  Data/Manifests/BinaryDescriptor.json
  avcodec-63.dll
  avformat-63.dll
  avutil-61.dll
  swscale-10.dll
  swresample-7.dll
```

`dist` 目录整体作为模组安装到 HTModLoader 的 mods 目录即可。
FFmpeg 的 DLL 必须和 `light-flow.dll` 放在同一目录（构建脚本会自动复制）。
HTModLoader 一般以 `LOAD_WITH_ALTERED_SEARCH_PATH` 方式加载模组，同目录依赖会被找到；
如果你的加载器版本没有这样做，请把 `av*.dll` / `sw*.dll` 额外复制到 `Sky.exe` 同目录。

## 使用

| 快捷键 | 功能 |
| --- | --- |
| `F8` | 打开/关闭 Light Flow 面板 |
| `F9` | 开始/停止录制 |

面板中可以设置：

- **Output**：输出路径。留空时自动写入
  `<模组目录>\captures\light-flow-YYYYMMDD-HHMMSS.mp4`。
  支持直接填写 `rtmp://`、`rtmps://`、`srt://`、`udp://`、`tcp://` 地址。
- **Codec**：`libx264`（默认）、`h264_mf`、`libopenh264`、`mpeg4`。
- **Bitrate**：视频码率，单位 kbps。
- **Capture FPS**：抓取帧率上限，`0` 表示每个渲染帧都抓取。
- **Encoder queue**：编码队列长度。编码跟不上时会丢弃新帧，避免游戏卡顿。
- **Force movie capture mode**：通过 Hook 让游戏进入电影捕获分支，通常必须开启。
- **REC overlay**：在屏幕左上角显示录制状态。

设置会保存到 `<模组目录>\light-flow.json`。

## 二元描述文件

`Data/Manifests/BinaryDescriptor.json` 是 MetaBinary/BinaryDescriptor 的运行时描述文件。
顶层按游戏版本分为 `netease` 与 `tgc`，每个版本内部再按架构（`x64`）列出描述符：

```json
{
  "netease": { "x64": { "Compositor::Render$0": { ... }, ... } },
  "tgc":     { "x64": { "Compositor::Render$0": { ... }, ... } }
}
```

`BinaryDescriptorBarn::Initialize(json, profile)` 会优先读取 `root[profile][arch]`；
若文件里没有对应版本（或传入空 profile），则退回旧的 `root[arch]` 布局。
每一项对应 `src/skylink` 里的一个反射式声明：

- `sig.pattern` / `sig.match`：签名与地址表达式（见 FLAME.cc 的 `Docs/BinaryDescriptor.md`）；
- `rva`：签名扫描失败时的模块内偏移；
- `slot`：虚函数在 vftable 中的槽位。

当前两份构建的关键取值：

| 描述符 | 网易版 | TGC 版 |
| --- | --- | --- |
| `SystemIO::IsCapturingMovie$0` slot | 53 | 85 |
| `SystemIO::IsCapturingMovie$1` slot | （未使用）90 | 90 |
| `Renderer::GetPixels$0` slot | 87 | 90 |
| `Compositor::Render$0` RVA | `0x122CA00` | `0x1769950` |

更换游戏版本时，优先更新这个 JSON；结构体字段偏移（`CompositorLayout` /
`RendererLayout` / `RenderTargetLayout`）在 `src/skylink/skyCompositor.hpp`、
`src/skylink/skyRenderer.hpp` 中。

## 兼容性说明

- 支持两套 Windows 版本：
  - 网易国服 `Sky-32.2-260129`（`game_edition == HT_ImplSky_EditionChinese`）；
  - TGC 国际服（`game_edition == HT_ImplSky_EditionInternation`）。
  版本由 HTModLoader 的 `HTGetGameStatus()` 提供，模组据此选择描述符列表与
  结构体偏移。
- 其他版本可能需要重新核对 `src/skylink/skyCompositor.hpp` /
  `src/skylink/skyRenderer.hpp` 中的布局，以及 `Data/Manifests/BinaryDescriptor.json`
  中的签名/RVA/slot。
- `rtScreenshot` 必须是 CPU 可读（`VK_IMAGE_TILING_LINEAR`）的纹理。
  如果不是，模组会拒绝读取而不是让游戏 `abort()`。
- 录制期间游戏内置截图会延迟到下一帧执行（该帧不录制），这是有意为之。
- 编码分辨率在开始录制后的第一帧确定；之后窗口尺寸变化会自动缩放回该分辨率。

## 代码结构

```
src/
  mod.cpp                   HTModLoader 入口、版本检测、MetaBinary 初始化
  dllmain.cpp               模块句柄、线程库调用禁用
  metabinary/               FLAME.cc 的 MetaBinary / BinaryDescriptor 系统
    metabinary.*            MetaBinaryFunction / VirtualFunction / HookRedirect
    binaryDescriptor.*      JSON 描述、按版本选表、签名扫描、RVA 兜底
    binaryDescPatternMatch.* 描述表达式求值（$@、$a:b、# 等）
    platform/               Win64/HTModLoader 直接实现、x64 指令解码
  skylink/                  游戏结构与反射式链接声明（不含业务实现）
    skyGfx.hpp              GfxTexelFormat 与像素布局
    skyCompositor.hpp       Compositor 声明、CompositorLayout（网易/TGC）
    skyRenderer.hpp         Renderer/RenderTarget 声明、RendererLayout、
                            RenderTargetLayout（纹理槽 104/128）
    skySystemIO.hpp         SystemIO 捕获标志的虚函数链接
    skyTexture.hpp          Texture 布局与 GetReadableTexture 链接
  record/
    frameGrabber.*          rtScreenshot 定位、纹理格式校验、GetPixels 读回
    lightflow.*             录制状态机、帧队列、配置、Hook、ImGui 面板
    videoEncoder.*          FFmpeg 封装（编码 + 封包）
    sourcePixelFormat.hpp   源像素格式与 VkFormat 映射
  utils/                    日志、断言、基础类型
data/BinaryDescriptor.json  运行时二元描述文件（按版本分区）
```
