#include "windows.hpp"

#include <roapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <windows.h>

#include <winrt/Windows.Data.Xml.Dom.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.Notifications.h>
#include <winrt/base.h>

#include <algorithm>
#include <cstdint>
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
constexpr wchar_t kNotificationShortcutName[] = L"OBS System Notifications.lnk";
constexpr wchar_t kNotificationShortcutDescription[] = L"OBS Studio notification integration";
constexpr size_t kMaxActiveToasts = 32;

constexpr PROPERTYKEY kAppUserModelIdPropertyKey = {
	{0x9F4C2855, 0x9F79, 0x4B39, {0xA8, 0xD0, 0xE1, 0xD4, 0x2D, 0xE1, 0xD5, 0xF3}}, 5};

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

std::wstring notification_emoji(std::string_view title)
{
	if (title == "Recording started")
		return L"\U0001F534";
	if (title == "Recording paused")
		return L"\u23F8";
	if (title == "Recording resumed")
		return L"\u25B6";
	if (title == "Recording saved")
		return L"\U0001F4BE";
	if (title == "Replay buffer started")
		return L"\U0001F501";
	if (title == "Replay buffer stopped")
		return L"\u23F9";
	if (title == "Replay saved")
		return L"\U0001F39E";
	if (title == "Screenshot saved")
		return L"\U0001F4F8";
	return {};
}

bool ensure_notification_identity()
{
	try {
		wchar_t executablePath[MAX_PATH] = {};
		const DWORD executableLength = GetModuleFileNameW(nullptr, executablePath, ARRAYSIZE(executablePath));
		if (executableLength == 0 || executableLength >= ARRAYSIZE(executablePath)) {
			blog(LOG_ERROR, "%s failed to resolve OBS executable path: %lu", kLogPrefix,
				static_cast<unsigned long>(GetLastError()));
			return false;
		}

		PWSTR programsPath = nullptr;
		const HRESULT folderResult =
			SHGetKnownFolderPath(FOLDERID_Programs, KF_FLAG_DEFAULT, nullptr, &programsPath);
		if (FAILED(folderResult) || !programsPath) {
			blog(LOG_ERROR, "%s failed to resolve Start menu path: 0x%08lx", kLogPrefix,
				static_cast<unsigned long>(folderResult));
			return false;
		}

		const std::filesystem::path shortcutPath = std::filesystem::path{programsPath} /
			kNotificationShortcutName;
		CoTaskMemFree(programsPath);

		winrt::com_ptr<IShellLinkW> shellLink;
		winrt::check_hresult(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
			IID_PPV_ARGS(shellLink.put())));
		winrt::check_hresult(shellLink->SetPath(executablePath));
		winrt::check_hresult(shellLink->SetWorkingDirectory(std::filesystem::path{executablePath}.parent_path().c_str()));
		winrt::check_hresult(shellLink->SetArguments(L""));
		winrt::check_hresult(shellLink->SetDescription(kNotificationShortcutDescription));

		winrt::com_ptr<IPropertyStore> propertyStore;
		winrt::check_hresult(shellLink->QueryInterface(IID_PPV_ARGS(propertyStore.put())));
		PROPVARIANT appUserModelId{};
		appUserModelId.vt = VT_LPWSTR;
		appUserModelId.pwszVal = const_cast<PWSTR>(kAppUserModelId);
		winrt::check_hresult(propertyStore->SetValue(kAppUserModelIdPropertyKey, appUserModelId));
		winrt::check_hresult(propertyStore->Commit());

		winrt::com_ptr<IPersistFile> persistFile;
		winrt::check_hresult(shellLink->QueryInterface(IID_PPV_ARGS(persistFile.put())));
		winrt::check_hresult(persistFile->Save(shortcutPath.c_str(), TRUE));
		blog(LOG_INFO, "%s notification identity shortcut ready: %ls", kLogPrefix,
			shortcutPath.c_str());
		return true;
	} catch (const winrt::hresult_error &error) {
		blog(LOG_ERROR, "%s notification identity setup failed: 0x%08lx", kLogPrefix,
			static_cast<unsigned long>(error.code().value));
		return false;
	}
}

std::wstring to_xml(const NotificationPayload &payload)
{
	const std::wstring title = winrt::to_hstring(payload.title).c_str();
	const std::wstring emoji = notification_emoji(payload.title);
	const std::wstring displayTitle = emoji.empty() ? title : emoji + L" " + title;
	const std::wstring body = winrt::to_hstring(payload.body).c_str();
	return L"<toast launch=\"reveal\"><visual><binding template=\"ToastGeneric\"><text>" +
		escape_xml(displayTitle) + L"</text><text>" + escape_xml(body) +
		L"</text></binding></visual><audio silent=\"true\"/></toast>";
}

void reveal_file(const std::wstring &path)
{
	if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
		blog(LOG_WARNING, "%s cannot reveal missing file: %ls", kLogPrefix, path.c_str());
		return;
	}

	PIDLIST_ABSOLUTE filePidl = nullptr;
	const HRESULT parseResult = SHParseDisplayName(path.c_str(), nullptr, &filePidl, 0, nullptr);
	if (FAILED(parseResult) || !filePidl) {
		blog(LOG_WARNING, "%s failed to resolve Explorer item 0x%08lx: %ls", kLogPrefix,
			static_cast<unsigned long>(parseResult), path.c_str());
		return;
	}

	PIDLIST_ABSOLUTE folderPidl = ILClone(filePidl);
	PCUITEMID_CHILD fileItem = ILFindLastID(filePidl);
	if (!folderPidl || !fileItem || !ILRemoveLastID(folderPidl)) {
		blog(LOG_WARNING, "%s failed to resolve Explorer folder for: %ls", kLogPrefix, path.c_str());
		if (folderPidl)
			ILFree(folderPidl);
		CoTaskMemFree(filePidl);
		return;
	}

	const HRESULT openResult = SHOpenFolderAndSelectItems(folderPidl, 1, &fileItem, 0);
	if (FAILED(openResult))
		blog(LOG_WARNING, "%s failed to select Explorer item 0x%08lx: %ls", kLogPrefix,
			static_cast<unsigned long>(openResult), path.c_str());

	ILFree(folderPidl);
	CoTaskMemFree(filePidl);
}

} // namespace

struct WindowsNotificationBackend::Impl {
	struct ActiveToast {
		uint64_t id = 0;
		winrt::Windows::UI::Notifications::ToastNotification toast{nullptr};
		winrt::event_token activationToken{};
		winrt::event_token dismissedToken{};
		winrt::event_token failureToken{};
	};

	winrt::Windows::UI::Notifications::ToastNotifier notifier{nullptr};
	std::vector<std::unique_ptr<ActiveToast>> activeToasts;
	uint64_t nextToastId = 1;
	bool ownsRoInitialization = false;
	bool started = false;

	void remove_active_toast(uint64_t id, bool fromDismissedEvent = false)
	{
		const auto it = std::find_if(activeToasts.begin(), activeToasts.end(),
			[id](const auto &activeToast) { return activeToast && activeToast->id == id; });
		if (it == activeToasts.end())
			return;

		const auto &activeToast = *it;
		if (activeToast->activationToken.value != 0)
			activeToast->toast.Activated(activeToast->activationToken);
		if (!fromDismissedEvent && activeToast->dismissedToken.value != 0)
			activeToast->toast.Dismissed(activeToast->dismissedToken);
		if (activeToast->failureToken.value != 0)
			activeToast->toast.Failed(activeToast->failureToken);
		activeToasts.erase(it);
	}
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
		if (!ensure_notification_identity()) {
			if (impl_->ownsRoInitialization) {
				RoUninitialize();
				impl_->ownsRoInitialization = false;
			}
			return false;
		}

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

	while (!impl_->activeToasts.empty())
		impl_->remove_active_toast(impl_->activeToasts.back()->id);
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
		activeToast->id = impl_->nextToastId++;
		activeToast->toast = winrt::Windows::UI::Notifications::ToastNotification{document};
		if (payload.clickAction == ClickAction::RevealFile && payload.filePath) {
			const std::wstring path = payload.filePath->wstring();
			const uint64_t toastId = activeToast->id;
			activeToast->activationToken = activeToast->toast.Activated(
				[this, toastId, path](const auto &, const auto &) {
					blog(LOG_INFO, "%s notification activated for: %ls", kLogPrefix, path.c_str());
					reveal_file(path);
					impl_->remove_active_toast(toastId);
				});
			activeToast->dismissedToken = activeToast->toast.Dismissed(
				[this, toastId](const auto &, const auto &) {
					impl_->remove_active_toast(toastId, true);
				});
			activeToast->failureToken = activeToast->toast.Failed(
				[](const auto &, const auto &error) {
					blog(LOG_WARNING, "%s notification delivery failed: 0x%08lx", kLogPrefix,
						static_cast<unsigned long>(error.ErrorCode().value));
				});
		}

		impl_->notifier.Show(activeToast->toast);
		if (payload.clickAction != ClickAction::None) {
			while (impl_->activeToasts.size() >= kMaxActiveToasts)
				impl_->remove_active_toast(impl_->activeToasts.front()->id);
			impl_->activeToasts.push_back(std::move(activeToast));
		}
	} catch (const winrt::hresult_error &error) {
		blog(LOG_WARNING, "%s failed to show notification: 0x%08lx", kLogPrefix,
			static_cast<unsigned long>(error.code().value));
	}
}
