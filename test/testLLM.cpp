#include "../sdk/include/DeepSeekProvider.h"
#include "../sdk/include/OllamaLLMProvider.h"
#include "../sdk/include/util/myLog.h"
#include <gtest/gtest.h>
#include <spdlog/common.h>
#include <string>
#include <vector>

// TEST(DeepSeekProviderTest, sendMessage) {
//   auto provider = std::make_shared<sdk_holmes::DeepSeekProvider>();
//   ASSERT_TRUE(provider != nullptr);

//   std::map<std::string, std::string> modelParam;
//   modelParam["api_key"] = "sk-9b2e8f52d85a44b881d96d08b83058d4";
//   modelParam["endpoint"] = "https://api.deepseek.com";

//   provider->initModel(modelParam);
//   ASSERT_TRUE(provider->isAvailable());

//   std::map<std::string, std::string> requestParam = {{"temperature", "0.7"},
//                                                      {"max_tokens", "2048"}};
//   std::vector<sdk_holmes::Message> messages;
//   messages.push_back({"user", "你是谁？"});

//   // std::string response= provider->sendMessage(messages,requestParam)
//   auto writeChunk = [&](const std::string &chunk, bool last) {
//     INFO("chunk : {}", chunk);
//     if (last) {
//       INFO("[DONE]");
//     }
//   };
  
//   std::string fullData =
//       provider->sendMessageStream(messages, requestParam, writeChunk);
//   ASSERT_FALSE(fullData.empty());
//   INFO("response : {}", fullData);
// }
TEST(OllamaLLMProviderTest, sendMessage){
    auto provider = std::make_shared<sdk_holmes::OllamaLLMProvider>();
    ASSERT_TRUE(provider != nullptr);

    std::map<std::string, std::string> modelParam;
    modelParam["model_name"] = "qwen3.5:9b";
    modelParam["model_desc"] = "本地部署qwen3.5:9b模型，采用专家混合架构，专注于深度理解与推理";
    modelParam["endpoint"] = "http://localhost:11434";

    provider->initModel(modelParam);
    ASSERT_TRUE(provider->isAvailable());

    std::map<std::string, std::string> requestParam = {
        {"temperature", "0.7"},
        {"max_tokens", "2048"}
    };
    std::vector<sdk_holmes::Message> messages;
    messages.push_back({"user", "你是谁？"});

    auto writeChunk = [&](const std::string& chunk, bool last){ 
        INFO("chunk : {}", chunk);
        if(last){
            INFO("[DONE]"); 
        } 
    };
    std::string fullData = provider->sendMessageStream(messages, requestParam, writeChunk);
    ASSERT_FALSE(fullData.empty());
    INFO("response : {}", fullData);
}

int main(int argc, char **argv) {
  holmes::Logger::initLogger("testLLM", "stdout", spdlog::level::debug);
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}