#include "ChatServer.h"
#include <sdk_holmes/util/myLog.h>

#include <gflags/gflags.h>
#include <spdlog/common.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <unistd.h>

// ============================================================================
// 版本号
// ============================================================================
static const char* kAIChatServerVersion = "1.0.0";

// ============================================================================
// gflags - 服务器配置参数定义 (可由命令行 / 配置文件提供)
// ============================================================================
// 网络配置
DEFINE_string(host, "0.0.0.0", "服务器绑定的IP地址");
DEFINE_int32(port, 8080, "服务器绑定的端口号 (1-65535)");

// 日志配置
DEFINE_string(log_level, "INFO",
              "日志级别: TRACE | DEBUG | INFO | WARN | ERROR | CRITICAL");

// 模型通用参数
DEFINE_double(temperature, 0.7, "模型温度参数, 取值范围 [0.0, 2.0]");
DEFINE_int32(max_tokens, 2048, "模型单次响应的最大 token 数, 必须为正数");

// Ollama 本地模型配置 (来自命令行/配置文件, 不从环境变量获取)
DEFINE_string(ollama_endpoint, "http://localhost:11434",
              "Ollama 服务器地址, 不可为空");
DEFINE_string(ollama_model_name, "qwen3.5:9b",
              "Ollama 接入的模型名称, 不可为空");
DEFINE_string(ollama_model_desc, "Ollama 本地部署的 Qwen3.5 9B 模型",
              "Ollama 接入的模型描述, 不可为空");

// 配置文件路径 (gflags --flagfile 等价机制, 由 gflags 自身解析)
DEFINE_string(config, "ChatServer.conf",
              "ChatServer 配置文件路径, 相对路径相对于可执行文件所在目录");

// 注: deepseek / chatgpt / gemini 等云端模型的 API Key 不通过命令行参数提供,
//     仅从环境变量读取:
//        DEEPSEEK_API_KEY / CHATGPT_API_KEY / GEMINI_API_KEY

// ============================================================================
// 全局退出标志 & 信号处理
// ============================================================================
namespace {
std::unique_ptr<chat_server::ChatServer> g_chatServer;
std::atomic<bool> g_exitFlag{false};

void signalHandler(int sig) {
    (void)sig;
    g_exitFlag.store(true);
}
}  // namespace

// ============================================================================
// 工具函数
// ============================================================================
// 获取可执行文件所在目录
static std::string getExecutableDir(const char* argv0) {
    char buf[4096] = {0};
    ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    std::string path;
    if (n > 0) {
        path.assign(buf, n);
    } else if (argv0 != nullptr) {
        path = argv0;
    }
    auto pos = path.find_last_of('/');
    if (pos == std::string::npos) return std::string(".");
    return path.substr(0, pos);
}

// 从环境变量读取, 不存在时返回空字符串
static std::string getEnvOrEmpty(const char* name) {
    const char* v = std::getenv(name);
    return (v == nullptr) ? std::string() : std::string(v);
}

// 将日志级别字符串映射到 spdlog 等级
static spdlog::level::level_enum parseLogLevel(const std::string& s) {
    std::string up;
    up.reserve(s.size());
    for (char c : s) up.push_back(static_cast<char>(::toupper(c)));
    if (up == "TRACE")                   return spdlog::level::trace;
    if (up == "DEBUG")                   return spdlog::level::debug;
    if (up == "INFO")                    return spdlog::level::info;
    if (up == "WARN" || up == "WARNING") return spdlog::level::warn;
    if (up == "ERROR" || up == "ERR")    return spdlog::level::err;
    if (up == "CRITICAL" || up == "CRIT")return spdlog::level::critical;
    return spdlog::level::info;
}

// 帮助说明
static void printUsage(const char* prog) {
    std::cout
        << "\n"
        << "================================================================\n"
        << "  AIChatServer  v" << kAIChatServerVersion << "\n"
        << "  一个支持多模型 (DeepSeek / ChatGPT / Gemini / Ollama) 的 AI 聊天服务\n"
        << "================================================================\n\n"
        << "用法:\n"
        << "  " << prog << " [选项]\n\n"
        << "通用选项:\n"
        << "  -h, --help                   显示本帮助信息\n"
        << "  -v, --version                显示版本号\n"
        << "  --config=<file>              指定配置文件 (默认: ChatServer.conf)\n\n"
        << "服务器选项:\n"
        << "  --host=<ip>                  绑定 IP, 默认 0.0.0.0\n"
        << "  --port=<port>                绑定端口, 默认 8080\n"
        << "  --log_level=<level>          日志级别 TRACE|DEBUG|INFO|WARN|ERROR|CRITICAL\n\n"
        << "模型选项:\n"
        << "  --temperature=<float>        采样温度, 范围 [0.0, 2.0], 默认 0.7\n"
        << "  --max_tokens=<int>           最大输出 token 数, 默认 2048\n"
        << "  --ollama_endpoint=<url>      Ollama 服务器地址 (必填)\n"
        << "  --ollama_model_name=<str>    Ollama 接入的模型名称 (必填)\n"
        << "  --ollama_model_desc=<str>    Ollama 接入的模型描述 (必填)\n\n"
        << "环境变量 (云端模型 API Key, 至少一个非空):\n"
        << "  DEEPSEEK_API_KEY             DeepSeek 云端 API Key\n"
        << "  CHATGPT_API_KEY              ChatGPT  云端 API Key\n"
        << "  GEMINI_API_KEY               Gemini   云端 API Key\n\n"
        << "使用案例:\n"
        << "  # 1) 使用默认配置文件 ChatServer.conf 启动\n"
        << "  export DEEPSEEK_API_KEY=sk-xxxx\n"
        << "  ./AIChatServer\n\n"
        << "  # 2) 命令行覆盖部分参数 (优先级高于配置文件)\n"
        << "  ./AIChatServer --port=9090 --log_level=DEBUG --temperature=1.0\n\n"
        << "  # 3) 指定 Ollama 参数\n"
        << "  ./AIChatServer --ollama_endpoint=http://127.0.0.1:11434 \\\n"
        << "                 --ollama_model_name=qwen2.5:7b \\\n"
        << "                 --ollama_model_desc='Qwen2.5 7B 本地模型'\n\n"
        << "  # 4) 通过自定义配置文件启动\n"
        << "  ./AIChatServer --config=/etc/aichat/ChatServer.conf\n\n"
        << "  # 5) 查看版本\n"
        << "  ./AIChatServer --version\n\n"
        << "HTTP 接口列表 (Base URL = http://<host>:<port>):\n"
        << "  POST   /api/session                     创建一个新的会话\n"
        << "  GET    /api/sessions                    获取全部会话列表\n"
        << "  GET    /api/models                      获取可用模型列表\n"
        << "  DELETE /api/session/{sessionId}         删除指定会话\n"
        << "  GET    /api/session/{sessionId}/history 获取指定会话的历史消息\n"
        << "  POST   /api/message                     发送消息 (一次性返回完整响应)\n"
        << "  POST   /api/message/async               发送消息 (SSE 流式增量返回)\n\n"
        << "前端静态资源默认从可执行文件同目录的 ./www 加载.\n"
        << std::endl;
}

static void printVersion() {
    std::cout << "AIChatServer version " << kAIChatServerVersion << std::endl;
}

// 在 gflags 解析之前手动拦截 -h / -v / --help / --version
static bool handleShortOptions(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        if (std::strcmp(a, "-h") == 0 || std::strcmp(a, "--help") == 0) {
            printUsage(argv[0]);
            return true;
        }
        if (std::strcmp(a, "-v") == 0 || std::strcmp(a, "--version") == 0) {
            printVersion();
            return true;
        }
    }
    return false;
}

// 安全检查: 任何一项不通过都视为致命错误
static bool validateConfig(const chat_server::ServerConfig& cfg,
                           std::string& err) {
    if (cfg.host.empty()) {
        err = "host 不能为空";
        return false;
    }
    if (cfg.port <= 0 || cfg.port > 65535) {
        err = "port 必须在 [1, 65535] 范围内, 当前值=" + std::to_string(cfg.port);
        return false;
    }
    if (cfg.temperature < 0.0 || cfg.temperature > 2.0) {
        err = "temperature 必须在 [0.0, 2.0] 范围内, 当前值=" +
              std::to_string(cfg.temperature);
        return false;
    }
    if (cfg.maxTokens <= 0) {
        err = "max_tokens 必须为正数, 当前值=" + std::to_string(cfg.maxTokens);
        return false;
    }
    if (cfg.ollamaEndpoint.empty() ||
        cfg.ollamaModelName.empty() ||
        cfg.ollamaModelDesc.empty()) {
        err = "Ollama 配置 (ollama_endpoint / ollama_model_name / "
              "ollama_model_desc) 都不能为空";
        return false;
    }
    // 云端 API Key 至少有一个非空 (从环境变量读取)
    const std::string ds  = getEnvOrEmpty("DEEPSEEK_API_KEY");
    const std::string gpt = getEnvOrEmpty("CHATGPT_API_KEY");
    const std::string gem = getEnvOrEmpty("GEMINI_API_KEY");
    if (ds.empty() && gpt.empty() && gem.empty() &&
        cfg.deepseekAPIKey.empty()) {
        err = "DEEPSEEK_API_KEY / CHATGPT_API_KEY / GEMINI_API_KEY "
              "环境变量至少需要一个非空";
        return false;
    }
    return true;
}

// ============================================================================
// main
// ============================================================================
int main(int argc, char** argv) {
    // 1) 拦截 -h / --help / -v / --version
    if (handleShortOptions(argc, argv)) {
        return 0;
    }

    // 2) gflags 的 usage / version 字符串
    google::SetVersionString(kAIChatServerVersion);
    google::SetUsageMessage(
        "AIChatServer - 多模型 AI 聊天服务\n"
        "用法: AIChatServer [--host=...] [--port=...] [--config=...] ...\n"
        "完整帮助请使用 -h 或 --help");

    // 3) 预扫描 --config (决定是否加载配置文件)
    std::string configPath = FLAGS_config;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        const std::string key = "--config=";
        if (a.rfind(key, 0) == 0) {
            configPath = a.substr(key.size());
        } else if (a == "--config" && i + 1 < argc) {
            configPath = argv[i + 1];
        }
    }
    if (!configPath.empty() && configPath.front() != '/') {
        configPath = getExecutableDir(argv[0]) + "/" + configPath;
    }

    // 4) 若配置文件存在, 由 gflags 自身解析 (无需手写解析逻辑)
    //    文件格式: 每行一个 --key=value, 由 gflags 库标准支持
    {
        std::ifstream probe(configPath);
        if (probe.good()) {
            probe.close();
            google::ReadFromFlagsFile(configPath, argv[0],
                                      /*errors_fatal=*/true);
            std::cout << "[INFO] 已从配置文件加载参数: " << configPath
                      << std::endl;
        } else {
            std::cout << "[INFO] 未找到配置文件 " << configPath
                      << ", 将使用命令行参数 / gflags 默认值" << std::endl;
        }
    }

    // 5) 解析命令行 (会覆盖配置文件中的同名参数)
    google::ParseCommandLineFlags(&argc, &argv, /*remove_flags=*/true);

    // 6) 初始化日志
    const std::string execDir = getExecutableDir(argv[0]);
    const std::string logFile = execDir + "/logs/AIChatServer.log";
    holmes::Logger::initLogger("AIChatServer", logFile,
                               parseLogLevel(FLAGS_log_level));

    // 7) 组装 ServerConfig
    //    - Ollama 三项参数全部来自命令行/配置文件
    //    - 云端模型 API Key 仅从环境变量读取
    chat_server::ServerConfig config;
    config.host             = FLAGS_host;
    config.port             = FLAGS_port;
    config.logLevel         = FLAGS_log_level;
    config.temperature      = FLAGS_temperature;
    config.maxTokens        = FLAGS_max_tokens;
    config.deepseekAPIKey   = getEnvOrEmpty("DEEPSEEK_API_KEY");
    config.ollamaEndpoint   = FLAGS_ollama_endpoint;
    config.ollamaModelName  = FLAGS_ollama_model_name;
    config.ollamaModelDesc  = FLAGS_ollama_model_desc;

    // 8) 安全检查
    std::string err;
    if (!validateConfig(config, err)) {
        std::cerr << "[FATAL] 配置参数非法: " << err << std::endl;
        std::cerr << "请使用 " << argv[0] << " -h 查看帮助" << std::endl;
        ERR("配置参数非法: {}", err);
        google::ShutDownCommandLineFlags();
        return 1;
    }

    INFO("AIChatServer v{} starting on {}:{}", kAIChatServerVersion,
         config.host, config.port);
    INFO("log_level={}, temperature={}, max_tokens={}", config.logLevel,
         config.temperature, config.maxTokens);
    INFO("ollama: endpoint={}, model={}", config.ollamaEndpoint,
         config.ollamaModelName);

    // 9) 注册 Ctrl+C / kill 信号
    std::signal(SIGINT,  signalHandler);
    std::signal(SIGTERM, signalHandler);

    // 10) 启动服务器
    //     ChatServer::start() 内部已经派生 detached 线程跑 httplib::listen(),
    //     调用本身是非阻塞的, 主线程不会被卡住, 这里直接同步调用即可
    g_chatServer = std::make_unique<chat_server::ChatServer>(config);
    if (!g_chatServer->start()) {
        std::cerr << "[FATAL] ChatServer 启动失败" << std::endl;
        ERR("ChatServer 启动失败");
        g_chatServer.reset();
        google::ShutDownCommandLineFlags();
        return 2;
    }

    INFO("AIChatServer 已就绪, 按 Ctrl+C 退出");

    // 11) 主线程空闲等待退出信号
    while (!g_exitFlag.load() && g_chatServer->isRunning()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    // 12) 优雅退出: stop() 会让 httplib::listen() 返回,
    //     ChatServer 内部 detached 的监听线程随之自行清理
    INFO("收到退出信号, 正在停止服务...");
    g_chatServer->stop();
    g_chatServer.reset();
    INFO("AIChatServer 已退出");

    google::ShutDownCommandLineFlags();
    return 0;
}
