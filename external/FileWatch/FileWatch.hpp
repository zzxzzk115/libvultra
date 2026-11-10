//	MIT License
//	
//	Copyright(c) 2017 Thomas Monkman
//	
//	Permission is hereby granted, free of charge, to any person obtaining a copy
//	of this software and associated documentation files(the "Software"), to deal
//	in the Software without restriction, including without limitation the rights
//	to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//	copies of the Software, and to permit persons to whom the Software is
//	furnished to do so, subject to the following conditions :
//	
//	The above copyright notice and this permission notice shall be included in all
//	copies or substantial portions of the Software.
//	
//	THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//	IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//	FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
//	AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//	LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//	OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
//	SOFTWARE.

#ifndef FILEWATCHER_H
#define FILEWATCHER_H

#include <cstdio>
#include <cerrno>
#include <exception>
#include <expected>
#include <filesystem>
#include <memory>
#include <fstream>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <process.h>
#include <stdlib.h>
#include <stdio.h>
#include <tchar.h>
#include <Pathcch.h>
#include <shlwapi.h>
#endif // WIN32

#if __unix__
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/inotify.h>
#include <poll.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <unistd.h>
#endif // __unix__

#ifdef __linux__
#include <linux/limits.h>
#endif

#if defined(__APPLE__) || defined(__MACH__)
#include <CoreFoundation/CoreFoundation.h>
#include <CoreServices/CoreServices.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#define FILEWATCH_PLATFORM_MAC 1
#endif

#if __unix__ || FILEWATCH_PLATFORM_MAC
#include <pthread.h>
#endif

#include <functional>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <utility>
#include <vector>
#include <array>
#include <unordered_map>
#include <unordered_set>
#include <system_error>
#include <string>
#include <algorithm>
#include <type_traits>
#include <future>
#include <regex>
#include <cstddef>
#include <cstring>
#include <cwchar>
#include <cassert>
#include <cstdlib>
#include <iostream>

#ifdef FILEWATCH_PLATFORM_MAC
extern "C" int __getdirentries64(int, char *, int, long *);
#endif // FILEWATCH_PLATFORM_MAC

namespace filewatch {
	enum class Event {
		added,
		removed,
		modified,
		renamed_old,
		renamed_new
	};
      
      template<typename StringType>
      struct IsWChar {
            static constexpr bool value = false;
      };

      template<> 
      struct IsWChar<wchar_t> {
            static constexpr bool value = true;
      };

      template<typename Fn, typename... Args>
      struct Invokable {
            static Fn make() {
                  return (Fn*)0;
            }

            template<typename T>
            static T defaultValue() {
                  return *(T*)0;
            }

            static void call(int) {
                  make()(defaultValue<Args...>());
            }

            static int call(long value);

            static constexpr bool value = std::is_same<decltype(call(0)), int>::value;
      };

#define _FILEWATCH_TO_STRING(x) #x
#define FILEWATCH_TO_STRING(x) _FILEWATCH_TO_STRING(x)

      [[maybe_unused]] static const char* event_to_string(Event event) {
            switch (event) {
            case Event::added:
                  return FILEWATCH_TO_STRING(Event::added);
            case Event::removed:
                  return FILEWATCH_TO_STRING(Event::removed);
            case Event::modified:
                  return FILEWATCH_TO_STRING(Event::modified);
            case Event::renamed_old:
                  return FILEWATCH_TO_STRING(Event:renamed_old);
            case Event::renamed_new:
                  return FILEWATCH_TO_STRING(Event::renamed_new);
            }
            assert(false);
      }

      template<typename StringType>
      static typename std::enable_if<std::is_same<typename StringType::value_type, wchar_t>::value, bool>::type 
      isParentOrSelfDirectory(const StringType& path) {
            return path == L"." || path == L"..";
      }

      template<typename StringType>
      static typename std::enable_if<std::is_same<typename StringType::value_type, char>::value, bool>::type 
      isParentOrSelfDirectory(const StringType& path) {
            return path == "." || path == "..";
      }

	/**
	* \class FileWatch
	*
	* \brief Watches a folder or file, and will notify of changes via function callback.
	*
	* \author Thomas Monkman
	*
	*/
	template<class StringType>
	class FileWatch
	{
		typedef typename StringType::value_type C;
		typedef std::basic_string<C, std::char_traits<C>> UnderpinningString;
		typedef std::basic_regex<C, std::regex_traits<C>> UnderpinningRegex;

	public:


        using ExpectedCallback = std::function<std::expected<void, std::string>(const StringType&, Event)>;

        static std::expected<std::unique_ptr<FileWatch>, std::string>
        createExpected(StringType path, ExpectedCallback callback)
        {
            if (!callback) {
                return std::unexpected("File watcher callback is empty");
            }
            auto watcher = std::unique_ptr<FileWatch>(new FileWatch(Uninitialized{}));
            watcher->_callback = std::move(callback);
            if (auto ready = watcher->initializeExpected(path); !ready) {
                return std::unexpected(ready.error());
            }
            return watcher;
        }

        std::expected<void, std::string> statusExpected() const
        {
            if (!_failed.load()) { return {}; }
            std::lock_guard<std::mutex> lock(_error_mutex);
            if (!_error.empty()) {
                return std::unexpected(_error);
            }
            return {};
        }

        FileWatch(StringType path, UnderpinningRegex pattern,
                  std::function<void(const StringType&, Event)> callback) : FileWatch(Uninitialized{})
        {
            _pattern = std::move(pattern);
            // Only the legacy callback boundary retains upstream exception swallowing.
            _callback = [callback = std::move(callback)](const StringType& file, Event event)
                -> std::expected<void, std::string> {
                try {
                    if (callback) { callback(file, event); }
                } catch (const std::exception&) {}
                return {};
            };
            if (auto ready = initializeExpected(path); !ready) {
                throw std::runtime_error(ready.error());
            }
        }

        FileWatch(StringType path, std::function<void(const StringType&, Event)> callback) :
            FileWatch(std::move(path), UnderpinningRegex(_regex_all), std::move(callback)) {}

        ~FileWatch() { destroy(); }

        FileWatch(const FileWatch& other) : FileWatch(Uninitialized{})
        {
            _pattern = other._pattern;
            _callback = other._callback;
            if (auto ready = initializeExpected(other._input_path); !ready) {
                throw std::runtime_error(ready.error());
            }
        }

        FileWatch& operator=(const FileWatch& other)
        {
            if (this != &other) {
                destroy();
                _pattern = other._pattern;
                _callback = other._callback;
                if (auto ready = initializeExpected(other._input_path); !ready) {
                    throw std::runtime_error(ready.error());
                }
            }
            return *this;
        }

		// Const memeber varibles don't let me implent moves nicely, if moves are really wanted std::unique_ptr should be used and move that.
		FileWatch(FileWatch<StringType>&&) = delete;
		FileWatch<StringType>& operator=(FileWatch<StringType>&&) & = delete;

	private:
		static constexpr C _regex_all[] = { '.', '*', '\0' };
		static constexpr C _this_directory[] = { '.', '/', '\0' };

		struct PathParts
		{
			PathParts(StringType directory, StringType filename) : directory(directory), filename(filename) {}
			StringType directory;
			StringType filename;
		};
		StringType _path;
        StringType _input_path;

		UnderpinningRegex _pattern;

		static constexpr std::size_t _buffer_size = { 1024 * 256 };

		// only used if watch a single file
		StringType _filename;

		ExpectedCallback _callback;

        // Native thread creation has a returned error, unlike std::thread's throwing constructor.
        struct NativeThread {
            FileWatch* owner = nullptr;
            void (*function)(FileWatch*) = nullptr;
            bool running = false;
#ifdef _WIN32
            HANDLE handle = nullptr;
            static unsigned __stdcall enter(void* context) {
                auto* thread = static_cast<NativeThread*>(context);
                thread->function(thread->owner);
                return 0;
            }
#else
            pthread_t handle{};
            static void* enter(void* context) {
                auto* thread = static_cast<NativeThread*>(context);
                thread->function(thread->owner);
                return nullptr;
            }
#endif
            std::expected<void, std::string> start(FileWatch* watch, void (*entry)(FileWatch*)) {
                owner = watch;
                function = entry;
#ifdef _WIN32
                handle = reinterpret_cast<HANDLE>(_beginthreadex(nullptr, 0, enter, this, 0, nullptr));
                if (!handle) {
                    return std::unexpected("Create watch thread: " + std::error_code(errno, std::generic_category()).message());
                }
#else
                const auto status = pthread_create(&handle, nullptr, enter, this);
                if (status != 0) {
                    return std::unexpected("Create watch thread: " + std::error_code(status, std::generic_category()).message());
                }
#endif
                running = true;
                return {};
            }
            bool joinable() const { return running; }
            void join() {
                if (!running) { return; }
#ifdef _WIN32
                const auto status = WaitForSingleObject(handle, INFINITE);
                // Joining a live, uniquely owned handle is an internal invariant.
                if (status != WAIT_OBJECT_0) { std::terminate(); }
                CloseHandle(handle);
                handle = nullptr;
#else
                if (pthread_join(handle, nullptr) != 0) { std::terminate(); }
#endif
                running = false;
            }
        };

        NativeThread _watch_thread;

		std::condition_variable _cv;
		std::mutex _callback_mutex;
		std::vector<std::pair<StringType, Event>> _callback_information;
        NativeThread _callback_thread;

        std::promise<std::expected<void, std::string>> _running;
        std::atomic<bool> _ready = false;
        mutable std::mutex _error_mutex;
        std::string _error;
        std::atomic<bool> _failed = false;
		std::atomic<bool> _destory = { false };
		bool _watching_single_file = { false };

#pragma mark "Platform specific data"
#ifdef _WIN32
		HANDLE _directory = { nullptr };
		HANDLE _close_event = { nullptr };

		const DWORD _listen_filters =
			FILE_NOTIFY_CHANGE_SECURITY |
			FILE_NOTIFY_CHANGE_CREATION |
			FILE_NOTIFY_CHANGE_LAST_WRITE |
			FILE_NOTIFY_CHANGE_SIZE |
			FILE_NOTIFY_CHANGE_ATTRIBUTES |
			FILE_NOTIFY_CHANGE_DIR_NAME |
			FILE_NOTIFY_CHANGE_FILE_NAME;

		const std::unordered_map<DWORD, Event> _event_type_mapping = {
			{ FILE_ACTION_ADDED, Event::added },
			{ FILE_ACTION_REMOVED, Event::removed },
			{ FILE_ACTION_MODIFIED, Event::modified },
			{ FILE_ACTION_RENAMED_OLD_NAME, Event::renamed_old },
			{ FILE_ACTION_RENAMED_NEW_NAME, Event::renamed_new }
		};
#endif // WIN32

#if __unix__
		struct FolderInfo {
			int folder;
			int watch;
		};

		FolderInfo _directory{-1, -1};

		const std::uint32_t _listen_filters = IN_MODIFY | IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO;

		const static std::size_t event_size = (sizeof(struct inotify_event));
#endif // __unix__

#if FILEWATCH_PLATFORM_MAC
            struct FileState 
            {
                  int fd;
                  uint32_t nlink;
                  time_t last_modification;

                  FileState(int fd, uint32_t nlink, time_t lt) 
                        : fd(fd), nlink(nlink), 
                        last_modification(lt)
                  {

                  }
                  FileState(const FileState&) = delete;
                  FileState& operator=(const FileState&) = delete;
                  FileState(FileState&& other) : fd(other.fd), nlink(other.nlink), last_modification(other.last_modification)
                  {
                        other.fd = -1;
                  }

                  FileState invalidate_and_clone() {
                        int fd = this->fd;

                        this->fd = -1;
                        return FileState {fd, nlink, last_modification};
                  }

                  ~FileState() 
                  {
                        if (fd != -1) {
                              close(fd);
                        }
                  }
            };
            std::unordered_map<StringType, FileState> _directory_snapshot{};
            bool _previous_event_is_rename = false;
            std::atomic<CFRunLoopRef> _run_loop{nullptr};
            int _file_fd = -1;
            struct timespec _last_modification_time = {};
            FSEventStreamRef _directory = nullptr;
            bool _stream_started = false;
            // fd for single file
#endif // FILEWATCH_PLATFORM_MAC


        struct Uninitialized {};
        explicit FileWatch(Uninitialized) : _pattern(_regex_all) {}

        void signalReady(std::expected<void, std::string> result)
        {
            if (!_ready.exchange(true)) {
                _running.set_value(std::move(result));
            }
        }

        void wakeForStop()
        {
#ifdef _WIN32
            if (_close_event) { SetEvent(_close_event); }
#elif FILEWATCH_PLATFORM_MAC
            if (auto loop = _run_loop.load()) { CFRunLoopStop(loop); }
#endif
            _cv.notify_all();
        }

        void fail(std::string error)
        {
            {
                std::lock_guard<std::mutex> lock(_error_mutex);
                if (_error.empty()) { _error = error; }
            }
            _failed = true;
            signalReady(std::unexpected(std::move(error)));
            _destory = true;
            wakeForStop();
        }

        std::expected<void, std::string> initializeExpected(const StringType& path)
        {
            if (path.empty() || path.find(C(0)) != StringType::npos) {
                return std::unexpected("File watcher requires a nonempty path without NUL characters");
            }
            std::error_code error;
            const auto resolved = std::filesystem::canonical(std::filesystem::path(path), error);
            if (error) { return std::unexpected("Resolve watch path: " + error.message()); }
            const auto status = std::filesystem::status(resolved, error);
            if (error) { return std::unexpected("Inspect watch path: " + error.message()); }
            _watching_single_file = std::filesystem::is_regular_file(status);
            if (!_watching_single_file && !std::filesystem::is_directory(status)) {
                return std::unexpected("File watcher requires a directory or regular file");
            }
            _input_path = resolved.template string<C>();
            _filename = _watching_single_file ? resolved.filename().template string<C>() : StringType{};
            _path = (_watching_single_file ? resolved.parent_path() : resolved).template string<C>();
            _destory = false;
            _ready = false;
            _error.clear();
            _failed = false;
            _running = std::promise<std::expected<void, std::string>>();
            auto directory = get_directory_expected(_path);
            if (!directory) {
                destroy();
                return std::unexpected(directory.error());
            }
            _directory = *directory;
#ifdef _WIN32
            _close_event = CreateEvent(NULL, TRUE, FALSE, NULL);
            if (!_close_event) {
                const auto message = std::error_code(GetLastError(), std::system_category()).message();
                destroy();
                return std::unexpected("Create watch stop event: " + message);
            }
#elif FILEWATCH_PLATFORM_MAC
            if (_watching_single_file) {
                _file_fd = open(_input_path.c_str(), O_RDONLY);
                struct stat state{};
                if (_file_fd < 0 || fstat(_file_fd, &state) != 0) {
                    const auto message = std::error_code(errno, std::system_category()).message();
                    destroy();
                    return std::unexpected("Open watched file: " + message);
                }
                _last_modification_time = state.st_mtimespec;
            }
#endif
            auto future = _running.get_future();
            auto callback = _callback_thread.start(this, [](FileWatch* self) { self->callback_thread(); });
            if (!callback) {
                destroy();
                return callback;
            }
            auto watch = _watch_thread.start(this, [](FileWatch* self) {
                if (auto monitored = self->monitor_directory(); !monitored) { self->fail(monitored.error()); }
            });
            if (!watch) {
                destroy();
                return watch;
            }
            auto started = future.get();
            if (!started) { destroy(); }
            return started;
        }

        void destroy()
        {
            _destory = true;
            wakeForStop();
#if __unix__
            if (_directory.folder >= 0 && _directory.watch >= 0) {
                inotify_rm_watch(_directory.folder, _directory.watch);
            }
#endif
            if (_watch_thread.joinable()) { _watch_thread.join(); }
            if (_callback_thread.joinable()) { _callback_thread.join(); }
#ifdef _WIN32
            if (_directory && _directory != INVALID_HANDLE_VALUE) { CloseHandle(_directory); }
            if (_close_event) { CloseHandle(_close_event); }
            _directory = nullptr;
            _close_event = nullptr;
#elif __unix__
            if (_directory.folder >= 0) { close(_directory.folder); }
            _directory = {-1, -1};
#elif FILEWATCH_PLATFORM_MAC
            if (_directory) {
                if (_stream_started) { FSEventStreamStop(_directory); }
                _stream_started = false;
                FSEventStreamInvalidate(_directory);
                FSEventStreamRelease(_directory);
                _directory = nullptr;
            }
            if (auto loop = _run_loop.exchange(nullptr)) { CFRelease(loop); }
            if (_file_fd >= 0) { close(_file_fd); _file_fd = -1; }
            _directory_snapshot.clear();
#endif
            _callback_information.clear();
        }

		const PathParts split_directory_and_file(const StringType& path) const 
		{
			const auto predict = [](C character) {
#ifdef _WIN32
				return character == C('\\') || character == C('/');
#elif __unix__ || FILEWATCH_PLATFORM_MAC
				return character == C('/');
#endif // __unix__
			};

			UnderpinningString path_string = path;
			const auto pivot = std::find_if(path_string.rbegin(), path_string.rend(), predict).base();
			//if the path is something like "test.txt" there will be no directory part, however we still need one, so insert './'
			const StringType directory = [&]() {
				const auto extracted_directory = UnderpinningString(path_string.begin(), pivot);
				return (extracted_directory.size() > 0) ? extracted_directory : UnderpinningString(_this_directory);
			}();
			const StringType filename = UnderpinningString(pivot, path_string.end());
			return PathParts(directory, filename);
		}

		bool pass_filter(const UnderpinningString& file_path)
		{ 
			if (_watching_single_file) {
				const UnderpinningString extracted_filename = { split_directory_and_file(file_path).filename };
				//if we are watching a single file, only that file should trigger action
				return extracted_filename == _filename;
			}
			return std::regex_match(file_path, _pattern);
		}

#ifdef _WIN32
		template<typename... Args> DWORD GetFileAttributesX(const char* lpFileName, Args... args) {
			return GetFileAttributesA(lpFileName, args...);
		}
		template<typename... Args> DWORD GetFileAttributesX(const wchar_t* lpFileName, Args... args) {
			return GetFileAttributesW(lpFileName, args...);
		}

		template<typename... Args> HANDLE CreateFileX(const char* lpFileName, Args... args) {
			return CreateFileA(lpFileName, args...);
		}
		template<typename... Args> HANDLE CreateFileX(const wchar_t* lpFileName, Args... args) {
			return CreateFileW(lpFileName, args...);
		}


        std::expected<HANDLE, std::string> get_directory_expected(const StringType& path)
        {
            HANDLE directory = CreateFileX(path.c_str(), FILE_LIST_DIRECTORY,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, HANDLE(0));
            if (directory == INVALID_HANDLE_VALUE) {
                return std::unexpected("Open watched directory: " +
                    std::error_code(GetLastError(), std::system_category()).message());
            }
            return directory;
        }

		void convert_wstring(const std::wstring& wstr, std::string& out)
		{
			int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
			out.resize(size_needed, '\0');
			WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &out[0], size_needed, NULL, NULL);
		}

		void convert_wstring(const std::wstring& wstr, std::wstring& out)
		{
			out = wstr;
		}

		std::expected<void, std::string> monitor_directory()
		{
			std::vector<BYTE> buffer(_buffer_size);
			DWORD bytes_returned = 0;
			OVERLAPPED overlapped_buffer{ 0 };

			overlapped_buffer.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
			if (!overlapped_buffer.hEvent) {
				return std::unexpected("Create watch monitor event: " + std::error_code(GetLastError(), std::system_category()).message());
			}

			std::array<HANDLE, 2> handles{ overlapped_buffer.hEvent, _close_event };

			auto async_pending = false;
            std::expected<void, std::string> result;
			do {
				std::vector<std::pair<StringType, Event>> parsed_information;
				const auto requested = ReadDirectoryChangesW(
					_directory,
					buffer.data(), static_cast<DWORD>(buffer.size()),
					TRUE,
					_listen_filters,
					&bytes_returned,
					&overlapped_buffer, NULL);
                if (!requested) {
                    result = std::unexpected("Request directory changes: " + std::error_code(GetLastError(), std::system_category()).message());
                    break;
                }
                signalReady({});
			
				async_pending = true;
			
				switch (WaitForMultipleObjects(2, handles.data(), FALSE, INFINITE))
				{
				case WAIT_OBJECT_0:
				{
					if (!GetOverlappedResult(_directory, &overlapped_buffer, &bytes_returned, TRUE)) {
						result = std::unexpected("Read directory changes: " + std::error_code(GetLastError(), std::system_category()).message());
                        break;
					}
					async_pending = false;

					if (bytes_returned == 0) {
						break;
					}

					FILE_NOTIFY_INFORMATION *file_information = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(&buffer[0]);
					do
					{
						std::wstring changed_file_w{ file_information->FileName, file_information->FileNameLength / sizeof(file_information->FileName[0]) };
						UnderpinningString changed_file;
						convert_wstring(changed_file_w, changed_file);
						if (pass_filter(changed_file))
						{
							parsed_information.emplace_back(StringType{ changed_file }, _event_type_mapping.at(file_information->Action));
						}

						if (file_information->NextEntryOffset == 0) {
							break;
						}

						file_information = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(reinterpret_cast<BYTE*>(file_information) + file_information->NextEntryOffset);
					} while (true);
					break;
				}
				case WAIT_OBJECT_0 + 1:
					// quit
					break;
				case WAIT_FAILED:
                    result = std::unexpected("Wait for directory changes: " + std::error_code(GetLastError(), std::system_category()).message());
                    break;
				}
				//dispatch callbacks
				{
					std::lock_guard<std::mutex> lock(_callback_mutex);
					_callback_information.insert(_callback_information.end(), parsed_information.begin(), parsed_information.end());
				}
				_cv.notify_all();
			} while (_destory == false && result);

			if (async_pending)
			{
				//clean up running async io
				CancelIo(_directory);
				GetOverlappedResult(_directory, &overlapped_buffer, &bytes_returned, TRUE);
			}
            CloseHandle(overlapped_buffer.hEvent);
            return result;
		}
#endif // WIN32

#if __unix__


        std::expected<FolderInfo, std::string> get_directory_expected(const StringType& path)
        {
            const auto folder = inotify_init();
            if (folder < 0) {
                return std::unexpected("Create inotify: " + std::error_code(errno, std::system_category()).message());
            }
            const auto watch = inotify_add_watch(folder, path.c_str(), _listen_filters);
            if (watch < 0) {
                const auto message = std::error_code(errno, std::system_category()).message();
                close(folder);
                return std::unexpected("Watch inotify directory: " + message);
            }
            return FolderInfo{folder, watch};
        }

		std::expected<void, std::string> monitor_directory()
		{
			std::vector<char> buffer(_buffer_size);

			signalReady({});
			while (_destory == false) 
			{
				// A removed directory has no watch left to wake read() during destruction.
				pollfd descriptor{_directory.folder, POLLIN, 0};
				const auto ready = poll(&descriptor, 1, 100);
				if (ready < 0 && errno != EINTR) {
					return std::unexpected("Poll watched directory: " + std::error_code(errno, std::system_category()).message());
				}
				if (ready <= 0 || _destory) {
					continue;
				}
				const auto length = read(_directory.folder, static_cast<void*>(buffer.data()), buffer.size());
                if (length < 0 && errno != EINTR) {
                    return std::unexpected("Read watched directory: " + std::error_code(errno, std::system_category()).message());
                }
				if (length > 0) 
				{
					int i = 0;
					std::vector<std::pair<StringType, Event>> parsed_information;
					while (i < length) 
					{
						struct inotify_event *event = reinterpret_cast<struct inotify_event *>(&buffer[i]); // NOLINT
                        if (event->mask & IN_IGNORED) {
                            if (_destory) { return {}; }
                            return std::unexpected("Inotify watch was removed");
                        }
						if (event->len) 
						{
							const UnderpinningString changed_file{ event->name };
							if (pass_filter(changed_file))
							{
								if (event->mask & IN_CREATE) 
								{
									parsed_information.emplace_back(StringType{ changed_file }, Event::added);
								}
								else if (event->mask & IN_DELETE) 
								{
									parsed_information.emplace_back(StringType{ changed_file }, Event::removed);
								}
								else if (event->mask & IN_MOVED_FROM)
								{
									parsed_information.emplace_back(StringType{ changed_file }, Event::renamed_old);
								}
								else if (event->mask & IN_MOVED_TO)
								{
									parsed_information.emplace_back(StringType{ changed_file }, Event::renamed_new);
								}
								else if (event->mask & IN_MODIFY) 
								{
									parsed_information.emplace_back(StringType{ changed_file }, Event::modified);
								}
							}
						}
						i += event_size + event->len;
					}
					//dispatch callbacks
					{
						std::lock_guard<std::mutex> lock(_callback_mutex);
						_callback_information.insert(_callback_information.end(), parsed_information.begin(), parsed_information.end());
					}
					_cv.notify_all();
				}
			}
            return {};
		}
#endif // __unix__

#if FILEWATCH_PLATFORM_MAC
            static StringType utf8StringToUtf32String(const char* buffer) {
                  mbstate_t state{};
                  StringType s{};

                  size_t needed = mbsrtowcs(nullptr, &buffer, 0, &state) + 1;
                  s.reserve(needed);
                  mbsrtowcs((wchar_t*)&s[0], &buffer, s.size(), &state);
                  return s;
            }

            template<typename Fn, class = std::enable_if<Invokable<Fn, StringType>::value>>
            static void walkDirectory(const StringType& path, Fn callback) {
                  int fd = open(path.c_str(), O_RDONLY);
                  char buf[1024];
                  long basep = 0;

                  if (fd == -1) {
                        return;
                  }

                  int ret = __getdirentries64(fd, buf, sizeof(buf), &basep);

                  while (ret > 0) {
                        char* current = buf;
                        int offset = 0;

                        while (offset < ret) {
                              struct dirent* dirent = (struct dirent*)current;
                              StringType name = IsWChar<C>::value? 
                                    utf8StringToUtf32String(dirent->d_name) 
                                    : StringType(dirent->d_name);
                              
                              callback(std::move(name));
                              current += dirent->d_reclen;
                              offset += dirent->d_reclen;
                        }
                        ret = __getdirentries64(fd, buf, sizeof(buf), &basep);
                  }
                  close(fd);
            }

            static StringType nameofFd(int fd) {
                  size_t len = 0;
                  char buf[MAXPATHLEN];

                  if (fcntl(fd, F_GETPATH, buf) == -1) {
                        return StringType{};
                  }
                  if (IsWChar<C>::value) {
                        return utf8StringToUtf32String(buf);
                  }

                  len = strnlen(buf, MAXPATHLEN);
                  for (int i = len - 1; i >= 0; i--) {
                        if(buf[i] == '/') {
                              return StringType{buf + i + 1, len - i - 1};
                        }
                  }
                  return StringType{buf, len};
            }

            static StringType fullPathOfFd(int fd) {
                  char buf[MAXPATHLEN];

                  if (fcntl(fd, F_GETPATH, buf) == -1) {
                        return StringType{};
                  }
                  if (IsWChar<C>::value) {
                        return utf8StringToUtf32String(buf);
                  }
                  return StringType{(C*)buf};
            }

            static StringType pathOfFd(int fd) {
                  size_t len = 0;
                  char buf[MAXPATHLEN];

                  if (fcntl(fd, F_GETPATH, buf) == -1) {
                        return StringType{};
                  }
                  if (IsWChar<C>::value) {
                        return utf8StringToUtf32String(buf);
                  }

                  len = strnlen(buf, MAXPATHLEN);
                  for (int i = len - 1; i >= 0; i--) {
                        if(buf[i] == '/') {
                              return StringType{buf, static_cast<size_t>(i)};
                        }
                  }
                  return StringType{buf, len};
            }

            static bool fdIsRemoved(int fd) {
                  char buf[MAXPATHLEN];
                  return fcntl(fd, F_GETPATH, buf) == -1;
            }

            FileState makeFileState(const StringType& path) {
                  // An atomic-save temporary may disappear before its queued event is delivered.
                  int fd = open(fullPathOf(path).c_str(), O_RDONLY);
                  struct stat stat{};
                  if (fd == -1 || fstat(fd, &stat) == -1) {
                        if (fd != -1) close(fd);
                        return FileState {-1, 0, 0};
                  }
                  // Transfer this descriptor; opening again leaked one fd for every snapshot entry.
                  return FileState {fd, static_cast<uint32_t>(stat.st_nlink), stat.st_mtimespec.tv_sec};
            }

            static StringType filenameOf(const StringType& file) {
                  for (int i = file.size() - 1; i >= 0; i--) {
                        if(file[i] == '/') {
                              return file.substr(i + 1);
                        }
                  }
                  return file;
            }

            static bool isInDirectory(const StringType& file, const StringType& path) {
                  if (file.size() < path.size()) {
                        return false;
                  }
                  return strncmp(file.data(), path.data(), path.size()) == 0;
            }

            PathParts splitPath(const StringType& path) {
                  PathParts split = split_directory_and_file(path);

                  if (split.directory.size() > 0 && split.directory[split.directory.size() - 1] == '/') {
                              split.directory.erase(split.directory.size() - 1);
                  }
                  return split;
            }

            StringType fullPathOf(const StringType& file) {
                  return _path + '/' + file;
            }

            int openFile(const StringType& file) {
                  int fd = open(fullPathOf(file).c_str(), O_RDONLY);
                  assert(fd != -1);
                  return fd;
            }

            void walkAndSeeChanges() {
                  struct RenamedPair {
                        StringType old;
                        StringType current;
                  };
                  struct EventInfo {
                        StringType file;
                        struct timespec time;
                        Event event;
                  };
                  std::unordered_map<StringType, FileState> newSnapshot{};
                  std::vector<EventInfo> events{};

                  for (auto& entry : _directory_snapshot) {
                        struct stat stat;

                        fstat(entry.second.fd, &stat);
                        if (fdIsRemoved(entry.second.fd)) {
                              events.push_back(EventInfo {
                                    .event = Event::removed,
                                    .file = entry.first,
                                    .time = stat.st_ctimespec
                              });
                              continue;
                        }

                        StringType fullPath = fullPathOfFd(entry.second.fd);
                        PathParts pathPair = splitPath(fullPath);

                        if (pathPair.directory != _path) {
                              events.push_back(EventInfo {
                                    .event = Event::removed,
                                    .file = entry.first,
                                    .time = stat.st_ctimespec
                              });
                              continue;
                        }
                        if (entry.first != pathPair.filename) {
                              events.push_back(EventInfo {
                                    .event = Event::renamed_old,
                                    .file = entry.first,
                                    .time = stat.st_ctimespec
                              });
                              events.push_back(EventInfo {
                                    .event = Event::renamed_new,
                                    .file = pathPair.filename,
                                    .time = stat.st_ctimespec
                              });
                        }
                        else {
                              if (stat.st_mtimespec.tv_sec > entry.second.last_modification) {
                                    entry.second.last_modification = stat.st_mtimespec.tv_sec;
                                    events.push_back(EventInfo {
                                          .event = Event::modified,
                                          .file = pathPair.filename,
                                          .time = stat.st_mtimespec
                                    });
                              }
                        }
                        newSnapshot.insert(std::make_pair(std::move(pathPair.filename), 
                              std::move(entry.second.invalidate_and_clone())));
                  }

                  walkDirectory(_path, [&](StringType file) {
                        if (isParentOrSelfDirectory(file) || !std::regex_match(file, _pattern)) {
                              return;
                        }
                        if (newSnapshot.count(file) == 0) {
                              FileState state = makeFileState(file);
                              if (state.fd == -1) return;
                              struct stat stat{};
                              
                              fstat(state.fd, &stat);
                              events.push_back(EventInfo {
                                    .event = Event::added,
                                    .file = file,
                                    .time = stat.st_mtimespec
                              });
                              newSnapshot.insert(std::make_pair(file, std::move(state)));
                        }
                  });

                  std::swap(_directory_snapshot, newSnapshot);

                  std::sort(events.begin(), events.end(), [] (const EventInfo& a, EventInfo& b) {
                        if (a.time.tv_sec == b.time.tv_sec) {
                              return a.time.tv_nsec < b.time.tv_nsec;
                        }
                        return a.time.tv_sec < b.time.tv_sec;
                  });

                  {
                        std::lock_guard<std::mutex> lock(_callback_mutex);
                        
                        for (const auto& event : events) {
                              _callback_information.push_back(std::make_pair(event.file, event.event));
                        }
                  }
                  _cv.notify_all();
            }

            void seeSingleFileChanges() {
                  struct EventInfo {
                        StringType file;
                        Event event;
                  };

                  int eventCount = 1;
                  EventInfo eventInfos[2];

                  if (fdIsRemoved(_file_fd)) {
                        eventInfos[0].event = Event::removed;
                        eventInfos[0].file = _filename;
                  }
                  else {
                        StringType absPath = pathOfFd(_file_fd);
                        PathParts split = splitPath(absPath);

                        if (split.directory != _path) {
                              eventInfos[0].event = Event::removed;
                              eventInfos[0].file = _filename;
                        }
                        else if (split.filename != _filename) {
                              eventInfos[0].event = Event::renamed_old;
                              eventInfos[0].file = std::move(_filename);
                              eventInfos[1].event = Event::renamed_new;
                              eventInfos[1].file = split.filename;
                              eventCount = 2;
                              _filename = std::move(split.filename);
                        }
                        else {
                              struct stat stat;

                              fstat(_file_fd, &stat);

                              if (stat.st_mtimespec.tv_sec > _last_modification_time.tv_sec) {
                                    eventInfos[0].event = Event::modified;
                                    eventInfos[0].file = _filename;
                                    _last_modification_time = stat.st_mtimespec;
                              }
                              else if (stat.st_mtimespec.tv_nsec > _last_modification_time.tv_nsec) {
                                    eventInfos[0].event = Event::modified;
                                    eventInfos[0].file = _filename;
                                    _last_modification_time = stat.st_mtimespec;
                              }
                              else {
                                    return;
                              }
                        }
                  }

                  {
                        std::lock_guard<std::mutex> lock(_callback_mutex);
                        for (int i = 0; i < eventCount; i++) {
                              _callback_information.push_back(
                                    std::make_pair(eventInfos[i].file, eventInfos[i].event));
                        }
                  }
                  _cv.notify_all();
            }

            void notify(CFStringRef path, const FSEventStreamEventFlags flags) {
                  CFIndex pathLength = CFStringGetLength(path);
                  CFIndex written = 0;
                  char buffer[PATH_MAX + 1];

                  CFStringGetBytes(path, 
                        CFRange {
                              .location = 0,
                              .length = pathLength,
                        }, 
                        IsWChar<C>::value? kCFStringEncodingUTF32 : kCFStringEncodingUTF8, 
                        0, 
                        false, 
                        (UInt8*)buffer, 
                        PATH_MAX, 
                        &written);
                  
                  buffer[written] = 0;

                  StringType absolutePath{(const C*)buffer, static_cast<size_t>(pathLength)};
                  PathParts pathPair = splitPath(absolutePath);

                  if (_watching_single_file && pathPair.filename != _filename) {
                        return;
                  }
                  if (pathPair.directory != _path || !std::regex_match(pathPair.filename, _pattern)) {
                        return;
                  }

                  Event event = Event::modified;
                  if (_previous_event_is_rename) {
                        event = Event::renamed_new;
                        FileState state = makeFileState(pathPair.filename);
                        if (state.fd != -1) {
                              _directory_snapshot.insert(std::make_pair(pathPair.filename, std::move(state)));
                        }
                        _previous_event_is_rename = false;
                  }
                  else if (flags & kFSEventStreamEventFlagItemRenamed) {
                        const auto state = _directory_snapshot.find(pathPair.filename);
                        if (state == _directory_snapshot.end()) {
                              // FSEvents may coalesce create/rename/remove for an atomic-save temporary.
                              // Reconcile actual files instead of dereferencing an absent snapshot entry.
                              walkAndSeeChanges();
                              return;
                        }
                        StringType fdPath = pathOfFd(state->second.fd);

                        // moved/delete to Trash folder
                        if (!isInDirectory(absolutePath, fdPath)) {
                              event = Event::removed;
                              _directory_snapshot.erase(pathPair.filename);
                        }
                        else {
                              event = Event::renamed_old;
                              _previous_event_is_rename = true;
                        }
                  }
                  else if (flags & kFSEventStreamEventFlagItemCreated) {
                        FileState state = makeFileState(pathPair.filename);
                        if (state.fd != -1) {
                              _directory_snapshot.insert(std::make_pair(pathPair.filename, std::move(state)));
                        }
                        event = Event::added;
                  }
                  else if (flags & kFSEventStreamEventFlagItemRemoved) {
                        _directory_snapshot.erase(pathPair.filename);
                        event = Event::removed;
                  }

                  {
                        std::lock_guard<std::mutex> lock(_callback_mutex);
                        _callback_information.push_back(std::make_pair(std::move(pathPair.filename), event));
                  }
                  _cv.notify_all();
            }

            static void handleFsEvent(__attribute__((unused)) ConstFSEventStreamRef streamFef, 
                                          void* clientCallBackInfo, 
                                          size_t numEvents, 
                                          CFArrayRef eventPaths, 
                                          const FSEventStreamEventFlags* eventFlags, 
                                          __attribute__((unused)) const FSEventStreamEventId* eventIds) {
                  FileWatch<StringType>* self = (FileWatch<StringType>*)clientCallBackInfo;

                  for (size_t i = 0; i < numEvents; i++) {
                        FSEventStreamEventFlags flag = eventFlags[i];
                        CFStringRef path = (CFStringRef)CFArrayGetValueAtIndex(eventPaths, i);

                        if (self->_watching_single_file) {
                              self->seeSingleFileChanges();
                        }
                        else if (flag & kFSEventStreamEventFlagMustScanSubDirs) {
                                    self->walkAndSeeChanges();
                        }
                        else {
                              self->notify(path, flag);
                        }
                  }
            }

            FSEventStreamRef openStream(const StringType& directory) {
                  CFStringEncoding encoding = IsWChar<C>::value? 
                        kCFStringEncodingUTF32 : kCFStringEncodingUTF8;
                  CFStringRef path = CFStringCreateWithBytes(kCFAllocatorDefault, 
                                          (const UInt8*)directory.data(), 
                                          directory.size() * sizeof(C),
                                          encoding, 
                                          false);
                  if (!path) { return nullptr; }
                  CFArrayRef paths = CFArrayCreate(
                                          kCFAllocatorDefault, 
                                          (const void**)&path, 
                                          1, 
                                          nullptr);
                  if (!paths) { CFRelease(path); return nullptr; }
                  FSEventStreamContext context {
                        .info = (void*)this
                  };
                  FSEventStreamRef event = FSEventStreamCreate(
                                    kCFAllocatorDefault, 
                                    (FSEventStreamCallback)handleFsEvent, 
                                    &context, 
                                    paths, 
                                    kFSEventStreamEventIdSinceNow, 
                                    0, 
                                    kFSEventStreamCreateFlagNoDefer | kFSEventStreamCreateFlagFileEvents | 
                                    kFSEventStreamCreateFlagUseCFTypes);

                  CFRelease(path);
                  CFRelease(paths);
                  return event;
            }

            FSEventStreamRef openStreamForDirectory(const StringType& directory) {
                  FSEventStreamRef stream = openStream(directory);
                  walkDirectory(directory, [this] (StringType path) mutable {
                        if (!isParentOrSelfDirectory(path) && std::regex_match(path, _pattern)) {
                              FileState state = makeFileState(path);
                              if (state.fd != -1) {
                                    _directory_snapshot.insert(std::make_pair(std::move(path), std::move(state)));
                              }
                        }
                  });
                  return stream;
            }


            std::expected<FSEventStreamRef, std::string> get_directory_expected(const StringType& directory) {
                  const int fd = open(directory.c_str(), O_RDONLY);
                  if (fd < 0) {
                        return std::unexpected("Open watched directory: " + std::error_code(errno, std::system_category()).message());
                  }
                  close(fd);
                  auto stream = openStreamForDirectory(directory);
                  if (!stream) { return std::unexpected("Create FSEvents stream"); }
                  return stream;
            }

            std::expected<void, std::string> monitor_directory() {
                  const auto loop = CFRunLoopGetCurrent();
                  CFRetain(loop);
                  _run_loop = loop;
                  FSEventStreamScheduleWithRunLoop(_directory, loop, kCFRunLoopDefaultMode);
                  if (!FSEventStreamStart(_directory)) {
                        return std::unexpected("Start FSEvents stream");
                  }
                  _stream_started = true;
                  signalReady({});
                  // A bounded run also handles stop arriving between startup acknowledgement and RunInMode.
                  while (!_destory) { CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.1, false); }
                  return {};
            }
#endif // FILEWATCH_PLATFORM_MAC

		void callback_thread()
		{
			while (_destory == false) {
				std::unique_lock<std::mutex> lock(_callback_mutex);
				if (_callback_information.empty() && _destory == false) {
					_cv.wait(lock, [this] { return _callback_information.size() > 0 || _destory; });
				}
				decltype(_callback_information) callback_information = {};
				std::swap(callback_information, _callback_information);
				lock.unlock();

				for (const auto& file : callback_information) {
					if (_callback) {
                        if (_destory) { return; }
                        if (auto called = _callback(file.first, file.second); !called) {
                            fail(called.error());
                            return;
                        }
					}
				}
			}
		}
	};

	template<class StringType> constexpr typename FileWatch<StringType>::C FileWatch<StringType>::_regex_all[];
	template<class StringType> constexpr typename FileWatch<StringType>::C FileWatch<StringType>::_this_directory[];
}
#endif
