#include <obs-module.h>
#include <util/base.h>

#include <memory>
#include <string>

#include <notifications/index.hpp>
#include <notifications/platform/windows.hpp>
#include <obs/event-router/index.hpp>

OBS_DECLARE_MODULE()

namespace {

constexpr const char *kLogPrefix = "[obs-system-notifications]";

std::unique_ptr<NotificationService> notification_service;
std::unique_ptr<WindowsNotificationBackend> notification_backend;
std::unique_ptr<EventRouter> event_router;

void on_notification(NotificationEvent event, const NotificationContext &context)
{
	if (!notification_service)
		return;

	notification_service->show(event, context);
}

void log_notification_payload(const NotificationPayload &payload)
{
	const std::string path = payload.filePath ? payload.filePath->u8string() : std::string{};
	blog(LOG_INFO, "%s notification payload: %s | %s | path=%s", kLogPrefix,
		payload.title.c_str(), payload.body.c_str(), path.c_str());
}

} // namespace

bool obs_module_load(void)
{
	notification_backend = std::make_unique<WindowsNotificationBackend>();
	if (!notification_backend->start()) {
		blog(LOG_WARNING, "%s notification backend unavailable; continuing without system toasts", kLogPrefix);
	}
	notification_service = std::make_unique<NotificationService>(
		[](const NotificationPayload &payload) {
			log_notification_payload(payload);
			if (notification_backend)
				notification_backend->show(payload);
		});
	event_router = std::make_unique<EventRouter>(on_notification);
	if (!event_router->start()) {
		event_router.reset();
		notification_service.reset();
		notification_backend.reset();
		blog(LOG_ERROR, "%s event router startup failed", kLogPrefix);
		return false;
	}

	blog(LOG_INFO, "%s plugin loaded", kLogPrefix);
	return true;
}

void obs_module_unload(void)
{
	event_router.reset();
	notification_service.reset();
	notification_backend.reset();
	blog(LOG_INFO, "%s plugin unloaded", kLogPrefix);
}
