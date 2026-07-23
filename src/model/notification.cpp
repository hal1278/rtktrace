#include "rtktrace/model/notification.hpp"

#include <algorithm>
#include <utility>

namespace rtktrace {

const char* notification_level_name(NotificationLevel level) noexcept
{
    switch (level) {
    case NotificationLevel::Info:
        return "Info";
    case NotificationLevel::Warning:
        return "Warning";
    case NotificationLevel::Error:
        return "Error";
    }
    return "Unknown";
}

void NotificationHistory::add(NotificationLevel level, std::string message)
{
    entries_.push_back(UserNotification{level, std::move(message)});
}

void NotificationHistory::add(const Diagnostic& diagnostic)
{
    NotificationLevel level = NotificationLevel::Info;
    if (diagnostic.severity == DiagnosticSeverity::Warning
        || diagnostic.severity == DiagnosticSeverity::RequiresDecision) {
        level = NotificationLevel::Warning;
    } else if (diagnostic.severity == DiagnosticSeverity::Fatal) {
        level = NotificationLevel::Error;
    }
    std::string message = diagnostic.file_name;
    if (diagnostic.source_line_number.has_value()) {
        message += ": line " + std::to_string(*diagnostic.source_line_number);
    }
    if (!message.empty()) {
        message += ": ";
    }
    message += diagnostic.message;
    add(level, std::move(message));
}

void NotificationHistory::clear() noexcept
{
    entries_.clear();
}

const std::vector<UserNotification>& NotificationHistory::entries() const noexcept
{
    return entries_;
}

bool NotificationHistory::has_caution() const noexcept
{
    return std::any_of(entries_.begin(), entries_.end(),
        [](const auto& entry) { return entry.level != NotificationLevel::Info; });
}

} // namespace rtktrace
