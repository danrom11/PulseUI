#pragma once
#include <optional>
#include <string>
#include <vector>

namespace pulseui {

class Window;

namespace dialogs {

struct Filter {
  std::string name;     // "Pulse forms"
  std::string patterns; // "*.pform;*.json"
};

// Native file dialogs (modal). Return std::nullopt when cancelled.
std::optional<std::string> open_file(Window* parent, const std::string& title, const std::vector<Filter>& filters = {});
std::optional<std::string> save_file(Window* parent, const std::string& title, const std::vector<Filter>& filters = {},
                                     const std::string& default_name = {});

} // namespace dialogs
} // namespace pulseui
