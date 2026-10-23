#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>
namespace echo {
class Resources;
enum class AssetKind : std::uint8_t { Image, Audio, Script, Shader, Font, Other };
struct AssetRecord {
    std::uint64_t id{};
    std::string path, name, tags, source;
    AssetKind kind = AssetKind::Other;
    std::uint32_t width{}, height{};
    std::uint64_t bytes{};
    double duration{};
};
struct AssetCatalog {
    std::uint64_t nextId = 1;
    std::vector<AssetRecord> entries;
};
std::string assetReference(std::uint64_t);
std::uint64_t assetId(std::string_view);
const AssetRecord* findAsset(const AssetCatalog&, std::uint64_t);
const AssetRecord* findAssetPath(const AssetCatalog&, std::string_view);
void validateAssets(const AssetCatalog&);
AssetCatalog loadAssets(const std::string&);
std::string saveAssets(const AssetCatalog&);
AssetRecord probeAsset(const std::filesystem::path&, std::string relative);
void scanAssets(const Resources&, AssetCatalog&);
struct AssetUsage {
    std::string file, location;
};
std::vector<AssetUsage> assetUsages(const Resources&, const AssetRecord&);
AssetRecord importAsset(Resources&, AssetCatalog&, const std::filesystem::path&,
                        std::string_view folder);
void moveAsset(Resources&, AssetCatalog&, std::uint64_t, std::string_view target);
void relinkAsset(Resources&, AssetCatalog&, std::uint64_t, const std::filesystem::path&);
void deleteAsset(Resources&, AssetCatalog&, std::uint64_t);
void deleteAssets(Resources&, AssetCatalog&, const std::vector<std::uint64_t>&);
} // namespace echo
