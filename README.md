# ChatHolmes SDK

一款基于 C++17 实现的大模型接入 SDK，提供统一的接口访问多种 AI 模型。

## 功能特性

- ✅ **多模型支持**：DeepSeek、GPT、Gemini、Ollama 本地模型
- ✅ **消息模式**：支持全量消息和流式消息（SSE）
- ✅ **会话管理**：会话创建、查询、删除、历史消息管理
- ✅ **数据持久化**：基于 SQLite 的会话存储

## 快速开始

### 环境要求

- CMake 3.10+
- C++17 编译器
- OpenSSL 开发库
- SQLite3 开发库

### 获取源码

```bash
git clone https://gitee.com/zhibite-edu/ai-model-acess-dev.git
cd ai-model-acess-dev
```

### 项目结构

```
chat_holmes/
├── sdk/                    # SDK 源码目录
│   ├── include/            # 头文件
│   │   ├── ChatSDK.h       # 核心 SDK 接口
│   │   ├── LLMManager.h    # 模型管理器
│   │   ├── SessionManager.h # 会话管理器
│   │   ├── DataManager.h   # 数据持久化管理器
│   │   ├── LLMProvider.h   # LLM 提供者接口
│   │   ├── DeepSeekProvider.h  # DeepSeek 实现
│   │   ├── OllamaLLMProvider.h # Ollama 实现
│   │   ├── common.h        # 公共数据结构
│   │   └── util/           # 工具类
│   │       └── myLog.h     # 日志工具
│   ├── src/                # 源文件
│   │   ├── ChatSDK.cpp
│   │   ├── LLMManager.cpp
│   │   ├── SessionManager.cpp
│   │   ├── DataManager.cpp
│   │   ├── DeepSeekProvider.cpp
│   │   ├── OllamaLLMProvider.cpp
│   │   └── util/myLog.cpp
│   └── CMakeLists.txt      # SDK 编译配置
├── test/                   # 测试目录
│   ├── testLLM.cpp         # LLM 测试用例
│   ├── testSQLite/         # SQLite 测试
│   └── CMakeLists.txt      # 测试编译配置
├── .clangd                 # clangd 配置
└── README.md               # 项目说明
```

### 编译安装

```bash
# 进入 SDK 目录
cd sdk

# 创建构建目录
mkdir build && cd build

# 生成 Makefile
cmake ..

# 编译并安装
make
sudo make install
```

**安装位置**：
- 静态库：`/usr/local/lib/libsdk_holmes.a`
- 头文件：`/usr/local/include/ai_chat_sdk/`

## API 接口

### ChatSDK 类

| 方法 | 功能 |
|------|------|
| `initModels(configs)` | 初始化模型配置 |
| `createSession(modelName)` | 创建会话 |
| `getSession(sessionId)` | 获取会话信息 |
| `getSessionList()` | 获取所有会话列表 |
| `deleteSession(sessionId)` | 删除会话 |
| `sendMessage(sessionId, message)` | 发送全量消息 |
| `sendMessageStream(sessionId, message, callback)` | 发送流式消息 |
| `getAvailableModels()` | 获取可用模型列表 |

### 配置类

**APIConfig**（适用于 DeepSeek、GPT、Gemini）：
```cpp
struct APIConfig {
    std::string api_key;    // API 密钥
    std::string model_name; // 模型名称
    double temperature;     // 温度参数
    int max_tokens;         // 最大令牌数
};
```

**OllamaConfig**（适用于 Ollama 本地模型）：
```cpp
struct OllamaConfig {
    std::string model_name; // 模型名称
    std::string host;       // Ollama 服务地址
    int port;               // 端口号
};
```

## 使用示例

```cpp
#include <ai_chat_sdk/ChatSDK.h>
#include <ai_chat_sdk/util/myLog.h>

int main() {
    // 初始化日志
    bite::Logger::init_logger("chat_holmes", "stdout", spdlog::level::info);
    
    // 创建 SDK 实例
    sdk_holmes::ChatSDK chatSDK;
    
    // 配置 DeepSeek 模型
    sdk_holmes::APIConfig deepseekConfig;
    deepseekConfig.api_key = std::getenv("DEEPSEEK_API_KEY");
    deepseekConfig.model_name = "deepseek-chat";
    deepseekConfig.temperature = 0.7;
    
    // 初始化模型
    std::vector<std::shared_ptr<sdk_holmes::Config>> configs;
    configs.push_back(std::make_shared<sdk_holmes::APIConfig>(deepseekConfig));
    chatSDK.initModels(configs);
    
    // 创建会话
    std::string sessionId = chatSDK.createSession("deepseek-chat");
    
    // 发送流式消息
    chatSDK.sendMessageStream(sessionId, "Hello!", 
        [](const std::string& response, bool done) {
            std::cout << response;
            if (done) std::cout << std::endl;
        });
    
    return 0;
}
```

### CMakeLists.txt 配置

```cmake
project(MyChatApp)
set(CMAKE_CXX_STANDARD 17)

add_executable(MyChatApp main.cpp)

# 链接 SDK 和依赖库
target_link_libraries(MyChatApp PRIVATE
    sdk_holmes
    jsoncpp
    fmt
    spdlog
    sqlite3
    OpenSSL::SSL
    OpenSSL::Crypto
)
```

## 测试运行

```bash
# 编译测试
cd test
mkdir build && cd build
cmake ..
make

# 运行测试
./testLLM
```

## 支持的模型

| 模型类型 | 模型名称 | 配置类 |
|----------|----------|--------|
| DeepSeek | deepseek-chat | APIConfig |
| Ollama | 任意 Ollama 模型 | OllamaConfig |

## 依赖库

- **cpp-httplib**: HTTP 客户端库
- **nlohmann/json**: JSON 解析库
- **fmt**: 格式化库
- **spdlog**: 日志库
- **sqlite3**: 数据库
- **OpenSSL**: 加密库

## 许可证

MIT License