#include "index.hpp"

#include <utility>

namespace {

constexpr NotificationDefinition kRecordingStarted{
	"Recording started", "OBS is recording", ClickAction::None};
constexpr NotificationDefinition kRecordingPaused{
	"Recording paused", "Recording has been paused", ClickAction::None};
constexpr NotificationDefinition kRecordingResumed{
	"Recording resumed", "Recording has resumed", ClickAction::None};
constexpr NotificationDefinition kRecordingSaved{
	"Recording saved", "Recording has been saved", ClickAction::RevealFile};
constexpr NotificationDefinition kReplayBufferStarted{
	"Replay buffer started", "Replay buffer is active", ClickAction::None};
constexpr NotificationDefinition kReplayBufferStopped{
	"Replay buffer stopped", "Replay buffer has stopped", ClickAction::None};
constexpr NotificationDefinition kReplaySaved{
	"Replay saved", "Replay has been saved", ClickAction::RevealFile};
constexpr NotificationDefinition kScreenshotSaved{
	"Screenshot saved", "Screenshot has been saved", ClickAction::RevealFile};

} // namespace

const NotificationDefinition &notification_definition(NotificationEvent event)
{
	switch (event) {
	case NotificationEvent::RecordingStarted:
		return kRecordingStarted;
	case NotificationEvent::RecordingPaused:
		return kRecordingPaused;
	case NotificationEvent::RecordingResumed:
		return kRecordingResumed;
	case NotificationEvent::RecordingSaved:
		return kRecordingSaved;
	case NotificationEvent::ReplayBufferStarted:
		return kReplayBufferStarted;
	case NotificationEvent::ReplayBufferStopped:
		return kReplayBufferStopped;
	case NotificationEvent::ReplaySaved:
		return kReplaySaved;
	case NotificationEvent::ScreenshotSaved:
		return kScreenshotSaved;
	}

	return kRecordingStarted;
}

const char *notification_event_name(NotificationEvent event)
{
	switch (event) {
	case NotificationEvent::RecordingStarted:
		return "RecordingStarted";
	case NotificationEvent::RecordingPaused:
		return "RecordingPaused";
	case NotificationEvent::RecordingResumed:
		return "RecordingResumed";
	case NotificationEvent::RecordingSaved:
		return "RecordingSaved";
	case NotificationEvent::ReplayBufferStarted:
		return "ReplayBufferStarted";
	case NotificationEvent::ReplayBufferStopped:
		return "ReplayBufferStopped";
	case NotificationEvent::ReplaySaved:
		return "ReplaySaved";
	case NotificationEvent::ScreenshotSaved:
		return "ScreenshotSaved";
	}

	return "Unknown";
}

std::optional<NotificationPayload> make_notification_payload(NotificationEvent event,
									const NotificationContext &context)
{
	const NotificationDefinition &definition = notification_definition(event);
	NotificationPayload payload{
		definition.title,
		definition.body,
		definition.clickAction,
		std::nullopt,
	};

	if (definition.clickAction != ClickAction::None) {
		if (!context.filePath || context.filePath->empty())
			return std::nullopt;

		payload.filePath = context.filePath;
	}

	return payload;
}

NotificationService::NotificationService(NotificationSink sink) : sink_(std::move(sink)) {}

void NotificationService::show(NotificationEvent event, const NotificationContext &context)
{
	const std::optional<NotificationPayload> payload = make_notification_payload(event, context);
	if (payload && sink_)
		sink_(*payload);
}
