#include "index.hpp"

#include <util/base.h>
#include <util/bmem.h>

#include <filesystem>
#include <string>
#include <utility>

namespace {

constexpr const char *kLogPrefix = "[obs-system-notifications]";

std::filesystem::path take_obs_path(char *raw_path)
{
	if (!raw_path)
		return {};

	std::string utf8_path(raw_path);
	bfree(raw_path);
	if (utf8_path.empty())
		return {};

	return std::filesystem::u8path(utf8_path);
}

} // namespace

EventRouter::EventRouter(Handler handler) : handler_(std::move(handler)) {}

EventRouter::~EventRouter()
{
	stop();
}

bool EventRouter::start()
{
	if (started_)
		return true;

	obs_frontend_add_event_callback(on_frontend_event, this);
	started_ = true;
	return true;
}

void EventRouter::stop()
{
	if (!started_)
		return;

	obs_frontend_remove_event_callback(on_frontend_event, this);
	started_ = false;
}

void EventRouter::on_frontend_event(enum obs_frontend_event event, void *private_data)
{
	static_cast<EventRouter *>(private_data)->handle_event(event);
}

void EventRouter::handle_event(enum obs_frontend_event event)
{
	switch (event) {
	case OBS_FRONTEND_EVENT_RECORDING_STARTED:
		emit(NotificationEvent::RecordingStarted);
		break;
	case OBS_FRONTEND_EVENT_RECORDING_PAUSED:
		emit(NotificationEvent::RecordingPaused);
		break;
	case OBS_FRONTEND_EVENT_RECORDING_UNPAUSED:
		emit(NotificationEvent::RecordingResumed);
		break;
	case OBS_FRONTEND_EVENT_RECORDING_STOPPED:
		emit_file_event(NotificationEvent::RecordingSaved, obs_frontend_get_last_recording());
		break;
	case OBS_FRONTEND_EVENT_REPLAY_BUFFER_STARTED:
		emit(NotificationEvent::ReplayBufferStarted);
		break;
	case OBS_FRONTEND_EVENT_REPLAY_BUFFER_STOPPED:
		emit(NotificationEvent::ReplayBufferStopped);
		break;
	case OBS_FRONTEND_EVENT_REPLAY_BUFFER_SAVED:
		emit_file_event(NotificationEvent::ReplaySaved, obs_frontend_get_last_replay());
		break;
	case OBS_FRONTEND_EVENT_SCREENSHOT_TAKEN:
		emit_file_event(NotificationEvent::ScreenshotSaved, obs_frontend_get_last_screenshot());
		break;
	default:
		break;
	}
}

void EventRouter::emit(NotificationEvent event)
{
	if (!handler_)
		return;

	handler_(event, {});
}

void EventRouter::emit_file_event(NotificationEvent event, char *raw_path)
{
	const std::filesystem::path path = take_obs_path(raw_path);
	if (path.empty()) {
		blog(LOG_WARNING, "%s %s has no final OBS path", kLogPrefix, notification_event_name(event));
		return;
	}

	NotificationContext context;
	context.filePath = path;
	if (handler_)
		handler_(event, context);
}
