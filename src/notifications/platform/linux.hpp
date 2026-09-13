#pragma once

#include <memory>

#include <notifications/index.hpp>

class LinuxNotificationBackend {
public:
	LinuxNotificationBackend();
	~LinuxNotificationBackend();

	LinuxNotificationBackend(const LinuxNotificationBackend &) = delete;
	LinuxNotificationBackend &operator=(const LinuxNotificationBackend &) = delete;

	bool start();
	void stop();
	void show(const NotificationPayload &payload);

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};
