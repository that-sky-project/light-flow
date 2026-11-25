# 流光

> 千帧掠影疾如风，万象澄明入画中。<br>
> 莫叹流光留不住，一屏收尽世间虹。

**流光(LightFlow)** 是《光·遇》与《Sky: Children of the Light》的一个HTModLoader模组，用于截取游戏内部用于渲染截图的`rtScreenshot`渲染目标，并通过FFmpeg编码为视频文件或网络视频流，用以实现高帧率内录、补全官方在此处的空白。

该模组直接调用游戏的Vulkan渲染管线读取渲染目标，因此录制画面与游戏内置截图完全一致、不会录制到游戏UI，也不会受到其他窗口遮挡的影响。

> [!IMPORTANT]
> 生成式人工智能使用声明：<br>
> 本模组使用AI辅助开发。项目组保证所有实际参与构建的代码都经由人工审核并规范化。<br>
> 本模组所有技术路线设计、游戏二进制分析与可行性检验均由人工完成。

## 兼容性

|客户端类型|最旧兼容版本|最新兼容版本|
|-|-|-|
|网易版(`"netease"`)|`0.16.5`|`0.16.8`|
|国际版(`"tgc"`)|`0.32.2`|`0.35.5`|

## 安装

1. 安装[HTModLoader](https://github.com/HTMonkeyG/HTML-Sky)。
2. 下载[releases](https://github.com/that-sky-project/light-flow/releases/latest)中的最新版本mod压缩包。
3. 将安装包内文件解压至`<游戏安装目录>\htmodloader\mods\<模组目录>`中，其中模组目录可以为任何名称。建议使用`light-flow`。最终目录树应该如下所示：
```
<游戏安装目录>
 ├─Sky.exe
 ├─htmodloader
 │ └─mods
 │   ├─light-flow
 │   │ ├─manifest.json
 │   │ ├─light-flow.dll
 │   │ └─<...>
 │   └─<...>
 ├─html-config.json
 ├─winhttp.dll
 └─<...>
```

## 使用

|快捷键|功能|
|-|-|
|`F8`|打开/关闭 Light Flow 面板|
|`F9`|开始/停止录制|

快捷键可在HTModLoader中修改。

面板中可以设置：

- **Output**：输出路径。留空时自动写入`<模组目录>\captures\light-flow-YYYYMMDD-HHMMSS.mp4`。支持直接填写 `rtmp://`、`rtmps://`、`srt://`、`udp://`、`tcp://` 地址。
- **Codec**：`libx264`（默认）、`h264_mf`、`libopenh264`、`mpeg4`。
- **Bitrate**：视频码率，单位为kbps。标清12000，高清30000。
- **Capture FPS**：抓取帧率上限，`0` 表示每个渲染帧都抓取。30或60即可。
- **Encoder queue**：编码队列长度。编码跟不上时会丢弃新帧，避免游戏卡顿。
- **Force movie capture mode**：通过Hook让游戏进入电影捕获分支，通常必须开启。
- **REC overlay**：在屏幕左上角显示录制状态。
- **Swap R&B**：在编码前交换红/蓝通道。默认关闭；若录出的画面红蓝颠倒时开启。一般仅在国际服需要开启。

## 构建

用Visual Studio 2022打开`project/light-flow.sln`，选择`Release | x64`构建。构建产物在`dist/`中。`dist` 目录整体作为模组安装到 HTModLoader 的 mods 目录即可。FFmpeg的动态库必须和`light-flow.dll`放在同一目录（构建脚本会自动复制）。

## 二进制描述符

`Data/Manifests/BinaryDescriptor.json`是MetaBinary/BinaryDescriptor的运行时描述文件。顶层按游戏版本分为`netease`与`tgc`，每个版本内部再按架构（`x64`）列出描述符：

```json
{
  "netease": { "x64": { "Compositor::Render$0": { ... }, ... } },
  "tgc":     { "x64": { "Compositor::Render$0": { ... }, ... } }
}
```

`BinaryDescriptorBarn::Initialize(json, profile)`会优先读取`root[profile][arch]`；若文件里没有对应版本（或传入空profile），则退回旧的`root[arch]`布局。每一项对应`src/skylink`里的一个反射式声明：

- `sig`：**特征码数组**。每个元素是 `{ "pattern", "mask", "match" }`，按从上到下的顺序扫描；第一个既能命中、其 `match` 又能成功求值的元素被采用，其余忽略。也可以使用单个对象包含下列元素。
  - `pattern` / `mask`：字节序列与可选的位掩码（`??` 表示通配字节）；
  - `match`：地址/槽位表达式，语法见`src/metabinary/binaryDescPatternMatch.hpp`。对普通函数，结果就是函数地址（默认`$@`，即命中位置）；对虚函数，结果被解释为**vtable 槽位索引**，例如命中`FF 90 ?? ?? ?? ??`（`call [rax+disp32]`）时可用`$0:0 / 8` 从位移中取出槽位。
- `rva`：所有签名都失败时的模块内偏移，作为普通函数的兜底；
- `slot`：所有签名都失败时的vtable槽位，作为虚函数的兜底。

若模组无法正常运行，用户可以自行根据ida更新二进制描述符中的特征码，同时也可以在本仓库发布issue或PR。

## 其他相关项目
- [拾影(hSC plugin)](https://github.com/HTMonkeyG/hSC)：摄像机修改与航拍插件。
- [flame.cc](https://github.com/that-sky-project/flame.cc)：通用型光遇ModAPI。本模组使用的二进制描述符声明方式源自该项目。
- [that-sky-infdev](https://github.com/that-sky-project/that-sky-infdev)：光遇无限世界生成测试mod。本模组使用的渲染器函数及常量声明源自该项目。

## 免责声明
- 不当地调整本模组的设置与参数可能严重降低游戏性能、导致游戏崩溃。项目组不会接受或修复明确因用户错误输入导致的问题。

- 本模组使用了dll注入、inline hook等技术对客户端进行修改以实现功能，可能违反游戏服务条款，并导致账号被限制或封禁。请在使用本模组前斟酌风险。

- 《光·遇》、《Sky光·遇》、《Sky: Children of the Light》的一切合法权益（包括但不限于使用本软件产生的游戏内图片、音视频）归属于[网易](https://sky.163.com/)和/或[ThatGameCompany](https://www.thatskygame.com/)，且适用于其对应用户条约与服务协议。

- 本模组软件和相关文档文件不是网易及ThatGameCompany的官方产品，未经上述二者批准且与上述二者无关。

- FFmpeg 7.1.1的许可证文件[见此](https://github.com/that-sky-project/light-flow/blob/main/libraries/ffmpeg/LICENSE)。
