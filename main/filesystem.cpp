#include "filesystem.hpp"

#include <cerrno>
#include <cstdio>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

#include "esp_littlefs.h"
namespace storage
{
	namespace
	{

		esp_err_t map_errno(int error) noexcept
		{
			switch (error)
			{
			case ENOENT:
				return ESP_ERR_NOT_FOUND;
			case ENOMEM:
				return ESP_ERR_NO_MEM;
			case EINVAL:
				return ESP_ERR_INVALID_ARG;
			default:
				return ESP_FAIL;
			}
		}

		class File final
		{
		  public:
			explicit File(std::FILE *file = nullptr) noexcept
				: file_(file) {}

			~File()
			{
				if (file_)
				{
					std::fclose(file_);
				}
			}

			File(const File &) = delete;
			File &operator=(const File &) = delete;

			[[nodiscard]]
			std::FILE *get() const noexcept
			{
				return file_;
			}

			[[nodiscard]]
			std::FILE *release() noexcept
			{
				return std::exchange(file_, nullptr);
			}

		  private:
			std::FILE *file_{};
		};

	} // namespace
	FilesystemService::FilesystemService()
		: FilesystemService(Config{})
	{
	}
	
	FilesystemService::FilesystemService(Config config)
		: config_(std::move(config)) {}

	FilesystemService::~FilesystemService()
	{
		if (mounted_)
		{
			(void)unmount();
		}
	}

	std::expected<void, esp_err_t>
	FilesystemService::mount()
	{
		if (mounted_)
		{
			return {};
		}

		if (config_.base_path.empty() ||
			config_.base_path.front() != '/' ||
			config_.partition_label.empty())
		{
			return std::unexpected(ESP_ERR_INVALID_ARG);
		}

		esp_vfs_littlefs_conf_t conf{};

		conf.base_path = config_.base_path.c_str();
		conf.partition_label = config_.partition_label.c_str();
		conf.format_if_mount_failed = config_.format_if_mount_failed;
		conf.dont_mount = false;
		conf.partition = nullptr;
		conf.blockdev = nullptr;
		conf.read_only = false;
		conf.grow_on_mount = false;

		const esp_err_t err = esp_vfs_littlefs_register(&conf);

		if (err != ESP_OK)
		{
			return std::unexpected(err);
		}

		mounted_ = true;
		return {};
	}

	std::expected<void, esp_err_t>
	FilesystemService::unmount()
	{
		if (!mounted_)
		{
			return {};
		}

		const esp_err_t err =
			esp_vfs_littlefs_unregister(
				config_.partition_label.c_str());

		if (err != ESP_OK)
		{
			return std::unexpected(err);
		}

		mounted_ = false;
		return {};
	}

	bool FilesystemService::is_mounted() const noexcept
	{
		return mounted_ &&
			   esp_littlefs_mounted(config_.partition_label.c_str());
	}

	std::expected<void, esp_err_t>
	FilesystemService::require_mounted() const
	{
		if (!is_mounted())
		{
			return std::unexpected(ESP_ERR_INVALID_STATE);
		}

		return {};
	}

	std::expected<std::string, esp_err_t>
	FilesystemService::resolve_path(
		std::string_view path) const
	{

		if (path.empty() ||
			path.find('\0') != std::string_view::npos)
		{
			return std::unexpected(ESP_ERR_INVALID_ARG);
		}

		// Both "config.json" and "/config.json" are accepted.
		while (!path.empty() && path.front() == '/')
		{
			path.remove_prefix(1);
		}

		if (path.empty())
		{
			return std::unexpected(ESP_ERR_INVALID_ARG);
		}

		// Reject traversal and malformed empty path segments.
		std::size_t start = 0;

		while (start < path.size())
		{
			const auto end = path.find('/', start);

			const auto segment = path.substr(
				start,
				end == std::string_view::npos
					? path.size() - start
					: end - start);

			if (segment.empty() ||
				segment == "." ||
				segment == "..")
			{
				return std::unexpected(ESP_ERR_INVALID_ARG);
			}

			if (end == std::string_view::npos)
			{
				break;
			}

			start = end + 1;
		}

		std::string full = config_.base_path;

		if (full.back() == '/')
		{
			full.pop_back();
		}

		full.push_back('/');
		full.append(path);

		return full;
	}

	std::expected<FilesystemService::Info, esp_err_t>
	FilesystemService::info() const
	{
		if (auto result = require_mounted(); !result)
		{
			return std::unexpected(result.error());
		}

		Info result{};

		const esp_err_t err = esp_littlefs_info(
			config_.partition_label.c_str(),
			&result.total_bytes,
			&result.used_bytes);

		if (err != ESP_OK)
		{
			return std::unexpected(err);
		}

		return result;
	}

	std::expected<bool, esp_err_t>
	FilesystemService::exists(std::string_view path) const
	{
		if (auto result = require_mounted(); !result)
		{
			return std::unexpected(result.error());
		}

		auto full = resolve_path(path);

		if (!full)
		{
			return std::unexpected(full.error());
		}

		struct stat st{};

		if (::stat(full->c_str(), &st) == 0)
		{
			return true;
		}

		if (errno == ENOENT)
		{
			return false;
		}

		return std::unexpected(map_errno(errno));
	}

	std::expected<std::string, esp_err_t>
	FilesystemService::read_text(std::string_view path) const
	{
		auto data = read_binary(path);

		if (!data)
		{
			return std::unexpected(data.error());
		}

		std::string result;

		if (!data->empty())
		{
			result.assign(
				reinterpret_cast<const char *>(data->data()),
				data->size());
		}

		return result;
	}

	std::expected<std::vector<std::byte>, esp_err_t>
	FilesystemService::read_binary(
		std::string_view path) const
	{

		if (auto result = require_mounted(); !result)
		{
			return std::unexpected(result.error());
		}

		auto full = resolve_path(path);

		if (!full)
		{
			return std::unexpected(full.error());
		}

		File file(std::fopen(full->c_str(), "rb"));

		if (!file.get())
		{
			return std::unexpected(map_errno(errno));
		}

		if (std::fseek(file.get(), 0, SEEK_END) != 0)
		{
			return std::unexpected(map_errno(errno));
		}

		const long end = std::ftell(file.get());

		if (end < 0)
		{
			return std::unexpected(map_errno(errno));
		}

		if (std::fseek(file.get(), 0, SEEK_SET) != 0)
		{
			return std::unexpected(map_errno(errno));
		}

		std::vector<std::byte> data(
			static_cast<std::size_t>(end));

		if (!data.empty())
		{
			const auto count = std::fread(
				data.data(), 1, data.size(), file.get());

			if (count != data.size())
			{
				return std::unexpected(
					std::ferror(file.get())
						? map_errno(errno)
						: ESP_FAIL);
			}
		}

		return data;
	}

	std::expected<void, esp_err_t>
	FilesystemService::write_text(
		std::string_view path,
		std::string_view text)
	{

		if (text.find('\0') != std::string_view::npos)
		{
			// Text may contain embedded NULs in some applications.
			// This implementation permits them, so no rejection is needed.
		}

		auto full = resolve_path(path);

		if (!full)
		{
			return std::unexpected(full.error());
		}

		if (auto result = require_mounted(); !result)
		{
			return std::unexpected(result.error());
		}

		File file(std::fopen(full->c_str(), "wb"));

		if (!file.get())
		{
			return std::unexpected(map_errno(errno));
		}

		if (!text.empty() &&
			std::fwrite(text.data(), 1, text.size(), file.get()) != text.size())
		{
			return std::unexpected(map_errno(errno));
		}

		if (std::fflush(file.get()) != 0)
		{
			return std::unexpected(map_errno(errno));
		}

		if (std::fclose(file.release()) != 0)
		{
			return std::unexpected(map_errno(errno));
		}

		return {};
	}

	std::expected<void, esp_err_t>
	FilesystemService::write_binary(
		std::string_view path,
		const std::vector<std::byte> &data)
	{

		if (auto result = require_mounted(); !result)
		{
			return std::unexpected(result.error());
		}

		auto full = resolve_path(path);

		if (!full)
		{
			return std::unexpected(full.error());
		}

		File file(std::fopen(full->c_str(), "wb"));

		if (!file.get())
		{
			return std::unexpected(map_errno(errno));
		}

		if (!data.empty() &&
			std::fwrite(data.data(), 1, data.size(), file.get()) != data.size())
		{
			return std::unexpected(map_errno(errno));
		}

		if (std::fflush(file.get()) != 0)
		{
			return std::unexpected(map_errno(errno));
		}

		if (std::fclose(file.release()) != 0)
		{
			return std::unexpected(map_errno(errno));
		}

		return {};
	}

	std::expected<void, esp_err_t>
	FilesystemService::append_text(
		std::string_view path,
		std::string_view text)
	{

		if (auto result = require_mounted(); !result)
		{
			return std::unexpected(result.error());
		}

		auto full = resolve_path(path);

		if (!full)
		{
			return std::unexpected(full.error());
		}

		File file(std::fopen(full->c_str(), "ab"));

		if (!file.get())
		{
			return std::unexpected(map_errno(errno));
		}

		if (!text.empty() &&
			std::fwrite(text.data(), 1, text.size(), file.get()) != text.size())
		{
			return std::unexpected(map_errno(errno));
		}

		if (std::fflush(file.get()) != 0)
		{
			return std::unexpected(map_errno(errno));
		}

		if (std::fclose(file.release()) != 0)
		{
			return std::unexpected(map_errno(errno));
		}

		return {};
	}

	std::expected<void, esp_err_t>
	FilesystemService::remove(std::string_view path)
	{
		if (auto result = require_mounted(); !result)
		{
			return std::unexpected(result.error());
		}

		auto full = resolve_path(path);

		if (!full)
		{
			return std::unexpected(full.error());
		}

		if (::unlink(full->c_str()) != 0)
		{
			return std::unexpected(map_errno(errno));
		}

		return {};
	}

	std::expected<void, esp_err_t>
	FilesystemService::rename(
		std::string_view from,
		std::string_view to)
	{

		if (auto result = require_mounted(); !result)
		{
			return std::unexpected(result.error());
		}

		auto source = resolve_path(from);

		if (!source)
		{
			return std::unexpected(source.error());
		}

		auto destination = resolve_path(to);

		if (!destination)
		{
			return std::unexpected(destination.error());
		}

		if (::rename(source->c_str(), destination->c_str()) != 0)
		{
			return std::unexpected(map_errno(errno));
		}

		return {};
	}
} // namespace storage