#include "windows.hpp"

#include <roapi.h>
#include <shellapi.h>
#include <windows.h>

#include <winrt/Windows.Data.Xml.Dom.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.Notifications.h>
#include <winrt/base.h>

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <util/base.h>

namespace {

constexpr const char *kLogPrefix = "[obs-system-notifications]";
constexpr wchar_t kAppUserModelId[] = L"OBS Studio";

std::wstring escape_xml(std::wstring_view value)
{
	std::wstring escaped;
	for (const wchar_t character : value) {
		switch (character) {
		case L'&':
			escaped += L"&amp;";
			break;
		case L'<':
			escaped += L"&lt;";
			break;
		case L'>':
			escaped += L"&gt;";
			break;
		case L'\"':
			escaped += L"&quot;";
			break;
		case L'\'':
			escaped += L"&apos;";
			break;
		default:
			escaped += character;
			break;
		}
	}
	return escaped;
}

std::wstring to_xml(const NotificationPayload &payload)
{
	const std::wstring title = winrt::to_hstring(payload.title).c_str();
	const std::wstring body = winrt::to_hstring(payload.body).c_str();
	return L"<toast launch=\"reveal\"><visual><binding template=\"ToastGeneric\"><text>" +
		escape_xml(title) + L"</text><text>" + escape_xml(body) + L"</text></binding></visual></toast>";
}

void reveal_file(const std::wstring &path)
{
	if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
		blog(LOG_WARNING, "%s cannot reveal missing file: %ls", kLogPrefix, path.c_str());
		return;
	}

	const std::wstring arguments = L"/select,\"" + path + L"\"";
	const HINSTANCE result = ShellExecuteW(nullptr, L"open", L"explorer.exe", arguments.c_str(), nullptr,
		SW_SHOWNORMAL);
	if (reinterpret_cast<INT_PTR>(result) <= 32)
		blog(LOG_WARNING, "%s failed to open Explorer for: %ls", kLogPrefix, path.c_str());
}

} // namespace

struct WindowsNotificationBackend::Impl {
	struct ActiveToast {
		winrt::Windows::UI::Notifications::ToastNotification toast{nullptr};
		winrt::event_token activationToken{};
		winrt::event_token failureToken{};
	};

	winrt::Windows::UI::Notifications::ToastNotifier notifier{nullptr};
	std::vector<std::unique_ptr<ActiveToast>> activeToasts;
	bool ownsRoInitialization = false;
	bool started = false;
};

WindowsNotificationBackend::WindowsNotificationBackend() : impl_(std::make_unique<Impl>()) {}

WindowsNotificationBackend::~WindowsNotificationBackend()
{
	stop();
}

bool WindowsNotificationBackend::start()
{
	if (impl_->started)
		return true;

	const HRESULT ro_result = RoInitialize(RO_INIT_MULTITHREADED);
	if (FAILED(ro_result) && ro_result != RPC_E_CHANGED_MODE) {
		blog(LOG_ERROR, "%s Windows Runtime initialization failed: 0x%08lx", kLogPrefix,
			static_cast<unsigned long>(ro_result));
		return false;
	}
	impl_->ownsRoInitialization = SUCCEEDED(ro_result);

	try {
		impl_->notifier = winrt::Windows::UI::Notifications::ToastNotificationManager::CreateToastNotifier(
			winrt::hstring{kAppUserModelId});
		blog(LOG_INFO, "%s Windows notification setting: %d", kLogPrefix,
			static_cast<int>(impl_->notifier.Setting()));
		impl_->started = true;
		return true;
	} catch (const winrt::hresult_error &error) {
		blog(LOG_ERROR, "%s Windows notification backend initialization failed: 0x%08lx", kLogPrefix,
			static_cast<unsigned long>(error.code().value));
		if (impl_->ownsRoInitialization)
			RoUninitialize();
		impl_->ownsRoInitialization = false;
		return false;
	}
}

void WindowsNotificationBackend::stop()
{
	if (!impl_->started && !impl_->ownsRoInitialization)
		return;

	for (const auto &activeToast : impl_->activeToasts) {
		if (!activeToast || !activeToast->toast)
			continue;
		if (activeToast->activationToken.value != 0)
			activeToast->toast.Activated(activeToast->activationToken);
		if (activeToast->failureToken.value != 0)
			activeToast->toast.Failed(activeToast->failureToken);
	}
	impl_->activeToasts.clear();
	impl_->notifier = nullptr;
	impl_->started = false;
	if (impl_->ownsRoInitialization) {
		RoUninitialize();
		impl_->ownsRoInitialization = false;
	}
}

void WindowsNotificationBackend::show(const NotificationPayload &payload)
{
	if (!impl_->started)
		return;

	try {
		winrt::Windows::Data::Xml::Dom::XmlDocument document;
		document.LoadXml(winrt::hstring{to_xml(payload)});

		auto activeToast = std::make_unique<Impl::ActiveToast>();
		activeToast->toast = winrt::Windows::UI::Notifications::ToastNotification{document};
		if (payload.clickAction == ClickAction::RevealFile && payload.filePath) {
			const std::wstring path = payload.filePath->wstring();
			activeToast->activationToken = activeToast->toast.Activated(
				[path](const auto &, const auto &) {
					blog(LOG_INFO, "%s notification activated for: %ls", kLogPrefix, path.c_str());
					reveal_file(path);
				});
			activeToast->failureToken = activeToast->toast.Failed(
				[](const auto &, const auto &error) {
					blog(LOG_WARNING, "%s notification delivery failed: 0x%08lx", kLogPrefix,
						static_cast<unsigned long>(error.ErrorCode().value));
				});
		}

		impl_->notifier.Show(activeToast->toast);
		if (payload.clickAction != ClickAction::None)
			impl_->activeToasts.push_back(std::move(activeToast));
	} catch (const winrt::hresult_error &error) {
		blog(LOG_WARNING, "%s failed to show notification: 0x%08lx", kLogPrefix,
			static_cast<unsigned long>(error.code().value));
	}
}
