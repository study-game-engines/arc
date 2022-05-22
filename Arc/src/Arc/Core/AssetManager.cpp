#include "arcpch.h"
#include "AssetManager.h"

namespace ArcEngine
{
	eastl::unordered_map<eastl::string, Ref<Texture2D>> AssetManager::m_Texture2DMap;
	eastl::unordered_map<eastl::string, Ref<TextureCubemap>> AssetManager::m_TextureCubeMap;

	void AssetManager::Init()
	{
	}

	void AssetManager::Shutdown()
	{
		m_Texture2DMap.clear();
		m_TextureCubeMap.clear();
	}

	Ref<Texture2D> AssetManager::GetTexture2D(eastl::string path)
	{
		if (m_Texture2DMap.find(path) != m_Texture2DMap.end())
			return m_Texture2DMap.at(path);

		Ref<Texture2D> texture = Texture2D::Create(path);
		m_Texture2DMap.emplace(path, texture);
		return texture;
	}

	Ref<TextureCubemap> AssetManager::GetTextureCubemap(eastl::string path)
	{
		if (m_TextureCubeMap.find(path) != m_TextureCubeMap.end())
			return m_TextureCubeMap.at(path);

		Ref<TextureCubemap> texture = TextureCubemap::Create(path);
		m_TextureCubeMap.emplace(path, texture);
		return texture;
	}
}
