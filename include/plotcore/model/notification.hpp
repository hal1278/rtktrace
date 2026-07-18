#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "plotcore/model/diagnostic.hpp"

namespace plotcore {

enum class NotificationLevel : std::uint8_t {
    Info,
    Warning,
    Error,
};

struct UserNotification {
    NotificationLevel level;
    std::string message;
};

class NotificationHistory {
public:
    void add(NotificationLevel level, std::string message);
    void add(const Diagnostic& diagnostic);
    void clear() noexcept;

    [[nodiscard]] const std::vector<UserNotification>& entries() const noexcept;
    [[nodiscard]] bool has_caution() const noexcept;

private:
    std::vector<UserNotification> entries_;
};

[[nodiscard]] const char* notification_level_name(NotificationLevel level) noexcept;

} // namespace plotcore
