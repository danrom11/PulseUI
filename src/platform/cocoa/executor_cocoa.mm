#import <Foundation/Foundation.h>
#include <memory>
#include <queue>
#include <mutex>
#include <functional>
#include <pulseui/core/executor.hpp>

namespace pulseui::platform {
  class CocoaExecutor final : public pulseui::core::executor {
  public:
    void post(std::function<void()> fn) override {
      // Dispatch to main queue
      auto blockFn = [fn = std::move(fn)](){ fn(); };
      dispatch_async(dispatch_get_main_queue(), blockFn);
    }
  };

  std::unique_ptr<pulseui::core::executor> make_cocoa_executor() {
    return std::make_unique<CocoaExecutor>();
  }
}
