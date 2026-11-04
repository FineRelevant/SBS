#pragma once

#include <cstddef>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

#include "esp_err.h"

namespace storage
{
	class FilesystemService final
	{
	  public:
		struct Config
		{
			std::string base_path{"/littlefs"};
			std::string partition_label{"storage"};
			bool format_if_mount_failed{false};
		};

		struct Info
		{
			std::size_t total_bytes{};
			std::size_t used_bytes{};

			[[nodiscard]] std::size_t free_bytes() const noexcept
			{
				return total_bytes >= used_bytes
						   ? total_bytes - used_bytes
						   : 0;
			}
		};

		FilesystemService();
		explicit FilesystemService(Config config);
		~FilesystemService();

		FilesystemService(const FilesystemService &) = delete;
		FilesystemService &operator=(const FilesystemService &) = delete;
		FilesystemService(FilesystemService &&) = delete;
		FilesystemService &operator=(FilesystemService &&) = delete;

		[[nodiscard]]
		esp_err_t mount();

		[[nodiscard]]
		esp_err_t unmount();

		[[nodiscard]]
		bool is_mounted() const noexcept;

		[[nodiscard]]
		std::expected<Info, esp_err_t> info() const;

		[[nodiscard]]
		std::expected<bool, esp_err_t>
		exists(std::string_view path) const;

		[[nodiscard]]
		std::expected<std::string, esp_err_t>
		read_text(std::string_view path) const;

		[[nodiscard]]
		std::expected<std::vector<std::byte>, esp_err_t>
		read_binary(std::string_view path) const;

		[[nodiscard]]
		esp_err_t write_text(std::string_view path, std::string_view text);

		[[nodiscard]]
		esp_err_t write_binary(
			std::string_view path,
			const std::vector<std::byte> &data);

		[[nodiscard]]
		esp_err_t append_text(std::string_view path, std::string_view text);

		[[nodiscard]]
		esp_err_t remove(std::string_view path);

		[[nodiscard]]
		esp_err_t rename(std::string_view from, std::string_view to);

		[[nodiscard]]
		esp_err_t format();

	  private:
		[[nodiscard]]
		esp_err_t require_mounted() const;

		[[nodiscard]]
		std::expected<std::string, esp_err_t>
		resolve_path(std::string_view path) const;

		Config config_;
		bool mounted_{false};
	};
}