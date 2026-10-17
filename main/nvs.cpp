#include "nvs.hpp"

#include <utility>

#include "nvs_flash.h"

namespace storage
{

	NvsService::NvsService(nvs_handle_t handle) noexcept
		: handle_(handle) {}

	NvsService::~NvsService()
	{
		close();
	}

	NvsService::NvsService(NvsService &&other) noexcept
		: handle_(std::exchange(other.handle_, 0)) {}

	NvsService &NvsService::operator=(NvsService &&other) noexcept
	{
		if (this != &other)
		{
			close();
			handle_ = std::exchange(other.handle_, 0);
		}

		return *this;
	}

	bool NvsService::is_open() const noexcept
	{
		return handle_ != 0;
	}

	void NvsService::close() noexcept
	{
		if (handle_ != 0)
		{
			nvs_close(handle_);
			handle_ = 0;
		}
	}

	std::expected<void, esp_err_t>
	NvsService::require_open() const
	{
		if (!is_open())
		{
			return std::unexpected(ESP_ERR_INVALID_STATE);
		}

		return {};
	}

	std::expected<std::string, esp_err_t>
	NvsService::validate_name(
		std::string_view value,
		std::size_t max_length)
	{

		if (value.empty() ||
			value.size() > max_length ||
			value.find('\0') != std::string_view::npos)
		{
			return std::unexpected(ESP_ERR_INVALID_ARG);
		}

		return std::string(value);
	}

	std::expected<NvsService, esp_err_t>
	NvsService::open(
		std::string_view name_space,
		Access access,
		std::string_view partition_label)
	{

		auto ns = validate_name(
			name_space, NVS_PART_NAME_MAX_SIZE - 1);

		if (!ns)
		{
			return std::unexpected(ns.error());
		}

		const auto mode = access == Access::ReadOnly
							  ? NVS_READONLY
							  : NVS_READWRITE;

		nvs_handle_t handle{};

		esp_err_t err{};

		if (partition_label.empty())
		{
			err = nvs_open(ns->c_str(), mode, &handle);
		}
		else
		{
			// ESP-IDF partition labels are limited to 16 characters,
			// including the terminating NUL.
			auto partition = validate_name(partition_label, 15);

			if (!partition)
			{
				return std::unexpected(partition.error());
			}

			err = nvs_open_from_partition(
				partition->c_str(),
				ns->c_str(),
				mode,
				&handle);
		}

		if (err != ESP_OK)
		{
			return std::unexpected(err);
		}

		return NvsService{handle};
	}

	std::expected<std::int32_t, esp_err_t>
	NvsService::get_i32(std::string_view key) const
	{
		if (auto r = require_open(); !r)
		{
			return std::unexpected(r.error());
		}

		auto k = validate_name(key, NVS_KEY_NAME_MAX_SIZE - 1);

		if (!k)
		{
			return std::unexpected(k.error());
		}

		std::int32_t value{};

		const auto err = nvs_get_i32(
			handle_, k->c_str(), &value);

		if (err != ESP_OK)
		{
			return std::unexpected(err);
		}

		return value;
	}

	std::expected<std::uint32_t, esp_err_t>
	NvsService::get_u32(std::string_view key) const
	{
		if (auto r = require_open(); !r)
		{
			return std::unexpected(r.error());
		}

		auto k = validate_name(key, NVS_KEY_NAME_MAX_SIZE - 1);

		if (!k)
		{
			return std::unexpected(k.error());
		}

		std::uint32_t value{};

		const auto err = nvs_get_u32(
			handle_, k->c_str(), &value);

		if (err != ESP_OK)
		{
			return std::unexpected(err);
		}

		return value;
	}

	std::expected<std::uint64_t, esp_err_t>
	NvsService::get_u64(std::string_view key) const
	{
		if (auto r = require_open(); !r)
		{
			return std::unexpected(r.error());
		}

		auto k = validate_name(key, NVS_KEY_NAME_MAX_SIZE - 1);

		if (!k)
		{
			return std::unexpected(k.error());
		}

		std::uint64_t value{};

		const auto err = nvs_get_u64(
			handle_, k->c_str(), &value);

		if (err != ESP_OK)
		{
			return std::unexpected(err);
		}

		return value;
	}

	std::expected<bool, esp_err_t>
	NvsService::get_bool(std::string_view key) const
	{
		if (auto r = require_open(); !r)
		{
			return std::unexpected(r.error());
		}

		auto k = validate_name(key, NVS_KEY_NAME_MAX_SIZE - 1);

		if (!k)
		{
			return std::unexpected(k.error());
		}

		std::uint8_t value{};

		const auto err = nvs_get_u8(
			handle_, k->c_str(), &value);

		if (err != ESP_OK)
		{
			return std::unexpected(err);
		}

		if (value > 1)
		{
			return std::unexpected(ESP_ERR_INVALID_RESPONSE);
		}

		return value == 1;
	}

	std::expected<std::string, esp_err_t>
	NvsService::get_string(std::string_view key) const
	{
		if (auto r = require_open(); !r)
		{
			return std::unexpected(r.error());
		}

		auto k = validate_name(key, NVS_KEY_NAME_MAX_SIZE - 1);

		if (!k)
		{
			return std::unexpected(k.error());
		}

		std::size_t size{};

		auto err = nvs_get_str(
			handle_, k->c_str(), nullptr, &size);

		if (err != ESP_OK)
		{
			return std::unexpected(err);
		}

		std::string value(size, '\0');

		err = nvs_get_str(
			handle_, k->c_str(), value.data(), &size);

		if (err != ESP_OK)
		{
			return std::unexpected(err);
		}

		if (!value.empty() && value.back() == '\0')
		{
			value.pop_back();
		}

		return value;
	}

	std::expected<std::vector<std::byte>, esp_err_t>
	NvsService::get_blob(std::string_view key) const
	{
		if (auto r = require_open(); !r)
		{
			return std::unexpected(r.error());
		}

		auto k = validate_name(key, NVS_KEY_NAME_MAX_SIZE - 1);

		if (!k)
		{
			return std::unexpected(k.error());
		}

		std::size_t size{};

		auto err = nvs_get_blob(
			handle_, k->c_str(), nullptr, &size);

		if (err != ESP_OK)
		{
			return std::unexpected(err);
		}

		std::vector<std::byte> value(size);

		if (size != 0)
		{
			err = nvs_get_blob(
				handle_, k->c_str(), value.data(), &size);

			if (err != ESP_OK)
			{
				return std::unexpected(err);
			}

			value.resize(size);
		}

		return value;
	}

	std::expected<void, esp_err_t>
	NvsService::set_i32(
		std::string_view key, std::int32_t value)
	{

		if (auto r = require_open(); !r)
		{
			return std::unexpected(r.error());
		}

		auto k = validate_name(key, NVS_KEY_NAME_MAX_SIZE - 1);

		if (!k)
		{
			return std::unexpected(k.error());
		}

		const auto err = nvs_set_i32(
			handle_, k->c_str(), value);

		if (err != ESP_OK)
		{
			return std::unexpected(err);
		}

		return {};
	}

	std::expected<void, esp_err_t>
	NvsService::set_u32(
		std::string_view key, std::uint32_t value)
	{

		if (auto r = require_open(); !r)
		{
			return std::unexpected(r.error());
		}

		auto k = validate_name(key, NVS_KEY_NAME_MAX_SIZE - 1);

		if (!k)
		{
			return std::unexpected(k.error());
		}

		const auto err = nvs_set_u32(
			handle_, k->c_str(), value);

		if (err != ESP_OK)
		{
			return std::unexpected(err);
		}

		return {};
	}

	std::expected<void, esp_err_t>
	NvsService::set_u64(
		std::string_view key, std::uint64_t value)
	{

		if (auto r = require_open(); !r)
		{
			return std::unexpected(r.error());
		}

		auto k = validate_name(key, NVS_KEY_NAME_MAX_SIZE - 1);

		if (!k)
		{
			return std::unexpected(k.error());
		}

		const auto err = nvs_set_u64(
			handle_, k->c_str(), value);

		if (err != ESP_OK)
		{
			return std::unexpected(err);
		}

		return {};
	}

	std::expected<void, esp_err_t>
	NvsService::set_bool(
		std::string_view key, bool value)
	{

		if (auto r = require_open(); !r)
		{
			return std::unexpected(r.error());
		}

		auto k = validate_name(key, NVS_KEY_NAME_MAX_SIZE - 1);

		if (!k)
		{
			return std::unexpected(k.error());
		}

		const auto err = nvs_set_u8(
			handle_, k->c_str(), value ? 1 : 0);

		if (err != ESP_OK)
		{
			return std::unexpected(err);
		}

		return {};
	}

	std::expected<void, esp_err_t>
	NvsService::set_string(
		std::string_view key, std::string_view value)
	{

		if (auto r = require_open(); !r)
		{
			return std::unexpected(r.error());
		}

		auto k = validate_name(key, NVS_KEY_NAME_MAX_SIZE - 1);

		if (!k)
		{
			return std::unexpected(k.error());
		}

		if (value.find('\0') != std::string_view::npos)
		{
			return std::unexpected(ESP_ERR_INVALID_ARG);
		}

		const std::string owned(value);

		const auto err = nvs_set_str(
			handle_, k->c_str(), owned.c_str());

		if (err != ESP_OK)
		{
			return std::unexpected(err);
		}

		return {};
	}

	std::expected<void, esp_err_t>
	NvsService::set_blob(
		std::string_view key,
		std::span<const std::byte> value)
	{

		if (auto r = require_open(); !r)
		{
			return std::unexpected(r.error());
		}

		auto k = validate_name(key, NVS_KEY_NAME_MAX_SIZE - 1);

		if (!k)
		{
			return std::unexpected(k.error());
		}

		const auto err = nvs_set_blob(
			handle_, k->c_str(), value.data(), value.size());

		if (err != ESP_OK)
		{
			return std::unexpected(err);
		}

		return {};
	}

	std::expected<void, esp_err_t>
	NvsService::erase(std::string_view key)
	{
		if (auto r = require_open(); !r)
		{
			return std::unexpected(r.error());
		}

		auto k = validate_name(key, NVS_KEY_NAME_MAX_SIZE - 1);

		if (!k)
		{
			return std::unexpected(k.error());
		}

		const auto err = nvs_erase_key(
			handle_, k->c_str());

		if (err != ESP_OK)
		{
			return std::unexpected(err);
		}

		return {};
	}

	std::expected<void, esp_err_t>
	NvsService::erase_all()
	{
		if (auto r = require_open(); !r)
		{
			return std::unexpected(r.error());
		}

		const auto err = nvs_erase_all(handle_);

		if (err != ESP_OK)
		{
			return std::unexpected(err);
		}

		return {};
	}

	std::expected<void, esp_err_t>
	NvsService::commit()
	{
		if (auto r = require_open(); !r)
		{
			return std::unexpected(r.error());
		}

		const auto err = nvs_commit(handle_);

		if (err != ESP_OK)
		{
			return std::unexpected(err);
		}

		return {};
	}

} // namespace storage