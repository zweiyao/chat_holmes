#include <mutex>
#include <spdlog/logger.h>
#include <spdlog/spdlog.h>

namespace holmes {
class Logger {
public:
  static void
  initLogger(const std::string &loggerName, const std::string &loggerFile,
             spdlog::level::level_enum logLevel = spdlog::level::info);
  static std::shared_ptr<spdlog::logger> getLogger();

private:
  Logger();
  Logger(const Logger &) = delete;
  Logger &operator=(const Logger &) = delete;

private:
  static std::shared_ptr<spdlog::logger> _logger;
  static std::mutex _mutex;
};

#define TRACE(format, ...)                                                     \
  holmes::Logger::getLogger()->trace(std::string("[{:>10s}:{:<4d}]") + format, \
                                     __FILE__, __LINE__, ##__VA_ARGS__)
#define DBG(format, ...)                                                       \
  holmes::Logger::getLogger()->debug(std::string("[{:>10s}:{:<4d}]") + format, \
                                     __FILE__, __LINE__, ##__VA_ARGS__)
#define INFO(format, ...)                                                      \
  holmes::Logger::getLogger()->info(std::string("[{:>10s}:{:<4d}]") + format,  \
                                    __FILE__, __LINE__, ##__VA_ARGS__)
#define WARN(format, ...)                                                      \
  holmes::Logger::getLogger()->warn(std::string("[{:>10s}:{:<4d}]") + format,  \
                                    __FILE__, __LINE__, ##__VA_ARGS__)
#define ERR(format, ...)                                                       \
  holmes::Logger::getLogger()->error(std::string("[{:>10s}:{:<4d}]") + format, \
                                     __FILE__, __LINE__, ##__VA_ARGS__)
#define CRIT(format, ...)                                                      \
  holmes::Logger::getLogger()->critical(std::string("[{:>10s}:{:<4d}]") +      \
                                            format,                            \
                                        __FILE__, __LINE__, ##__VA_ARGS__)
} // namespace holmes