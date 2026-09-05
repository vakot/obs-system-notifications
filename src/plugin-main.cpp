#include <obs-frontend-api.h>
#include <obs-module.h>
#include <util/base.h>

#include <string>

OBS_DECLARE_MODULE()

namespace {

constexpr const char *kLogPrefix = "[obs-system-notifications]";

const char *event_name(enum obs_frontend_event event)
{
	switch (event) {
	case OBS_FRONTEND_EVENT_RECORDING_STARTED:
		return "RecordingStarted";
	case OBS_FRONTEND_EVENT_RECORDING_STOPPED:
		return "RecordingStopped";
	case OBS_FRONTEND_EVENT_RECORDING_PAUSED:
		return "RecordingPaused";
	case OBS_FRONTEND_EVENT_RECORDING_UNPAUSED:
		return "RecordingResumed";
	case OBS_FRONTEND_EVENT_REPLAY_BUFFER_STARTED:
		return "ReplayBufferStarted";
	case OBS_FRONTEND_EVENT_REPLAY_BUFFER_STOPPED:
		return "ReplayBufferStopped";
	case OBS_FRONTEND_EVENT_REPLAY_BUFFER_SAVED:
		return "ReplayBufferSaved";
	case OBS_FRONTEND_EVENT_SCREENSHOT_TAKEN:
		return "ScreenshotTaken";
	case OBS_FRONTEND_EVENT_EXIT:
		return "Exit";
	default:
		return "Other";
	}
}

void on_frontend_event(enum obs_frontend_event event, void *)
{
	blog(LOG_INFO, "%s frontend event: %s", kLogPrefix, event_name(event));
}

} // namespace

bool obs_module_load(void)
{
	obs_frontend_add_event_callback(on_frontend_event, nullptr);
	blog(LOG_INFO, "%s plugin loaded", kLogPrefix);
	return true;
}

void obs_module_unload(void)
{
	obs_frontend_remove_event_callback(on_frontend_event, nullptr);
	blog(LOG_INFO, "%s plugin unloaded", kLogPrefix);
}
