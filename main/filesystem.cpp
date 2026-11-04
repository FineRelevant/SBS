
#include "filesystem.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

#include "esp_littlefs.h"

namespace storage
{
    namespace
    {
        [[nodiscard]]
        esp_err_t map_errno(int error) noexcept
        {
            switch (error)
            {
            case 0:
                return ESP_FAIL;

            case ENOENT:
                return ESP_ERR_NOT_FOUND;

            case ENOMEM:
                return ESP_ERR_NO_MEM;

            case EINVAL:
            case ENAMETOOLONG:
                return ESP_ERR_INVALID_ARG;

            case EEXIST:
                return ESP_ERR_INVALID_STATE;

            case EACCES:
            case EPERM:
            case EROFS:
                return ESP_ERR_INVALID_STATE;

            case ENOSPC:
                return ESP_ERR_NO_MEM;

            default:
                return ESP_FAIL;
            }
        }

        class File final
        {
        public:
            explicit File(std::FILE* file = nullptr) noexcept
                : file_(file)
            {
            }

            ~File()
            {
                (void)close();
            }

            File(const File&) = delete;
            File& operator=(const File&) = delete;

            File(File&& other) noexcept
                : file_(std::exchange(other.file_, nullptr))
            {
            }

            File& operator=(File&& other) noexcept
            {
                if (this != &other)
                {
                    (void)close();
                    file_ = std::exchange(other.file_, nullptr);
                }

                return *this;
            }

            [[nodiscard]]
            std::FILE* get() const noexcept
            {
                return file_;
            }

            [[nodiscard]]
            bool close() noexcept
            {
                if (!file_)
                {
                    return true;
                }

                auto* file = std::exchange(file_, nullptr);
                return std::fclose(file) == 0;
            }

        private:
            std::FILE* file_{};
        };

        [[nodiscard]]
        esp_err_t write_file(
            std::FILE* file,
            const void* data,
            std::size_t size) noexcept
        {
            if (size != 0 &&
                std::fwrite(data, 1, size, file) != size)
            {
                return ESP_FAIL;
            }

            if (std::fflush(file) != 0)
            {
                return ESP_FAIL;
            }

            return ESP_OK;
        }

    } // namespace

    FilesystemService::FilesystemService()
        : FilesystemService(Config{})
    {
    }

    FilesystemService::FilesystemService(Config config)
        : config_(std::move(config))
    {
    }

    FilesystemService::~FilesystemService()
    {
        if (mounted_)
        {
            (void)unmount();
        }
    }

    esp_err_t FilesystemService::mount()
    {
        if (mounted_)
        {
            return ESP_OK;
        }

        if (config_.base_path.empty() ||
            config_.base_path.front() != '/' ||
            config_.partition_label.empty())
        {
            return ESP_ERR_INVALID_ARG;
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

        const auto err = esp_vfs_littlefs_register(&conf);

        if (err != ESP_OK)
        {
            return err;
        }

        mounted_ = true;
        return ESP_OK;
    }

    esp_err_t FilesystemService::unmount()
    {
        if (!mounted_)
        {
            return ESP_OK;
        }

        const auto err = esp_vfs_littlefs_unregister(
            config_.partition_label.c_str());

        if (err != ESP_OK)
        {
            return err;
        }

        mounted_ = false;
        return ESP_OK;
    }

    bool FilesystemService::is_mounted() const noexcept
    {
        return mounted_ &&
            esp_littlefs_mounted(config_.partition_label.c_str());
    }

    esp_err_t FilesystemService::require_mounted() const
    {
        return is_mounted()
            ? ESP_OK
            : ESP_ERR_INVALID_STATE;
    }

    std::expected<std::string, esp_err_t>
    FilesystemService::resolve_path(std::string_view path) const
    {
        if (path.empty() ||
            path.find('\0') != std::string_view::npos)
        {
            return std::unexpected(ESP_ERR_INVALID_ARG);
        }

        // Accept both "config.json" and "/config.json".
        while (!path.empty() && path.front() == '/')
        {
            path.remove_prefix(1);
        }

        if (path.empty())
        {
            return std::unexpected(ESP_ERR_INVALID_ARG);
        }

        // Reject traversal and malformed segments.
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

        if (config_.base_path.empty() ||
            config_.base_path.front() != '/')
        {
            return std::unexpected(ESP_ERR_INVALID_STATE);
        }

        std::string full = config_.base_path;

        while (full.size() > 1 && full.back() == '/')
        {
            full.pop_back();
        }

        if (full != "/")
        {
            full.push_back('/');
        }

        full.append(path);

        return full;
    }

    std::expected<FilesystemService::Info, esp_err_t>
    FilesystemService::info() const
    {
        if (const auto err = require_mounted(); err != ESP_OK)
        {
            return std::unexpected(err);
        }

        Info result{};

        const auto err = esp_littlefs_info(
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
        auto full = resolve_path(path);

        if (!full)
        {
            return std::unexpected(full.error());
        }

        if (const auto err = require_mounted(); err != ESP_OK)
        {
            return std::unexpected(err);
        }

        struct stat st{};

        if (::stat(full->c_str(), &st) == 0)
        {
            return true;
        }

        const int error = errno;

        if (error == ENOENT)
        {
            return false;
        }

        return std::unexpected(map_errno(error));
    }

    std::expected<std::string, esp_err_t>
    FilesystemService::read_text(std::string_view path) const
    {
        auto data = read_binary(path);

        if (!data)
        {
            return std::unexpected(data.error());
        }

        return std::string(
            reinterpret_cast<const char*>(data->data()),
            data->size());
    }

	std::expected<std::vector<std::byte>, esp_err_t>
	FilesystemService::read_binary(std::string_view path) const
	{
		auto full = resolve_path(path);
		if (!full) {
			return std::unexpected(full.error());
		}

		if (const auto err = require_mounted(); err != ESP_OK) {
			return std::unexpected(err);
		}

		File file(std::fopen(full->c_str(), "rb"));
		if (!file.get()) {
			return std::unexpected(map_errno(errno));
		}

		if (std::fseek(file.get(), 0, SEEK_END) != 0) {
			return std::unexpected(map_errno(errno));
		}

		const long end = std::ftell(file.get());
		if (end < 0) {
			return std::unexpected(map_errno(errno));
		}

		if (std::fseek(file.get(), 0, SEEK_SET) != 0) {
			return std::unexpected(map_errno(errno));
		}

		const auto size = static_cast<std::size_t>(end);

		std::vector<std::byte> data(size);          // ← allocate first!

		if (size != 0) {
			const auto count = std::fread(data.data(), 1, size, file.get());
			if (count != size) {
				return std::unexpected(ESP_FAIL);
			}
		}

		return data;
	}

    esp_err_t FilesystemService::write_text(
        std::string_view path,
        std::string_view text)
    {
        auto full = resolve_path(path);

        if (!full)
        {
            return full.error();
        }

        if (const auto err = require_mounted(); err != ESP_OK)
        {
            return err;
        }

        File file(std::fopen(full->c_str(), "wb"));

        if (!file.get())
        {
            return map_errno(errno);
        }

        if (const auto err = write_file(
                file.get(), text.data(), text.size());
            err != ESP_OK)
        {
            return err;
        }

        return file.close()
            ? ESP_OK
            : ESP_FAIL;
    }

    esp_err_t FilesystemService::write_binary(
        std::string_view path,
        const std::vector<std::byte>& data)
    {
        auto full = resolve_path(path);

        if (!full)
        {
            return full.error();
        }

        if (const auto err = require_mounted(); err != ESP_OK)
        {
            return err;
        }

        File file(std::fopen(full->c_str(), "wb"));

        if (!file.get())
        {
            return map_errno(errno);
        }

        if (const auto err = write_file(
                file.get(), data.data(), data.size());
            err != ESP_OK)
        {
            return err;
        }

        return file.close()
            ? ESP_OK
            : ESP_FAIL;
    }

    esp_err_t FilesystemService::append_text(
        std::string_view path,
        std::string_view text)
    {
        auto full = resolve_path(path);

        if (!full)
        {
            return full.error();
        }

        if (const auto err = require_mounted(); err != ESP_OK)
        {
            return err;
        }

        File file(std::fopen(full->c_str(), "ab"));

        if (!file.get())
        {
            return map_errno(errno);
        }

        if (const auto err = write_file(
                file.get(), text.data(), text.size());
            err != ESP_OK)
        {
            return err;
        }

        return file.close()
            ? ESP_OK
            : ESP_FAIL;
    }

    esp_err_t FilesystemService::remove(std::string_view path)
    {
        auto full = resolve_path(path);

        if (!full)
        {
            return full.error();
        }

        if (const auto err = require_mounted(); err != ESP_OK)
        {
            return err;
        }

        if (::unlink(full->c_str()) != 0)
        {
            return map_errno(errno);
        }

        return ESP_OK;
    }

    esp_err_t FilesystemService::rename(
        std::string_view from,
        std::string_view to)
    {
        auto source = resolve_path(from);

        if (!source)
        {
            return source.error();
        }

        auto destination = resolve_path(to);

        if (!destination)
        {
            return destination.error();
        }

        if (const auto err = require_mounted(); err != ESP_OK)
        {
            return err;
        }

        if (::rename(
                source->c_str(),
                destination->c_str()) != 0)
        {
            return map_errno(errno);
        }

        return ESP_OK;
    }



	esp_err_t FilesystemService::format()
	{
		if (config_.partition_label.empty())
		{
			return ESP_ERR_INVALID_ARG;
		}

		const esp_err_t err =
			esp_littlefs_format(config_.partition_label.c_str());

		if (err != ESP_OK)
		{
			return err;
		}

		return ESP_OK;
	}
} // namespace storage