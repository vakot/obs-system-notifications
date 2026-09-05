#pragma once

#include <memory>

#include <notifications/index.hpp>

class WindowsNotificationBackend {
public:
	WindowsNotificationBackend();
	~WindowsNotificationBackend();

	WindowsNotificationBackend(const WindowsNotificationBackend &) = delete;
	WindowsNotificationBackend &operator=(const WindowsNotificationBackend &) = delete;

	bool start();
	void stop();
	void show(const NotificationPayload &payload);

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};
