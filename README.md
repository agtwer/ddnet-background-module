# ddnet-background-module

这是一个**基于 DDNet 的背景模块**：把「自定义背景（图片 / 视频动态背景）」做成可插拔的源码模块——给一份干净的 DDNet 官方 20.1 源码打一个补丁即得到完整功能，不打补丁就是原版客户端。

> 本模块同时也是一份**模块开发样例**（多基线补丁映射、新增源文件、FFmpeg 依赖）：字段与打包规则见 [模块开发文档](https://github.com/agtwer/ddnet-module-installer/blob/main/MODULE-DEV.md)。

## 安装方法

| 方式 | 做法 | 获取来源 |
|---|---|---|
| **A. 程序一键部署** | 用安装器选模块后一键完成：拉源码 → 打补丁 → 装 FFmpeg 8.1 → 编译 → 组装出可直接双击的客户端目录 | **ddnet-module-installer**：<https://github.com/agtwer/ddnet-module-installer>（把 `dist/background-1.2.dmod` 放进安装器的 `mods\`，或直接拖进窗口） |
| **B. 一条命令全自动** | `powershell -ExecutionPolicy Bypass -File install.ps1`（自动完成：拉源码@基线提交 → 子模块 → 打补丁 → 装 FFmpeg 8.1 → cmake 构建 → 组装 `dist\` 可直接双击的目录）。**镜像默认就开**，可换一个用 `-Mirror <host>`、关掉用 `-MirrorOff`；本机有代理就加 `-Proxy 127.0.0.1:7890`（代理同时作用于 git 与 FFmpeg 下载，也会带给子模块那 580MB） | git clone 本页：`git clone https://github.com/agtwer/ddnet-background-module` |

```powershell
# B 的常用变体
install.ps1 -WorkDir D:\src\myclient                        # 指定目录（镜像默认开）
install.ps1 -Mirror <host>                                  # 换一个镜像域名
install.ps1 -MirrorOff                                      # 直连 GitHub（不用镜像）
install.ps1 -Proxy 127.0.0.1:7890                           # 走本机 HTTP 代理
install.ps1 -Source C:\src\ddnet-20.1                       # 直接给已有源码树打补丁（不克隆）
install.ps1 -SkipBuild                                      # 只装补丁与 FFmpeg，稍后自己编译
install.ps1 -SkipFfmpeg -FfmpegZip C:\dl\ffmpeg.zip
```

[English below ↓](#english)

## 支持的基线

| 基线 | 状态 | 说明 |
|---|---|---|
| **DDNet 官方版 20.1** | ✅ **完整验证** | 补丁 `patch/ddnet-20.1.patch`（容器内同名）：干净套用 + 编译 0 错误 + 实机验证（选项背景深浅五档、隐藏 GUI、4K 视频按屏幕分辨率解码） |
| **其它 DDNet 版本 / 分支** | ⚠️ **通用做法** | 耦合面只有 6 处，用 `-Reject` 半自动套用 + 按锚点手工贴（本模块只对官方 20.1 提供适配补丁） |

**多基线补丁机制**：`module.json` 里用 `patches` 映射按「来源@版本」带对应补丁（本模块为 `{"ddnet@20.1":"patch/ddnet-20.1.patch"}`），安装器按当前选择自动取那份，日志会写明「使用 ddnet 20.1 专用补丁」。

```powershell
apply.ps1 -Target C:\path\to\ddnet-20.1 -CheckOnly   # 先看能不能干净套上（不改文件）
apply.ps1 -Target C:\path\to\ddnet-20.1 -Reject      # 能套的套上，套不上的留 .rej（附清单）
apply.ps1 -Target C:\path\to\ddnet-20.1 -Revert      # 卸载
```

## 适用基线

| 项 | 值 |
|---|---|
| 上游 | **ddnet/ddnet 官方版 20.1** |
| 基线提交 | `20.1`（tag） |
| 改动规模 | 17 个文件，+2572 / −7 行 |

> **已验证**：把 20.1 的干净树取出后套上本补丁，`git apply --check` 通过、编译 0 错误并实机运行验证。
> 注意 Windows 上 `git apply` 受 `core.autocrlf` 影响会把结果写成 CRLF，这是正常现象；想保持 LF 就加 `git -c core.autocrlf=false apply`。

## 安装

**Windows**

```powershell
git clone --branch 20.1 https://github.com/ddnet/ddnet myclient
cd myclient
git submodule update --init --recursive
powershell -ExecutionPolicy Bypass -File ..\ddnet-background-module\apply.ps1 -Target .
```

**Linux / macOS**

```sh
git clone --branch 20.1 https://github.com/ddnet/ddnet myclient
cd myclient && git submodule update --init --recursive
../ddnet-background-module/apply.sh .
```

只想先试试能不能套上：`apply.ps1 -CheckOnly` / `apply.sh . --check`。

## 卸载

```sh
git apply -R patch/ddnet-20.1.patch              # 或
git checkout -- . && git clean -fd src           # 彻底回到基线
```

## 视频能力的前置条件

`ddnet-libs` 子模块自带的 FFmpeg 是**录制专用构建（没有 demuxer）**，不换它**只能显示图片背景，打不开 mp4**。
需要换成 BtbN 的 **FFmpeg 8.1 shared** 全量 DLL：`avcodec-62`、`avformat-62`、`avutil-60`、`swresample-6`、`swscale-9`（Windows 放 exe 同目录，其它平台对应动态库），`cmake/FindFFMPEG.cmake` 在补丁里已经改成这些名字。换完记得重新配置并**重编引擎**。

## 模块内容

```
ddnet-background-module/
├─ patch/
│   └─ ddnet-20.1.patch            DDNet 官方 20.1 的完整改动（17 个文件）
├─ files/                         5 个新增源文件（手工安装 / 换用其它版本控制时用）
│   ├─ custom_background.{h,cpp}          背景组件（解码→上传→渲染，含 4 种显示方式）
│   ├─ wallpaper_engine.{h,cpp}           Wallpaper Engine 壁纸扫描与解析
│   └─ menus_settings_mycustom.cpp        Background 设置页
├─ apply.ps1 / apply.sh           安装器（支持 -CheckOnly / --revert）
├─ install.ps1                    一条命令全自动（拉源码→打补丁→装 FFmpeg→编译→组装）
├─ pack-dmod.ps1                  打包出 dist/background-1.2.dmod
└─ module.json                    机器可读清单（基线、补丁映射、配置项、引擎 API）
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

- `IGraphics::UpdateTextureRgba()` + `CCommandBuffer::CMD_TEXTURE_UPDATE`（纹理不再每帧销毁重建），含 OpenGL2 / OpenGL3 后端实现
- `IGraphics::WrapRepeat()`
- `ui_page` 取值范围放宽到 1..17（原本 14/15 被 clamp，进不去设置页）
- 纹理上行分带提交（命令环上限 `CMD_BUFFER_DATA_BUFFER_SIZE = 2MB`）

## 移植到更新的上游

上游一旦更新，补丁可能不再干净地套上。此时按补丁里每个 hunk 的上下文逐点重贴即可；文件级改动都在 `files/` 里有完整副本。

---

# English

This is a **DDNet-based background module**: it turns the custom background feature (image / video dynamic background) into a **source module** — apply one patch to a clean DDNet official 20.1 tree and you get the whole feature; skip it and you have the stock client.

## Baselines

| | |
|---|---|
| Upstream | **DDNet official 20.1** (`ddnet/ddnet`) |
| Size | 17 files, +2572 / −7 lines |

## Install

Two ways, mirroring the Chinese section: **A** drop `dist/background-1.2.dmod` into the [ddnet-module-installer](https://github.com/agtwer/ddnet-module-installer) `mods\` folder (or drag it into the window) and install from there; **B** `git clone` this page and run `install.ps1`.

```powershell
# Windows
git clone --branch 20.1 https://github.com/ddnet/ddnet myclient
cd myclient
git submodule update --init --recursive
powershell -ExecutionPolicy Bypass -File ..\ddnet-background-module\apply.ps1 -Target .
```

```sh
# Linux / macOS
git clone --branch 20.1 https://github.com/ddnet/ddnet myclient
cd myclient && git submodule update --init --recursive
../ddnet-background-module/apply.sh .
```

Dry run: `apply.ps1 -CheckOnly` or `apply.sh . --check`.

## Uninstall

```sh
git apply -R patch/ddnet-20.1.patch
# or completely: git checkout -- . && git clean -fd src
```

## Prerequisite for video

The FFmpeg shipped inside the `ddnet-libs` submodule is a **recorder-only build without demuxers**: without replacing it, **images work but video files cannot be opened**. Use the BtbN **FFmpeg 8.1 shared** build (`avcodec-62`, `avformat-62`, `avutil-60`, `swresample-6`, `swscale-9`); `cmake/FindFFMPEG.cmake` is already patched to those names. Reconfigure and rebuild the engine afterwards.

## Contents

```
patch/ddnet-20.1.patch         full change set (17 files, apply with git apply)
files/                         the 5 added source files (manual installation / other VCS)
apply.ps1 / apply.sh           installers (-CheckOnly / --revert supported)
module.json                    machine-readable manifest
```

Configuration variables are listed in the Chinese section above; background files live in the **`Background` folder inside the save directory** (Windows: `%APPDATA%\DDNet\Background`).
