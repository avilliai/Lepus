# Lepus 架构与设计规范

## 1. 项目概况
Lepus 是针对腾讯 NTQQ (桌面端/Headless) 的高性能、高稳定性 C++20 现代逆向协议框架。
提供原生 **In-Process Native Hook / 内存注入** 核心，100% 官方路径通过所有签名验证点，向下完全兼容 **OneBot v11**，向上独创面向大语言模型智能体设计的 **Lepus Modern Agent Protocol**。

## 2. 核心架构设计

```
                            ┌──────────────────────────────────────────────┐
                            │                 NTQQ Process                 │
                            │  (wrapper.node / kernel.dll / Node.js Engine)│
                            └──────────────────────┬───────────────────────┘
                                                   │ Memory In-Process Hook
                                                   ▼
┌───────────────────────────────────────────────────────────────────────────────────────────────────┐
│                                       Lepus C++20 Core                                            │
├──────────────────────────────────────────┬────────────────────────────────────────────────────────┤
│             NTQQ Native Bridge           │                   Hook Manager (MinHook)               │
│ • Dispatcher Hook (send/recv message)    │ • Memory Scanner & Pattern Matcher                     │
│ • FeKit / SecData 签名自动继承 (100% 通过)  │ • Inline Hook & VTable Hook Trampoline                 │
│ • SSO Packet Proxy (纯官方凭证加签)       │ • Thread-Safe Hook LifeCycle                           │
├──────────────────────────────────────────┴────────────────────────────────────────────────────────┤
│                                  Lepus Modern Business Core                                       │
│ • ConversationMemory: 结构化多轮会话上下文树、语义 Token 计数与滚动窗口截断                          │
│ • AgentToolRegistry: 原生函数调用 (Tool / Function Calling) 注册表与自动执行引擎                      │
├──────────────────────────────────────────┬────────────────────────────────────────────────────────┤
│            OneBot v11 Adapter            │                  Lepus Agent Protocol                  │
│ • CQ Code 语法解析器 & 转义防护           │ • 强类型 AST 消息节点 (Text, Mention, Image, Audio)   │
│ • OneBot v11 HTTP API 路由与事件上报      │ • Tool Call / Tool Result 节点协议                     │
│ • WebSocket 消息广播与响应分发           │ • 动态流式下发 (Streaming Tokens / CoT 思考链分片)       │
└──────────────────────────────────────────┴────────────────────────────────────────────────────────┘
                                                   │ High-Performance HTTP / WS Gateway
                                                   ▼
                                       Upstream LLM Agent / User
```

## 3. 为什么能 100% 稳定通过所有签名验证点？
1. **脱离签名伪造风险**：纯协议模拟项目（如 Lagrange 等）需依赖外部 Sign 签名服务，一旦官方更新算法或做设备与内存完整性关联，便导致 235/237 异常与封控；
2. **全流程原生执行**：Lepus 注入到 NTQQ 进程内存中，直接挂钩 `wrapper.node` 的 Dispatcher 发包函数与内核 `IKernelSession`。所有的网络请求、ECDH 握手、FeKit 动态设备信息加签，全部直接调用腾讯内部未导出的官方二进制代码执行；
3. **服务端完全识别为合法客户端**：在腾讯服务器看来，这是真正的客户端本地产生的合法会话。

## 4. Lepus 现代 Agent 协议与 OneBot v11 对比

| 特性 | OneBot v11 | Lepus Agent Protocol |
| :--- | :--- | :--- |
| **消息结构** | 易产生注入漏洞的 CQ 码纯文本 / 弱类型列表 | 强类型 AST 树状节点（`AgentNode`） |
| **智能体适配** | 仅支持发送普通文本与多媒体 | 原生包含 `thought` (思维链)、`tool_call`、`tool_result` 节点 |
| **上下文管理** | 客户端自行记录，极易丢失与超限 | 服务端内核内置多轮上下文窗口与 Token 压缩截断 |
| **工具调用** | 无统一规范，需上层繁琐调度 | 提供标准 OpenAI 格式的 Tool Manifest 导出与即时回调 |
| **高并发性能** | 依赖外部运行时（Node/Python/Java），内存大 | C++20 单二进制，极低内存消耗 (<25MB)，毫秒响应 |

## 5. API 规范速查

### OneBot v11 兼容接口：
- `POST /send_private_msg`: `{"user_id": 123456, "message": "hello"}`
- `POST /send_group_msg`: `{"group_id": 987654, "message": "hello group"}`
- `POST /get_status`: 获取当前机器人连接状态

### Lepus 现代 Agent 接口：
- `POST /api/v1/agent/send`: 发送强类型多节点富文本消息（支持思考链与混合内容）
- `GET  /api/v1/agent/tools`: 获取当前注册的智能工具清单 (OpenAI Function Calling Schema)
- `POST /api/v1/agent/tools/call`: 直接请求执行工具回调并获取结构化结果
- `GET  /api/v1/agent/context?user_id=xxx`: 获取用户指定会话的历史上下文，便于直接喂入 LLM
