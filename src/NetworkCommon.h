#pragma once

#include <atomic>
#include <string>
#include <curl/curl.h>
#include <QString>
#include <obs-module.h>
#include <QtConcurrent/QtConcurrent>
#include <QThreadPool>
#include <exception>
#include <utility>

static size_t auth_curl_write_callback(void *contents, size_t size, size_t nmemb, void *userp)
{
	size_t realsize = size * nmemb;
	((std::string *)userp)->append((char *)contents, realsize);
	return realsize;
}

inline std::pair<long, QString> ExecuteNetworkRequest(const QString &url, const QString &method,
						      struct curl_slist *headers, const std::string &body = "",
						      bool verbose = false)
{
	CURL *curl = curl_easy_init();
	if (!curl)
		return {0, ""};

	long http_code = 0;
	std::string response;

	curl_easy_setopt(curl, CURLOPT_URL, url.toStdString().c_str());
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, auth_curl_write_callback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
	curl_easy_setopt(curl, CURLOPT_FAILONERROR, 0L);

	if (verbose) {
		curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
	}

	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

	if (method == "POST") {
		curl_easy_setopt(curl, CURLOPT_POST, 1L);
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
	} else if (method == "PATCH") {
		curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PATCH");
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
	}

	CURLcode res = curl_easy_perform(curl);
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

	curl_easy_cleanup(curl);

	if (res != CURLE_OK) {
		blog(LOG_ERROR, "[NetworkCommon] cURL error: %s", curl_easy_strerror(res));
		return {0, ""};
	}

	return {http_code, QString::fromStdString(response)};
}

// Lets a caller abort a running transfer, so shutting OBS down mid-download does not
// block on it.
struct DownloadAbortFlag {
	std::atomic_bool aborted{false};
};

static int download_progress_callback(void *userp, curl_off_t, curl_off_t, curl_off_t, curl_off_t)
{
	auto *flag = static_cast<DownloadAbortFlag *>(userp);
	// Any non-zero return makes libcurl abort the transfer.
	return (flag && flag->aborted.load()) ? 1 : 0;
}

// ExecuteNetworkRequest returns a QString, which would mangle binary content through
// UTF-8 conversion. Downloads therefore go straight to a file instead.
// Returns the HTTP status, or 0 when the transfer itself failed.
inline long DownloadToFile(const QString &url, const QString &targetPath, DownloadAbortFlag *abortFlag = nullptr,
			   long maxBytes = 64 * 1024 * 1024)
{
	FILE *file = nullptr;
	if (fopen_s(&file, targetPath.toLocal8Bit().constData(), "wb") != 0 || !file) {
		blog(LOG_ERROR, "[NetworkCommon] Cannot open %s for writing.", targetPath.toStdString().c_str());
		return 0;
	}

	CURL *curl = curl_easy_init();
	if (!curl) {
		fclose(file);
		return 0;
	}

	long http_code = 0;

	curl_easy_setopt(curl, CURLOPT_URL, url.toStdString().c_str());
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, nullptr);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, file);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	// GitHub redirects release assets to its CDN; anything beyond that is refused.
	curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
	curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "https");
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300L);
	curl_easy_setopt(curl, CURLOPT_MAXFILESIZE, maxBytes);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "obs-game-detector-update");
	curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);

	if (abortFlag) {
		curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, download_progress_callback);
		curl_easy_setopt(curl, CURLOPT_XFERINFODATA, abortFlag);
		curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
	}

	CURLcode res = curl_easy_perform(curl);
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

	curl_easy_cleanup(curl);
	fclose(file);

	if (res != CURLE_OK) {
		blog(LOG_ERROR, "[NetworkCommon] Download failed: %s", curl_easy_strerror(res));
		return 0;
	}

	return http_code;
}

template<typename Func>
auto RunTaskSafe(QThreadPool *pool, const char *context, Func &&func) -> QFuture<decltype(func())>
{
	using ReturnType = decltype(func());
	return QtConcurrent::run(pool, [func = std::forward<Func>(func), context]() mutable -> ReturnType {
		try {
			return func();
		} catch (const std::exception &e) {
			blog(LOG_ERROR, "[%s] Exception caught: %s", context, e.what());
			if constexpr (!std::is_void_v<ReturnType>) {
				return ReturnType{};
			}
		} catch (...) {
			blog(LOG_ERROR, "[%s] Unknown exception caught.", context);
			if constexpr (!std::is_void_v<ReturnType>) {
				return ReturnType{};
			}
		}
	});
}
