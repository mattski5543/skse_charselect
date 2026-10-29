// Copies the player's face tint from the graphics card into a texture file,
// and writes a skin-tone placeholder when there isn't one yet (see Tint.h).

#include "Tint.h"

#include "Paths.h"
#include "core/Dds.h"
#include "core/Encoding.h"

namespace charselect::tint
{
	static_assert(dds::kR16G16B16A16_FLOAT == static_cast<std::uint32_t>(REX::W32::DXGI_FORMAT_R16G16B16A16_FLOAT));
	static_assert(dds::kR10G10B10A2_UNORM == static_cast<std::uint32_t>(REX::W32::DXGI_FORMAT_R10G10B10A2_UNORM));
	static_assert(dds::kR8G8B8A8_UNORM == static_cast<std::uint32_t>(REX::W32::DXGI_FORMAT_R8G8B8A8_UNORM));
	static_assert(dds::kR8G8B8A8_UNORM_SRGB == static_cast<std::uint32_t>(REX::W32::DXGI_FORMAT_R8G8B8A8_UNORM_SRGB));
	static_assert(dds::kB8G8R8A8_UNORM == static_cast<std::uint32_t>(REX::W32::DXGI_FORMAT_B8G8R8A8_UNORM));
	static_assert(dds::kB8G8R8A8_UNORM_SRGB == static_cast<std::uint32_t>(REX::W32::DXGI_FORMAT_B8G8R8A8_UNORM_SRGB));

	namespace
	{
		// ARGB of the SkinTone layer in the preset, or a pale default.
		std::uint32_t SkinTone(std::uint32_t a_characterId)
		{
			try {
				std::ifstream in(paths::PresetPath(a_characterId), std::ios::binary);
				return dds::SkinToneFromPreset(in);
			} catch (const std::exception& e) {
				logger::warn("Tint: could not read skin tone from preset: {}", e.what());
			}
			return dds::kDefaultSkinTone;
		}

		// The D3D texture behind an engine texture, with its description. It
		// holds a reference the caller must Release().
		REX::W32::ID3D11Texture2D* D3DTexture(RE::NiSourceTexture* a_texture, REX::W32::D3D11_TEXTURE2D_DESC& a_desc)
		{
			// Loaded and rendered textures both keep their GPU data at 0x48 (the
			// field RaceMenu reads too); anything else, don't touch.
			const auto* rtti = a_texture ? a_texture->GetRTTI() : nullptr;
			const std::string_view type = rtti && rtti->GetName() ? rtti->GetName() : "";
			if (type != "NiSourceTexture" && type != "NiRenderedTexture" && type != "BSRenderedTexture") {
				if (a_texture) {
					logger::debug("Tint: texture of type '{}' skipped", type);
				}
				return nullptr;
			}
			auto* renderer = a_texture->rendererTexture;
			if (!renderer) {
				return nullptr;
			}
			REX::W32::ID3D11Resource* resource = renderer->texture;
			if (resource) {
				resource->AddRef();
			} else if (renderer->resourceView) {
				renderer->resourceView->GetResource(&resource);  // adds a reference
			}
			if (!resource) {
				return nullptr;
			}
			REX::W32::D3D11_RESOURCE_DIMENSION dimension{};
			resource->GetType(&dimension);
			if (dimension != REX::W32::D3D11_RESOURCE_DIMENSION_TEXTURE2D) {
				resource->Release();
				return nullptr;
			}
			auto* texture = static_cast<REX::W32::ID3D11Texture2D*>(resource);
			texture->GetDesc(&a_desc);
			return texture;
		}
	}

	bool ExportFromPlayer(std::uint32_t a_characterId)
	{
		const auto id = paths::PresetName(a_characterId);
		auto*      player = RE::PlayerCharacter::GetSingleton();
		auto*      face = player ? player->GetHeadPartObject(RE::BGSHeadPart::HeadPartType::kFace) : nullptr;
		auto*      geometry = face ? face->AsGeometry() : nullptr;
		if (!geometry) {
			logger::warn("Tint: {}: the player has no face geometry (3D loaded: {})", id, player && player->Is3DLoaded());
			return false;
		}
		auto* shader = geometry->lightingShaderProp_cast();
		auto* material = shader ? shader->material : nullptr;
		if (!material) {
			logger::warn("Tint: {}: the face has no lighting shader material", id);
			return false;
		}
		if (material->GetFeature() != RE::BSShaderMaterial::Feature::kFaceGen) {
			logger::warn("Tint: {}: the face material is feature {}, not FaceGen; no tint mask to export", id,
				static_cast<int>(material->GetFeature()));
			return false;
		}

		// The tint mask: a render target if there is one (how RaceMenu's
		// export finds it), else the tint slot when it's uncompressed. On the
		// user's setup the player's mask is a 512x512 RGBA, 1-mip texture in
		// the tint slot without the render-target flag (0.5.17); a tint loaded
		// from a file (an NPC's) would be compressed.
		auto*                                                      facegen = static_cast<RE::BSLightingShaderMaterialFacegen*>(material);
		// Held references, so a texture the game swaps out meanwhile stays valid.
		const std::array<std::pair<const char*, RE::NiPointer<RE::NiSourceTexture>>, 3> slots{ { { "tint", facegen->tintTexture },
			{ "detail", facegen->detailTexture }, { "subsurface", facegen->subsurfaceTexture } } };
		REX::W32::ID3D11Texture2D*      source = nullptr;
		REX::W32::D3D11_TEXTURE2D_DESC  desc{};
		for (const auto& [slot, texture] : slots) {
			REX::W32::D3D11_TEXTURE2D_DESC d{};
			auto*                          d3d = D3DTexture(texture.get(), d);
			const bool                     target = d3d && (d.bindFlags & REX::W32::D3D11_BIND_RENDER_TARGET);
			logger::debug("Tint: {}: face {} slot: {}", id, slot,
				!texture ? "empty" : !d3d ? "no 2D texture" : std::format("{}x{} format {} mips {} {}", d.width, d.height, static_cast<int>(d.format), d.mipLevels, target ? "render target" : "loaded"));
			if (target && (!source || !(desc.bindFlags & REX::W32::D3D11_BIND_RENDER_TARGET))) {
				// A render target beats the tint-slot fallback.
				if (source) {
					source->Release();
				}
				source = d3d;
				desc = d;
			} else if (!source && d3d && std::string_view(slot) == "tint" && dds::BytesPerPixel(d.format) > 0 && d.mipLevels == 1) {
				source = d3d;
				desc = d;
			} else if (d3d) {
				d3d->Release();
			}
		}
		if (source) {
			logger::debug("Tint: {}: using the {} texture", id, (desc.bindFlags & REX::W32::D3D11_BIND_RENDER_TARGET) ? "render-target" : "uncompressed tint-slot");
		}
		if (!source) {
			logger::warn("Tint: {}: no rendered tint mask on the face", id);
			return false;
		}
		const auto release = [&]() { source->Release(); };
		if (dds::BytesPerPixel(desc.format) == 0 || desc.sampleDesc.count != 1) {
			logger::warn("Tint: {}: tint mask format {} (samples {}) isn't supported", id, static_cast<int>(desc.format), desc.sampleDesc.count);
			release();
			return false;
		}

		// GPU -> CPU: copy the top mip into a staging texture and read it.
		auto* device = RE::BSGraphics::Renderer::GetDevice();
		auto* renderer = RE::BSGraphics::Renderer::GetRendererDataSingleton();
		auto* context = renderer ? renderer->context : nullptr;
		auto* manager = RE::BSGraphics::Renderer::GetSingleton();
		if (!device || !context || !manager) {
			logger::warn("Tint: {}: no D3D11 device/context", id);
			release();
			return false;
		}
		REX::W32::D3D11_TEXTURE2D_DESC stagingDesc = desc;
		stagingDesc.mipLevels = 1;
		stagingDesc.arraySize = 1;
		stagingDesc.usage = REX::W32::D3D11_USAGE_STAGING;
		stagingDesc.bindFlags = 0;
		stagingDesc.cpuAccessFlags = REX::W32::D3D11_CPU_ACCESS_READ;
		stagingDesc.miscFlags = 0;
		REX::W32::ID3D11Texture2D* staging = nullptr;
		if (device->CreateTexture2D(&stagingDesc, nullptr, &staging) < 0 || !staging) {
			logger::warn("Tint: {}: could not create a staging texture", id);
			release();
			return false;
		}
		// The immediate context isn't thread-safe and this runs on an SKSE task
		// thread while the game renders: take the renderer's lock, as RaceMenu
		// does for its GPU morph updates. Unguarded, this crashed the graphics
		// driver (0.5.25). The pixels are copied out under the lock and written
		// to disk after it, so rendering waits only for the copy.
		std::vector<std::uint8_t> pixels;
		std::uint32_t             rowPitch = 0;
		manager->Lock();
		context->CopySubresourceRegion(staging, 0, 0, 0, 0, source, 0, nullptr);
		REX::W32::D3D11_MAPPED_SUBRESOURCE mapped{};
		if (context->Map(staging, 0, REX::W32::D3D11_MAP_READ, 0, &mapped) >= 0) {
			if (mapped.data) {
				rowPitch = mapped.rowPitch;
				const auto* data = static_cast<const std::uint8_t*>(mapped.data);
				pixels.assign(data, data + static_cast<std::size_t>(rowPitch) * desc.height);
			}
			context->Unmap(staging, 0);
		}
		manager->Unlock();
		release();
		staging->Release();
		bool written = false;
		if (!pixels.empty()) {
			std::error_code ec;
			std::filesystem::create_directories(Path(a_characterId).parent_path(), ec);
			written = dds::Write(Path(a_characterId), desc.width, desc.height, static_cast<std::uint32_t>(desc.format), pixels.data(), rowPitch);
		}
		if (written) {
			logger::info("Tint: {}: exported the face tint ({}x{}, format {}) to {}", id, desc.width, desc.height, static_cast<int>(desc.format),
				text::PathUtf8(Path(a_characterId)));
		} else {
			logger::warn("Tint: {}: could not read or write the tint mask", id);
		}
		return written;
	}

	std::filesystem::path Path(std::uint32_t a_characterId)
	{
		return std::filesystem::current_path() / "Data" / "Textures" / "CharGen" / "Exported" /
		       std::format("{}.dds", paths::PresetName(a_characterId));
	}

	bool HasRealExport(std::uint32_t a_characterId)
	{
		std::error_code ec;
		const auto      size = std::filesystem::file_size(Path(a_characterId), ec);
		return !ec && size > dds::kPlaceholderBytes;
	}

	void EnsurePlaceholder(std::uint32_t a_characterId)
	{
		const auto path = Path(a_characterId);
		if (std::filesystem::exists(path)) {
			return;
		}
		const auto argb = SkinTone(a_characterId);
		std::error_code ec;
		std::filesystem::create_directories(path.parent_path(), ec);
		if (!dds::WritePlaceholder(path, argb)) {
			logger::error("Tint: cannot write placeholder {}", text::PathUtf8(path));
			return;
		}
		logger::info("Tint: no RaceMenu tint export for {}; wrote a flat skin-tone placeholder #{:06X} at {}",
			paths::PresetName(a_characterId), argb & 0x00FFFFFF, text::PathUtf8(path));
	}
}
