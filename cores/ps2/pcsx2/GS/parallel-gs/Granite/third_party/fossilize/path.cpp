/* Copyright (c) 2017-2019 Hans-Kristian Arntzen
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "path.hpp"
#include <algorithm>
#include <sstream>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <sys/stat.h>
#else
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#ifdef __linux__
#include <linux/limits.h>
#endif
#endif

using namespace std;

namespace Util
{
namespace inner
{
template<typename T>
void join_helper(std::ostringstream &stream, T &&t)
{
	stream << std::forward<T>(t);
}

template<typename T, typename... Ts>
void join_helper(std::ostringstream &stream, T &&t, Ts &&... ts)
{
	stream << std::forward<T>(t);
	join_helper(stream, std::forward<Ts>(ts)...);
}
}

template<typename... Ts>
std::string join(Ts &&... ts)
{
	std::ostringstream stream;
	inner::join_helper(stream, std::forward<Ts>(ts)...);
	return stream.str();
}
}

namespace Path
{
static vector<string> split(const string &str, const char *delim, bool allow_empty)
{
	if (str.empty())
		return {};
	vector<string> ret;

	size_t start_index = 0;
	size_t index = 0;
	while ((index = str.find_first_of(delim, start_index)) != string::npos)
	{
		if (allow_empty || index > start_index)
			ret.push_back(str.substr(start_index, index - start_index));
		start_index = index + 1;

		if (allow_empty && (index == str.size() - 1))
			ret.emplace_back();
	}

	if (start_index < str.size())
		ret.push_back(str.substr(start_index));
	return ret;
}

vector<string> split(const string &str, const char *delim)
{
	return split(str, delim, true);
}

vector<string> split_no_empty(const string &str, const char *delim)
{
	return split(str, delim, false);
}

string strip_whitespace(const string &str)
{
	string ret;
	auto index = str.find_first_not_of(" \t");
	if (index == string::npos)
		return "";
	ret = str.substr(index, string::npos);
	index = ret.find_last_not_of(" \t");
	if (index != string::npos)
		return ret.substr(0, index + 1);
	else
		return ret;
}

string enforce_protocol(const string &path)
{
	if (path.empty())
		return "";

	auto index = path.find("://");
	if (index == string::npos)
		return string("file://") + path;
	else
		return path;
}

string canonicalize_path(const string &path)
{
	string transformed;
	transformed.resize(path.size());
	transform(begin(path), end(path), begin(transformed), [](char c) -> char { return c == '\\' ? '/' : c; });
	auto data = split_no_empty(transformed, "/");

	vector<string> result;
	for (auto &i : data)
	{
		if (i == "..")
		{
			if (!result.empty())
				result.pop_back();
		}
		else
			result.push_back(move(i));
	}

	string res;
	for (auto &i : result)
	{
		if (&i != result.data())
			res += "/";
		res += i;
	}
	return res;
}

static size_t find_last_slash(const string &str)
{
#ifdef _WIN32
	auto index = str.find_last_of("/\\");
#else
	auto index = str.find_last_of('/');
#endif
	return index;
}

bool is_abspath(const string &path)
{
	if (path.empty())
		return false;
	if (path.front() == '/')
		return true;

#ifdef _WIN32
	{
		auto index = (std::min)(path.find(":/"), path.find(":\\"));
		if (index != string::npos)
			return true;
	}
#endif

	return path.find("://") != string::npos;
}

bool is_root_path(const string &path)
{
	if (path.empty())
		return false;

	if (path.front() == '/' && path.size() == 1)
		return true;

#ifdef _WIN32
	{
		auto index = (std::min)(path.find(":/"), path.find(":\\"));
		if (index != string::npos && (index + 2) == path.size())
			return true;
	}
#endif

	auto index = path.find("://");
	return index != string::npos && (index + 3) == path.size();
}

string join(const string &base, const string &path)
{
	if (base.empty())
		return path;
	if (path.empty())
		return base;

	if (is_abspath(path))
		return path;

	auto index = find_last_slash(base);
	bool need_slash = index != base.size() - 1;
	return Util::join(base, need_slash ? "/" : "", path);
}

string basedir(const string &path)
{
	if (path.empty())
		return "";

	if (is_root_path(path))
		return path;

	auto index = find_last_slash(path);
	if (index == string::npos)
		return ".";

	// Preserve the first slash.
	if (index == 0 && is_abspath(path))
		index++;

	auto ret = path.substr(0, index + 1);
	if (!is_root_path(ret))
		ret.pop_back();
	return ret;
}

string basename(const string &path)
{
	if (path.empty())
		return "";

	auto index = find_last_slash(path);
	if (index == string::npos)
		return path;

	auto base = path.substr(index + 1, string::npos);
	return base;
}

string relpath(const string &base, const string &path)
{
	return Path::join(basedir(base), path);
}

string ext(const string &path)
{
	auto index = path.find_last_of('.');
	if (index == string::npos)
		return "";
	else
		return path.substr(index + 1, string::npos);
}

pair<string, string> split(const string &path)
{
	if (path.empty())
		return make_pair(string("."), string("."));

	auto index = find_last_slash(path);
	if (index == string::npos)
		return make_pair(string("."), path);

	auto base = path.substr(index + 1, string::npos);
	return make_pair(path.substr(0, index), base);
}

pair<string, string> protocol_split(const string &path)
{
	if (path.empty())
		return make_pair(string(""), string(""));

	auto index = path.find("://");
	if (index == string::npos)
		return make_pair(string(""), path);

	return make_pair(path.substr(0, index), path.substr(index + 3, string::npos));
}

string get_executable_path()
{
#ifdef _WIN32
	char target[4096];
	DWORD ret = GetModuleFileNameA(GetModuleHandle(nullptr), target, sizeof(target));
	target[ret] = '\0';
	return canonicalize_path(string(target));
#else
	static const char *exts[] = { "exe", "file", "a.out" };
	char link_path[PATH_MAX];
	char target[PATH_MAX];

	for (auto *ext : exts)
	{
		snprintf(link_path, sizeof(link_path), "/proc/self/%s", ext);
		ssize_t ret = readlink(link_path, target, sizeof(target) - 1);
		if (ret >= 0)
			target[ret] = '\0';

		return string(target);
	}

	return "";
#endif
}

bool mkdir(const std::string &path)
{
#ifdef _WIN32
	if (!CreateDirectoryA(path.c_str(), nullptr))
	{
		if (GetLastError() != ERROR_ALREADY_EXISTS)
			return false;
		if (!is_directory(path))
			return false;
	}
	return true;
#else
	int ret = ::mkdir(path.c_str(), 0750);
	if (ret == 0)
		return true;
	if (ret < 0 && errno != EEXIST)
		return false;

	// Verify the path is actually a directory.
	struct stat s = {};
	return ::stat(path.c_str(), &s) == 0 && (s.st_mode & S_IFDIR) != 0;
#endif
}

bool touch(const std::string &path)
{
#ifdef _WIN32
	HANDLE h = CreateFileA(path.c_str(), GENERIC_WRITE, FILE_SHARE_WRITE | FILE_SHARE_READ,
	                       nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
	                       INVALID_HANDLE_VALUE);
	if (h == INVALID_HANDLE_VALUE)
		return false;

	SYSTEMTIME st;
	FILETIME ft;
	GetSystemTime(&st);
	SystemTimeToFileTime(&st, &ft);
	bool success = SetFileTime(h, nullptr, nullptr, &ft) != 0;
	CloseHandle(h);
	return success;
#else
	int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0640);
	if (fd < 0)
		return false;

	const struct timespec times[2] = {{ 0, UTIME_NOW }, { 0, UTIME_NOW }};
	bool ret = ::futimens(fd, times) == 0;
	::close(fd);
	return ret;
#endif
}

bool is_file(const std::string &path)
{
#ifdef _WIN32
	struct __stat64 s;
	if (_stat64(path.c_str(), &s) < 0)
		return false;
	return (s.st_mode & _S_IFREG) != 0;
#else
	struct stat s = {};
	return ::stat(path.c_str(), &s) == 0 && (s.st_mode & S_IFREG) != 0;
#endif
}

bool is_directory(const std::string &path)
{
#ifdef _WIN32
	struct __stat64 s;
	if (_stat64(path.c_str(), &s) < 0)
		return false;
	return (s.st_mode & _S_IFDIR) != 0;
#else
	struct stat s = {};
	return ::stat(path.c_str(), &s) == 0 && (s.st_mode & S_IFDIR) != 0;
#endif
}

bool get_mtime_us(const std::string &path, uint64_t &mtime_us)
{
#ifdef _WIN32
	HANDLE h = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
	                       nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
	                       INVALID_HANDLE_VALUE);
	if (h == INVALID_HANDLE_VALUE)
		return false;

	FILETIME ft;
	if (!GetFileTime(h, nullptr, nullptr, &ft))
	{
		CloseHandle(h);
		return false;
	}

	mtime_us = ((uint64_t(ft.dwHighDateTime) << 32) | uint64_t(ft.dwLowDateTime)) / 10;
	CloseHandle(h);
	return true;
#else
	struct stat s = {};
	if (::stat(path.c_str(), &s) != 0)
		return false;
#if defined(__linux__)
	mtime_us = s.st_mtim.tv_sec * 1000000ll + s.st_mtim.tv_nsec / 1000;
#else
	// Fallback.
	mtime_us = s.st_mtime * 1000000ll;
#endif
	return true;
#endif
}
}
