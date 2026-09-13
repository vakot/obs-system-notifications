#include "linux.hpp"

#include <dbus/dbus.h>

#include <util/base.h>

#include <atomic>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>

struct LinuxNotificationState;

namespace {

constexpr const char *kLogPrefix = "[obs-system-notifications]";
constexpr const char *kNotificationInterface = "org.freedesktop.Notifications";
constexpr const char *kFileManagerService = "org.freedesktop.FileManager1";
constexpr const char *kFileManagerPath = "/org/freedesktop/FileManager1";
constexpr const char *kFileManagerInterface = "org.freedesktop.FileManager1";

bool is_uri_unreserved(unsigned char character)
{
	return std::isalnum(character) || character == '-' || character == '.' || character == '_' ||
	       character == '~';
}

std::string file_uri(const std::filesystem::path &path)
{
	const std::string utf8_path = path.u8string();
	std::string uri = "file://";
	static constexpr char kHex[] = "0123456789ABCDEF";

	for (const unsigned char character : utf8_path) {
		if (is_uri_unreserved(character) || character == '/') {
			uri += static_cast<char>(character);
		} else {
			uri += '%';
			uri += kHex[character >> 4];
			uri += kHex[character & 0x0F];
		}
	}

	return uri;
}

void log_message_error(const char *operation, DBusMessage *message)
{
	const char *error_name = dbus_message_get_error_name(message);
	const char *error_text = nullptr;
	DBusMessageIter arguments;
	if (dbus_message_iter_init(message, &arguments) &&
	    dbus_message_iter_get_arg_type(&arguments) == DBUS_TYPE_STRING) {
		dbus_message_iter_get_basic(&arguments, &error_text);
	}

	blog(LOG_WARNING, "%s %s failed: %s%s%s", kLogPrefix, operation,
		error_name ? error_name : "unknown D-Bus error", error_text ? ": " : "",
		error_text ? error_text : "");
}

bool show_file_in_manager(const std::filesystem::path &path)
{
	if (!path.is_absolute()) {
		blog(LOG_WARNING, "%s cannot reveal a relative file path: %s", kLogPrefix, path.c_str());
		return false;
	}

	std::error_code error;
	if (!std::filesystem::exists(path, error) || error) {
		blog(LOG_WARNING, "%s cannot reveal missing file: %s", kLogPrefix, path.c_str());
		return false;
	}

	DBusError bus_error;
	dbus_error_init(&bus_error);
	DBusConnection *connection = dbus_bus_get_private(DBUS_BUS_SESSION, &bus_error);
	if (!connection) {
		blog(LOG_WARNING, "%s failed to connect to the D-Bus session bus: %s%s%s", kLogPrefix,
			bus_error.name ? bus_error.name : "unknown error", bus_error.message ? ": " : "",
			bus_error.message ? bus_error.message : "");
		dbus_error_free(&bus_error);
		return false;
	}
	dbus_connection_set_exit_on_disconnect(connection, FALSE);

	DBusMessage *message = dbus_message_new_method_call(kFileManagerService, kFileManagerPath,
		kFileManagerInterface, "ShowItems");
	if (!message) {
		blog(LOG_WARNING, "%s failed to allocate the file-manager D-Bus request", kLogPrefix);
		dbus_connection_close(connection);
		dbus_connection_unref(connection);
		return false;
	}

	const std::string uri = file_uri(path);
	const char *uri_data = uri.c_str();
	const char *startup_id = "";
	DBusMessageIter arguments;
	DBusMessageIter uri_array;
	dbus_message_iter_init_append(message, &arguments);
	dbus_message_iter_open_container(&arguments, DBUS_TYPE_ARRAY, DBUS_TYPE_STRING_AS_STRING,
		&uri_array);
	dbus_message_iter_append_basic(&uri_array, DBUS_TYPE_STRING, &uri_data);
	dbus_message_iter_close_container(&arguments, &uri_array);
	dbus_message_iter_append_basic(&arguments, DBUS_TYPE_STRING, &startup_id);

	DBusMessage *reply = dbus_connection_send_with_reply_and_block(connection, message, 5000,
		&bus_error);
	dbus_message_unref(message);
	if (!reply) {
		blog(LOG_WARNING, "%s failed to reveal file through the desktop file manager: %s%s%s",
			kLogPrefix, bus_error.name ? bus_error.name : "unknown error",
			bus_error.message ? ": " : "", bus_error.message ? bus_error.message : "");
		dbus_error_free(&bus_error);
		dbus_connection_close(connection);
		dbus_connection_unref(connection);
		return false;
	}

	const bool success = dbus_message_get_type(reply) != DBUS_MESSAGE_TYPE_ERROR;
	if (!success)
		log_message_error("file reveal", reply);
	dbus_error_free(&bus_error);
	dbus_message_unref(reply);
	dbus_connection_close(connection);
	dbus_connection_unref(connection);
	return success;
}

struct PendingNotification {
	std::shared_ptr<LinuxNotificationState> state;
	std::optional<std::filesystem::path> file_path;
};

void destroy_pending_notification(void *user_data)
{
	delete static_cast<PendingNotification *>(user_data);
}

} // namespace

struct LinuxNotificationState {
	DBusConnection *connection = nullptr;
	std::atomic_bool stopping = false;
	std::mutex mutex;
	std::unordered_map<uint32_t, std::filesystem::path> file_paths;
};

struct LinuxNotificationBackend::Impl {
	std::shared_ptr<LinuxNotificationState> state = std::make_shared<LinuxNotificationState>();
	std::thread dispatch_thread;
	bool started = false;

	static void notification_reply(DBusPendingCall *pending, void *user_data)
	{
		const auto *pending_data = static_cast<PendingNotification *>(user_data);
		DBusMessage *reply = dbus_pending_call_steal_reply(pending);
		if (!reply)
			return;

		if (dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR) {
			log_message_error("notification delivery", reply);
			dbus_message_unref(reply);
			return;
		}

		DBusMessageIter arguments;
		uint32_t notification_id = 0;
		if (!dbus_message_iter_init(reply, &arguments) ||
		    dbus_message_iter_get_arg_type(&arguments) != DBUS_TYPE_UINT32) {
			blog(LOG_WARNING, "%s notification service returned an invalid notification id", kLogPrefix);
			dbus_message_unref(reply);
			return;
		}
		dbus_message_iter_get_basic(&arguments, &notification_id);

		if (pending_data->file_path) {
			std::lock_guard lock(pending_data->state->mutex);
			pending_data->state->file_paths[notification_id] = *pending_data->file_path;
		}

		dbus_message_unref(reply);
	}

	static DBusHandlerResult handle_signal(DBusConnection *, DBusMessage *message, void *user_data)
	{
		auto *state = static_cast<LinuxNotificationState *>(user_data);
		if (dbus_message_is_signal(message, kNotificationInterface, "ActionInvoked")) {
			DBusMessageIter arguments;
			uint32_t notification_id = 0;
			const char *action = nullptr;
			if (!dbus_message_iter_init(message, &arguments) ||
			    dbus_message_iter_get_arg_type(&arguments) != DBUS_TYPE_UINT32) {
				return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
			}
			dbus_message_iter_get_basic(&arguments, &notification_id);
			if (!dbus_message_iter_next(&arguments) ||
			    dbus_message_iter_get_arg_type(&arguments) != DBUS_TYPE_STRING) {
				return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
			}
			dbus_message_iter_get_basic(&arguments, &action);
			if (!action || std::string{action} != "default")
				return DBUS_HANDLER_RESULT_HANDLED;

			std::optional<std::filesystem::path> path;
			{
				std::lock_guard lock(state->mutex);
				const auto path_it = state->file_paths.find(notification_id);
				if (path_it != state->file_paths.end()) {
					path = path_it->second;
					state->file_paths.erase(path_it);
				}
			}
			if (path)
				show_file_in_manager(*path);
			return DBUS_HANDLER_RESULT_HANDLED;
		}

		if (dbus_message_is_signal(message, kNotificationInterface, "NotificationClosed")) {
			DBusMessageIter arguments;
			uint32_t notification_id = 0;
			if (dbus_message_iter_init(message, &arguments) &&
			    dbus_message_iter_get_arg_type(&arguments) == DBUS_TYPE_UINT32) {
				dbus_message_iter_get_basic(&arguments, &notification_id);
				std::lock_guard lock(state->mutex);
				state->file_paths.erase(notification_id);
			}
			return DBUS_HANDLER_RESULT_HANDLED;
		}

		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	}

	void dispatch()
	{
		while (!state->stopping && dbus_connection_read_write_dispatch(state->connection, 100)) {
		}
	}
};

LinuxNotificationBackend::LinuxNotificationBackend() : impl_(std::make_unique<Impl>()) {}

LinuxNotificationBackend::~LinuxNotificationBackend()
{
	stop();
}

bool LinuxNotificationBackend::start()
{
	if (impl_->started)
		return true;

	if (!dbus_threads_init_default()) {
		blog(LOG_ERROR, "%s failed to initialize D-Bus thread support", kLogPrefix);
		return false;
	}

	DBusError error;
	dbus_error_init(&error);
	DBusConnection *connection = dbus_bus_get_private(DBUS_BUS_SESSION, &error);
	if (!connection) {
		blog(LOG_WARNING, "%s notification backend unavailable: %s%s%s", kLogPrefix,
			error.name ? error.name : "unknown D-Bus error", error.message ? ": " : "",
			error.message ? error.message : "");
		dbus_error_free(&error);
		return false;
	}
	dbus_connection_set_exit_on_disconnect(connection, FALSE);

	static constexpr const char *kMatchRules[] = {
		"type='signal',interface='org.freedesktop.Notifications',member='ActionInvoked'",
		"type='signal',interface='org.freedesktop.Notifications',member='NotificationClosed'",
	};
	for (const char *match_rule : kMatchRules) {
		dbus_bus_add_match(connection, match_rule, &error);
		if (dbus_error_is_set(&error)) {
			blog(LOG_WARNING, "%s failed to subscribe to desktop notification events: %s%s%s",
				kLogPrefix, error.name ? error.name : "unknown D-Bus error", error.message ? ": " : "",
				error.message ? error.message : "");
			dbus_error_free(&error);
			dbus_connection_close(connection);
			dbus_connection_unref(connection);
			return false;
		}
	}

	impl_->state->connection = connection;
	impl_->state->stopping = false;
	if (!dbus_connection_add_filter(connection, &Impl::handle_signal, impl_->state.get(), nullptr)) {
		blog(LOG_WARNING, "%s failed to subscribe to desktop notification events", kLogPrefix);
		dbus_connection_close(connection);
		dbus_connection_unref(connection);
		impl_->state->connection = nullptr;
		return false;
	}
	dbus_connection_flush(connection);
	impl_->started = true;
	impl_->dispatch_thread = std::thread([this] { impl_->dispatch(); });
	return true;
}

void LinuxNotificationBackend::stop()
{
	if (!impl_->started)
		return;

	impl_->state->stopping = true;
	dbus_connection_close(impl_->state->connection);
	if (impl_->dispatch_thread.joinable())
		impl_->dispatch_thread.join();

	dbus_connection_remove_filter(impl_->state->connection, &Impl::handle_signal, impl_->state.get());
	dbus_connection_unref(impl_->state->connection);
	impl_->state->connection = nullptr;
	{
		std::lock_guard lock(impl_->state->mutex);
		impl_->state->file_paths.clear();
	}
	impl_->started = false;
}

void LinuxNotificationBackend::show(const NotificationPayload &payload)
{
	if (!impl_->started)
		return;

	DBusMessage *message = dbus_message_new_method_call("org.freedesktop.Notifications",
		"/org/freedesktop/Notifications", kNotificationInterface, "Notify");
	if (!message) {
		blog(LOG_WARNING, "%s failed to allocate the desktop notification request", kLogPrefix);
		return;
	}

	const char *application_name = "OBS Studio";
	uint32_t replaces_id = 0;
	const char *application_icon = "";
	const char *summary = payload.title.c_str();
	const char *body = payload.body.c_str();
	int32_t expire_timeout = -1;
	DBusMessageIter arguments;
	DBusMessageIter actions;
	DBusMessageIter hints;
	dbus_message_iter_init_append(message, &arguments);
	dbus_message_iter_append_basic(&arguments, DBUS_TYPE_STRING, &application_name);
	dbus_message_iter_append_basic(&arguments, DBUS_TYPE_UINT32, &replaces_id);
	dbus_message_iter_append_basic(&arguments, DBUS_TYPE_STRING, &application_icon);
	dbus_message_iter_append_basic(&arguments, DBUS_TYPE_STRING, &summary);
	dbus_message_iter_append_basic(&arguments, DBUS_TYPE_STRING, &body);
	dbus_message_iter_open_container(&arguments, DBUS_TYPE_ARRAY, DBUS_TYPE_STRING_AS_STRING,
		&actions);
	if (payload.filePath) {
		const char *action = "default";
		const char *label = "Show in File Manager";
		dbus_message_iter_append_basic(&actions, DBUS_TYPE_STRING, &action);
		dbus_message_iter_append_basic(&actions, DBUS_TYPE_STRING, &label);
	}
	dbus_message_iter_close_container(&arguments, &actions);
	dbus_message_iter_open_container(&arguments, DBUS_TYPE_ARRAY, "{sv}", &hints);
	dbus_message_iter_close_container(&arguments, &hints);
	dbus_message_iter_append_basic(&arguments, DBUS_TYPE_INT32, &expire_timeout);

	DBusPendingCall *pending = nullptr;
	if (!dbus_connection_send_with_reply(impl_->state->connection, message, &pending, -1) || !pending) {
		blog(LOG_WARNING, "%s failed to send desktop notification request", kLogPrefix);
		dbus_message_unref(message);
		return;
	}

	auto pending_data = std::make_unique<PendingNotification>();
	pending_data->state = impl_->state;
	pending_data->file_path = payload.filePath;
	PendingNotification *pending_data_ptr = pending_data.release();
	if (!dbus_pending_call_set_notify(pending, &Impl::notification_reply, pending_data_ptr,
		&destroy_pending_notification)) {
		blog(LOG_WARNING, "%s failed to track desktop notification response", kLogPrefix);
		destroy_pending_notification(pending_data_ptr);
		dbus_pending_call_cancel(pending);
		dbus_pending_call_unref(pending);
		dbus_message_unref(message);
		return;
	}

	dbus_pending_call_unref(pending);
	dbus_connection_flush(impl_->state->connection);
	dbus_message_unref(message);
}
