# ddnet-background-module

把「自定义背景（图片 / 视频动态背景）」做成**可插拔的源码模块**：给一份干净的 TClient 源码打一个补丁即得到完整功能，不打补丁就是原版客户端。

> **它不是运行时插件（不是 DLL）**：DDNet / TClient 没有插件 ABI，而这个功能需要**引擎级渲染支持**（新的纹理更新命令、`NO_MIPMAPS`、重复寻址）和菜单渲染钩子，所以模块形式只能是 **源码补丁 + 一键安装脚本**。想做成"丢一个 dll 进 data/ 就生效"在本项目上不可能，这里不做没有依据的承诺。

## 一键安装（两种）

| 方式 | 做法 | 适合 |
|---|---|---|
| **A. 免编译直装包** | 解压 → 双击 `DDNet.exe` | 只想玩（`ddnet-background-module-prebuilt-win64.zip`，Windows x64，约 108MB，内含 exe + 全部 DLL + data + FFmpeg 8.1） |
| **B. 一条命令全自动** | `powershell -ExecutionPolicy Bypass -File install.ps1` | 想自己编译（自动完成：拉源码@基线提交 → 子模块 → 打补丁 → 装 FFmpeg 8.1 → cmake 构建 → 组装 `dist\` 可直接双击的目录）。**镜像默认就开**（`ghproxy.net`），可换 `-Mirror hk.gh-proxy.org`、关掉用 `-MirrorOff`；本机有代理就加 `-Proxy 127.0.0.1:7890`（代理同时作用于 git 与 FFmpeg 下载，也会带给子模块那 580MB） |

```powershell
# B 的常用变体
install.ps1 -WorkDir D:\src\myclient                        # 指定目录（镜像默认开）
install.ps1 -Mirror hk.gh-proxy.org                         # 换一个镜像
install.ps1 -MirrorOff                                      # 直连 GitHub（不用镜像）
install.ps1 -Proxy 127.0.0.1:7890                           # 走本机 HTTP 代理
install.ps1 -Source C:\src\TClient                          # 直接给已有源码树打补丁（不克隆）
install.ps1 -SkipBuild                                      # 只装补丁与 FFmpeg，稍后自己编译
install.ps1 -SkipFfmpeg -FfmpegZip C:\dl\ffmpeg.zip
```

[English below ↓](#english)

## 支持的基线（官方版 / TClient / 其它分支都能用）

| 基线 | 状态 | 说明 |
|---|---|---|
| **TClient 10.9.0**（`6b4118bf0`） | ✅ **完整验证** | 补丁干净套用 + 内容 25/25 一致 + 编译通过 + 实机逐项验证。 |
| **DDNet 官方版** | ⚠️ **已量化差异，适配草案** | 实测与本模块基线几乎同源（`background.{h,cpp}`、`FindFFMPEG.cmake` 完全相同，引擎 4 个文件仅差 2~6 行）。**尚未编译验证**（需要 ddnet-libs 约 580MB 依赖 + 一次完整构建） |
| **其它第三方分支** | ⚠️ **通用做法** | 耦合面只有 6 处，用 `-Reject` 半自动套用 + 按锚点手工贴 |

**为什么不做成"一个补丁通吃"**：各分支的 `CMakeLists.txt`（源文件列表）与设置页枚举结构差异最大（官方 vs TClient 的 `CMakeLists.txt` 就差了 263 行），硬套只会满屏冲突；而**引擎接口与背景组件部分几乎逐行同源**，所以补丁覆盖大部分、剩余按补丁里每个 hunk 的函数上下文锚点手工落位最稳。

```powershell
apply.ps1 -Target C:\path\to\fork -CheckOnly   # 先看能不能干净套上（不改文件）
apply.ps1 -Target C:\path\to\fork -Reject      # 能套的套上，套不上的留 .rej（附清单）
apply.ps1 -Target C:\path\to\fork -Revert      # 卸载
```

安装器会自动识别基线（有没有 `src/engine/shared/config_variables_tclient.h`）并给出提示。

## 适用基线

| 项 | 值 |
|---|---|
| 上游 | **TClient 10.9.0**（`TaterClient/TClient`），其上游为 DDNet |
| 基线提交 | `6b4118bf0` |
| 改动规模 | 25 个文件，+2912 / −22 行 |
| 改动性质 | 纯本地视觉：不改网络协议、预测、碰撞、tick，**不影响平衡** |

> **已验证**：把基线提交的干净树取出后套上本补丁，25/25 个文件与开发树内容完全一致（`git archive` 取树 + `git apply` + 归一化换行后逐文件比对）。
> 注意 Windows 上 `git apply` 受 `core.autocrlf` 影响会把结果写成 CRLF，这是正常现象；想保持 LF 就加 `git -c core.autocrlf=false apply`。

## 安装（手工，不用安装器时）

**Windows**

```powershell
git clone https://github.com/TaterClient/TClient myclient
cd myclient
git submodule update --init --recursive
powershell -ExecutionPolicy Bypass -File ..\ddnet-background-module\apply.ps1 -Target .
```

**Linux / macOS**

```sh
git clone https://github.com/TaterClient/TClient myclient
cd myclient && git submodule update --init --recursive
../ddnet-background-module/apply.sh .
```

只想先试试能不能套上：`apply.ps1 -CheckOnly` / `apply.sh . --check`。

## 卸载

```sh
git apply -R patch/ddnet-background.patch        # 或
git checkout -- . && git clean -fd src           # 彻底回到基线
```

## 视频能力的前置条件（重要，别跳过）

`ddnet-libs` 子模块自带的 FFmpeg 是**录制专用构建（没有 demuxer）**，不换它**只能显示图片背景，打不开 mp4**。
需要换成 BtbN 的 **FFmpeg 8.1 shared** 全量 DLL：`avcodec-62`、`avformat-62`、`avutil-60`、`swresample-6`、`swscale-9`（Windows 放 exe 同目录，其它平台对应动态库），`cmake/FindFFMPEG.cmake` 在补丁里已经改成这些名字。换完记得重新配置并**重编引擎**。

## 模块内容

```
ddnet-background-module/
├─ patch/ddnet-background.patch   25 个文件的完整改动（git apply 一键套用）
├─ files/                         5 个新增源文件（手工安装 / 换用其它版本控制时用）
│   ├─ custom_background.{h,cpp}          背景组件（解码→上传→渲染，含 4 种显示方式）
│   ├─ wallpaper_engine.{h,cpp}           Wallpaper Engine 壁纸扫描与解析
│   └─ menus_settings_mycustom.cpp        Background 设置页
├─ apply.ps1 / apply.sh           安装器（支持 -CheckOnly / --revert）
└─ module.json                    机器可读清单（基线、文件、配置项、引擎 API）
```

## 配置项

| 变量 | 作用 |
|---|---|
| `mc_menu_background` / `mc_game_background` | 主菜单 / 游戏内实体层背景开关 |
| `mc_background_path` | 背景文件路径（相对存档目录；留空 = 原版背景） |
| `mc_background_source` | 来源：0 = 自己的文件，1 = Wallpaper Engine |
| `mc_background_video_fps` | 帧率上限（0 = 跟随片源） |
| `mc_background_video_res` | 解码分辨率上限（0 = 原始片源，不压缩） |
| `mc_background_fit` | 背景适配（只决定画面比例） |
| `mc_background_mode` | 显示方式：上下裁剪 / 左右裁剪 / 拉伸 / 平铺（不留黑边） |
| `mc_background_threads` | 解码线程数（默认 4） |
| `mc_background_diag` | 诊断落盘开关（默认关） |
| `mc_we_path` | 所选 Wallpaper Engine 壁纸 |
| `mc_ui_panel_alpha` | 菜单黑底/阴影不透明度，五档（完全透明 / 朦胧 / 淡藏 / 微淡 / 正常） |
| `mc_menu_title` | 主菜单大标题显隐 |

背景文件放在**存档目录下的 `Background` 文件夹**（Windows：`%APPDATA%\DDNet\Background`）。

## 引擎侧新增接口

- `IGraphics::UpdateTextureRgba()` + `CCommandBuffer::CMD_TEXTURE_UPDATE`（纹理不再每帧销毁重建）
- `IGraphics::TEXLOAD_NO_MIPMAPS`、`IGraphics::WrapRepeat()`
- `ui_page` 取值范围放宽到 1..17（原本 14/15 被 clamp，进不去设置页）
- 纹理上行分带提交（命令环上限 `CMD_BUFFER_DATA_BUFFER_SIZE = 2MB`）

## 移植到更新的上游

上游一旦更新，补丁可能不再干净地套上。此时按补丁里每个 hunk 的上下文逐点重贴即可；文件级改动都在 `files/` 里有完整副本。

---

# English

This module turns the custom background feature (image / video dynamic background) into a **source module**: apply one patch to a clean TClient tree and you get the whole feature; skip it and you have the stock client.

> **It is not a runtime plugin and not a DLL.** DDNet/TClient have no plugin ABI, and the feature needs engine-level rendering support (a texture update command, `NO_MIPMAPS`, repeat wrapping) plus menu render hooks. A source patch with an installer is therefore the only honest packaging; "drop a DLL into data/ and it works" is not possible here.

## Baseline

| | |
|---|---|
| Upstream | **TClient 10.9.0** (`TaterClient/TClient`), itself based on DDNet |
| Base commit | `6b4118bf0` |
| Size | 25 files, +2912 / −22 lines |
| Scope | local and visual only: no changes to network protocol, prediction, collisions or ticks, so **balance is untouched** |

## Install

```powershell
# Windows
git clone https://github.com/TaterClient/TClient myclient
cd myclient
git submodule update --init --recursive
powershell -ExecutionPolicy Bypass -File ..\ddnet-background-module\apply.ps1 -Target .
```

```sh
# Linux / macOS
git clone https://github.com/TaterClient/TClient myclient
cd myclient && git submodule update --init --recursive
../ddnet-background-module/apply.sh .
```

Dry run: `apply.ps1 -CheckOnly` or `apply.sh . --check`.

## Uninstall

```sh
git apply -R patch/ddnet-background.patch
# or completely: git checkout -- . && git clean -fd src
```

## Prerequisite for video (do not skip)

The FFmpeg shipped inside the `ddnet-libs` submodule is a **recorder-only build without demuxers**: without replacing it, **images work but video files cannot be opened**. Use the BtbN **FFmpeg 8.1 shared** build (`avcodec-62`, `avformat-62`, `avutil-60`, `swresample-6`, `swscale-9`); `cmake/FindFFMPEG.cmake` is already patched to those names. Reconfigure and rebuild the engine afterwards.

## Contents

```
patch/ddnet-background.patch   full change set (25 files, apply with git apply)
files/                         the 5 added source files (manual installation / other VCS)
apply.ps1 / apply.sh           installers (-CheckOnly / --revert supported)
module.json                    machine-readable manifest
```

Configuration variables are listed in the Chinese section above; background files live in the **`Background` folder inside the save directory** (Windows: `%APPDATA%\DDNet\Background`).
