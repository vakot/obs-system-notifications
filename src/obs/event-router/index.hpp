#pragma once

#include <obs-frontend-api.h>

#include <functional>

#include <notifications/index.hpp>

class EventRouter {
public:
	using Handler = std::function<void(NotificationEvent, const NotificationContext &)>;

	explicit EventRouter(Handler handler);
	~EventRouter();

	bool start();
	void stop();

private:
	static void on_frontend_event(enum obs_frontend_event event, void *private_data);
	void handle_event(enum obs_frontend_event event);
	void emit(NotificationEvent event);
	void emit_file_event(NotificationEvent event, char *raw_path);

	Handler handler_;
	bool started_ = false;
};
