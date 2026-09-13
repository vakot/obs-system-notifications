#include <filesystem>
#include <string>

#include <notifications/index.hpp>

int main()
{
	struct ExpectedDefinition {
		NotificationEvent event;
		const char *title;
		const char *body;
		ClickAction clickAction;
	};

	const ExpectedDefinition definitions[] = {
		{NotificationEvent::RecordingStarted, "Recording started", "OBS is recording", ClickAction::None},
		{NotificationEvent::RecordingPaused, "Recording paused", "Recording has been paused", ClickAction::None},
		{NotificationEvent::RecordingResumed, "Recording resumed", "Recording has resumed", ClickAction::None},
		{NotificationEvent::RecordingSaved, "Recording saved", "Recording has been saved", ClickAction::RevealFile},
		{NotificationEvent::ReplayBufferStarted, "Replay buffer started", "Replay buffer is active", ClickAction::None},
		{NotificationEvent::ReplayBufferStopped, "Replay buffer stopped", "Replay buffer has stopped", ClickAction::None},
		{NotificationEvent::ReplaySaved, "Replay saved", "Replay has been saved", ClickAction::RevealFile},
		{NotificationEvent::ScreenshotSaved, "Screenshot saved", "Screenshot has been saved", ClickAction::RevealFile},
	};

	for (const ExpectedDefinition &expected : definitions) {
		const NotificationDefinition &definition = notification_definition(expected.event);
		if (std::string{definition.title} != expected.title || std::string{definition.body} != expected.body ||
		    definition.clickAction != expected.clickAction)
			return 1;

		const NotificationEvent event = expected.event;
		if (expected.clickAction != ClickAction::None)
			continue;

		const auto payload = make_notification_payload(event);
		if (!payload || payload->title != expected.title || payload->body != expected.body ||
		    payload->clickAction != ClickAction::None || payload->filePath)
			return 1;
	}

	const NotificationContext context{std::filesystem::path{L"C:\\OBS\\Replay A.mkv"}};
	for (const NotificationEvent event :
	     {NotificationEvent::RecordingSaved, NotificationEvent::ReplaySaved,
	      NotificationEvent::ScreenshotSaved}) {
		if (make_notification_payload(event))
			return 1;
		const auto payload = make_notification_payload(event, context);
		if (!payload || payload->clickAction != ClickAction::RevealFile || payload->filePath != context.filePath)
			return 1;
	}

	return 0;
}
