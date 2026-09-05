#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>

enum class NotificationEvent {
	RecordingStarted,
	RecordingPaused,
	RecordingResumed,
	RecordingSaved,
	ReplayBufferStarted,
	ReplayBufferStopped,
	ReplaySaved,
	ScreenshotSaved,
};

enum class ClickAction {
	None,
	OpenFile,
	RevealFile,
};

struct NotificationContext {
	std::optional<std::filesystem::path> filePath;
};

struct NotificationDefinition {
	const char *title;
	const char *body;
	ClickAction clickAction;
};

struct NotificationPayload {
	std::string title;
	std::string body;
	ClickAction clickAction;
	std::optional<std::filesystem::path> filePath;
};

using NotificationSink = std::function<void(const NotificationPayload &)>;

const NotificationDefinition &notification_definition(NotificationEvent event);
const char *notification_event_name(NotificationEvent event);
std::optional<NotificationPayload> make_notification_payload(NotificationEvent event,
									const NotificationContext &context = {});

class NotificationService {
public:
	explicit NotificationService(NotificationSink sink);

	void show(NotificationEvent event, const NotificationContext &context = {});

private:
	NotificationSink sink_;
};
