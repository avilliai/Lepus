# Lepus 、 - NTQQ 原生独立逆向协议框架

Lepus 是一个针对桌面端 NTQQ 深度自研的现代化协议框架。采用纯 C++20 打造独立二进制架构，提供极速响应、极低内存占用，既全面兼容 **OneBot v11** 标准协议，又内置原生面向大语言模型（LLM / Agent）的现代化智能体协议。

---

## 架构说明与业界参考关系 (References & Credits)

Lepus 在协议分析与逆向探索阶段参考了开源社区优秀的逆向项目，并在此基础之上进行了完全独立的原生 C++20 架构重构与自研：

1. **[LuckyLilliaBot (LLOB)](https://github.com/LLOneBot/LuckyLilliaBot)**:
   - **参考内容**: NTQQ Electron 层与 Node Native Addon 桥接流程、早期 OneBot 接口标准映射逻辑。
2. **[SnowLuma](https://snowluma.github.io/)**:
   - **参考内容**: NTQQ 现代 Protobuf 结构设计、NTV2RichMedia（富媒体预上传 OIDB 0x11c4/0x11c5、Highway 传输头以及 commonElem 结构）。
3. **[PMHQ](https://github.com/pure-mojo-hook-qq)**:
   - **参考内容**: Mojo IPC 机制与底层控制管道架构概念。

### 核心差异与技术优势

| 维度 | LuckyLilliaBot (LLOB) | SnowLuma | Lepus (本项目) |
| :--- | :--- | :--- | :--- |
| **开发语言与运行时** | TypeScript + Node.js + Electron 前端注入 | TypeScript / Node.js 运行时依赖 | **纯 C++20 原生二进制**，零外部运行时依赖 |
| **资源消耗** | 占用较高（伴随 Electron / Chromium 渲染上下文） | 中等（需外部 Node/Bun 桥接服务） | **极低**（单 DLL 内存占用 < 15MB，微秒级封包延迟） |
| **检测面与安全性** | 易被检测点：修改渲染进程 JS、DOM/UI 注入、Electron 模块劫持 | 易被检测点：外部 Node 桥接管道行为、通用 Hook 特征 | **独立内存注入**，无需篡改 QQ 前端资源包，无额外 Node/V8 运行时特征 |
| **风控与签名验证** | 依赖 NT 客户端自身上下文 | 依赖协议层 RPC 回包 | **完全复用 NTQQ 进程内自身通道**，无缝继承客户端登录态与签名票据 |
| **协议支持** | OneBot v11 | 内部自定义 / OneBot 转换 | **OneBot v11 (HTTP+WS) + 现代 Agent 协议 (原生 Tool Calling / 思考链)** |

---

## 防封与检测点对比说明

腾讯 NTQQ 客户端当前主要的安全审计机制包括：
1. **文件完整性与资源包哈希校验**：针对 resources\app 下的前端 JS 文件篡改（如 LiteLoaderQQNT 类框架被针对性扫描）。
   - **Lepus 对策**：完全不修改 NTQQ 安装目录下的任何静态文件和 JS 资源，100% 保持官方文件签名完整。
2. **Node.js/Electron 全局环境检测**：通过原型链、全局变量探测或重命名敏感对象。
   - **Lepus 对策**：Lepus 运行在 C++ Native 底层空间，不介入 V8 引擎内部，完全规避 JS 层的探针。
3. **SSO 签名验证（Dverify / Sign Server）**：针对发包时签名不匹配引发的批量封号。
   - **Lepus 对策**：Lepus 复用 NTQQ 内部自身内核通道构建 MessageSvc.PbSendMsg 与 OidbSvcTrpcTcp，发包直接携带腾讯客户端实时生成的合法凭证，无需第三方签名服务。

---

## 项目结构与 Git 协作推送范围

推送至公开 GitHub 仓库时，遵循以下规范（已有完整 .gitignore 自动过滤）：

### 必须推送的核心源码（用于开源协作）：
- include/：Lepus 核心 C++ 头文件（协议层、Hook、网络服务器、Protobuf 解析器等）
- src/：入口代码（dllmain.cpp、launcher_main.cpp）
- Makefile：MinGW-w64 一键编译脚本
- config/：配置文件模板目录（config/config.json）
- docs/：项目架构与设计文档
- 	hird_party/httplib/、	hird_party/nlohmann/、	hird_party/minhook/：开源依赖源码库
- README.md、.gitignore

### 不应推送到公开仓库的内容（已加入 .gitignore）：
- in/（构建生成的可执行文件、DLL、日志）
- *.log（运行日志，包含私人敏感信息）
- 含有版权纠纷风险的闭源或第三方二进制文件

---

## 跨设备使用指南（免编译/纯使用环境）

如果你想在另一台电脑上直接使用 Lepus（无需安装 MinGW 编译器和开发环境）：

### 目录结构推荐：
保持与开发目录一致或解压即用目录：
`	ext
📁 Lepus/
├── 📁 config/
│   └── 📄 config.json           (端口与反向 WS 配置文件)
├── 📁 bin/
│   ├── 📄 lepus_launcher.exe    (启动引导器)
│   └── 📄 lepus_core.dll        (核心协议引擎 DLL)
└── 📁 third_party/
    └── 📄 lepus_hook.dll        (底层 Hook 驱动模块)
`
> 注：lepus_launcher.exe 会优先寻找 lepus_core.dll 以及同级或 ../third_party/lepus_hook.dll。若平铺放在同一个文件夹中，引导器也能够自动识别。

**运行环境依赖**：
- 目标电脑已安装并登录官方 NTQQ 64位版本。
- 系统已安装 Microsoft Visual C++ 2015-2022 Redistributable (x64) 运行库。

---

## 快速使用说明

### 1. 启动官方 NTQQ
正常打开并登录官方 QQ 客户端（确保使用的是 64 位 NTQQ）。

### 2. 管理员权限启动注入器
`powershell
.\bin\lepus_launcher.exe
`
注入成功后将看到以下服务开启：
- **OneBot v11 HTTP 接口**：http://127.0.0.1:3000
- **OneBot v11 正向 WebSocket**：ws://127.0.0.1:3001

### 3. 连接与发消息测试
使用任何 OneBot v11 机器人框架（如 NoneBot2、Koishi、AstrBot、Eridanus 等）连接 ws://127.0.0.1:3001 即可正常收发私聊、群聊消息、表情及富媒体图片。
