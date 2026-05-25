#pragma once
#include <ctime>
#include <string>
#include <vector>
namespace sdk_holmes {

class Message {
public:
  std::string _messageId; // 消息ID
  std::string _role;      // 消息角色
  std::string _content;   // 消息内容
  std::time_t _timestamp; // 消息发送时间戳
  Message(const std::string &role = "", const std::string &content = "")
      : _role(role), _content(content), _timestamp(0) {}
};

class Config {
public:
  std::string _modelName;
  double _temperature = 0.7;
  int _maxTokens = 2048;
  virtual ~Config() = default;
};

class APIConfig : public Config {
public:
  std::string _apiKey; // API密钥
};

class OllamaConfig : public Config {
public:
  std::string _modelName; // 模型名称
  std::string _modelDesc; // 模型描述
  std::string _endpoint;  // 模型API endpoint  base url
};

class ModelInfo {
public:
  std::string _modelName;    // 模型名称
  std::string _modelDesc;    // 模型描述
  std::string _provider;     // 模型提供者
  std::string _endpoint;     // 模型API endpoint  base url
  bool _isAvailable = false; // 模型是否可用

  ModelInfo(const std::string &modelName = "",
            const std::string &modelDesc = "", const std::string &provider = "",
            const std::string &endpoint = "")
      : _modelName(modelName), _modelDesc(modelDesc), _provider(provider),
        _endpoint(endpoint) {}
};

class Session {
public:
  std::string _sessionId;         // 会话ID
  std::string _modelName;         // 会话使用的模型名称
  std::vector<Message> _messages; // 会话中的消息列表
  std::time_t _createdAt;         // 会话创建时间戳
  std::time_t _updatedAt;         // 会话最后更新时间戳

  // 构造函数
  Session(const std::string &modelName = "") : _modelName(modelName) {}
};
} // namespace sdk_holmes