#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "esp_err.h"
#include "nvs.h"

namespace storage
{
	class NvsService final
	{
	public:
		enum class Access : uint8_t
		{
			ReadOnly = 0,
			ReadWrite,
		};

		NvsService() noexcept = default;
		~NvsService();

		NvsService(const NvsService &) = delete;
		NvsService &operator=(const NvsService &) = delete;

		NvsService(NvsService &&other) noexcept;
		NvsService &operator=(NvsService &&other) noexcept;

		[[nodiscard]]
		static std::expected<NvsService, esp_err_t>
		open(
			std::string_view name_space,
			Access access = Access::ReadWrite,
			std::string_view partition_label = {});

		[[nodiscard]]
		bool is_open() const noexcept;

		void close() noexcept;

		[[nodiscard]]
		std::expected<std::int32_t, esp_err_t>
		get_i32(std::string_view key) const;

		[[nodiscard]]
		std::expected<std::uint32_t, esp_err_t>
		get_u32(std::string_view key) const;

		[[nodiscard]]
		std::expected<std::uint64_t, esp_err_t>
		get_u64(std::string_view key) const;

		[[nodiscard]]
		std::expected<bool, esp_err_t>
		get_bool(std::string_view key) const;

		[[nodiscard]]
		std::expected<std::string, esp_err_t>
		get_string(std::string_view key) const;

		[[nodiscard]]
		std::expected<std::vector<std::byte>, esp_err_t>
		get_blob(std::string_view key) const;

		[[nodiscard]]
		esp_err_t set_i32(std::string_view key, std::int32_t value);

		[[nodiscard]]
		esp_err_t set_u32(std::string_view key, std::uint32_t value);

		[[nodiscard]]
		esp_err_t set_u64(std::string_view key, std::uint64_t value);

		[[nodiscard]]
		esp_err_t set_bool(std::string_view key, bool value);

		[[nodiscard]]
		esp_err_t set_string(std::string_view key, std::string_view value);

		[[nodiscard]]
		esp_err_t set_blob(std::string_view key, std::span<const std::byte> value);

		[[nodiscard]]
		esp_err_t erase(std::string_view key);

		[[nodiscard]]
		esp_err_t erase_all();

		[[nodiscard]]
		esp_err_t commit();

	private:
		explicit NvsService(nvs_handle_t handle) noexcept;

		[[nodiscard]]
		static std::expected<std::string, esp_err_t>
		validate_name(
			std::string_view value,
			std::size_t max_length);

		[[nodiscard]]
		esp_err_t require_open() const;

		nvs_handle_t handle_ {};
	};
} // namespace storage