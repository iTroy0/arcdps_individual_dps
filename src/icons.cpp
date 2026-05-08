#include "icons.h"

#include <d3d11.h>
#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "stb_image.h"

#include "log.h"
#include "util.h"

namespace idps {

namespace {
    ID3D11Device*                                     g_device = nullptr;
    std::unordered_map<std::string, ID3D11ShaderResourceView*> g_views;
    bool                                              g_loaded = false;

    const std::unordered_map<uint32_t, const char*>& elite_map() {
        static const std::unordered_map<uint32_t, const char*> m = {
            // Guardian
            {27, "e101"}, {62, "e102"}, {65, "e103"}, {81, "e104"},
            // Warrior
            {18, "e201"}, {61, "e202"}, {68, "e203"}, {74, "e204"},
            // Engineer
            {43, "e301"}, {57, "e302"}, {70, "e303"}, {75, "e304"},
            // Ranger
            { 5, "e401"}, {55, "e402"}, {72, "e403"}, {78, "e404"},
            // Thief
            { 7, "e501"}, {58, "e502"}, {71, "e503"}, {77, "e504"},
            // Elementalist
            {48, "e601"}, {56, "e602"}, {67, "e603"}, {80, "e604"},
            // Mesmer
            {40, "e701"}, {59, "e702"}, {66, "e703"}, {73, "e704"},
            // Necromancer
            {34, "e801"}, {60, "e802"}, {64, "e803"}, {76, "e804"},
            // Revenant
            {52, "e901"}, {63, "e902"}, {69, "e903"}, {79, "e904"},
        };
        return m;
    }

    bool read_file(const std::string& path, std::vector<unsigned char>& bytes) {
        FILE* f = nullptr;
        if (fopen_s(&f, path.c_str(), "rb") != 0 || !f) return false;
        std::fseek(f, 0, SEEK_END);
        long sz = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        if (sz <= 0) { std::fclose(f); return false; }
        bytes.resize(static_cast<size_t>(sz));
        size_t got = std::fread(bytes.data(), 1, bytes.size(), f);
        std::fclose(f);
        return got == bytes.size();
    }

    WORD resource_id_for(const char* stem) {
        // "001".."009" -> 1..9
        if (std::strlen(stem) == 3 && stem[0] == '0' && stem[1] == '0' &&
            stem[2] >= '1' && stem[2] <= '9') {
            return static_cast<WORD>(stem[2] - '0');
        }
        // "e<prof><0><spec>" with prof 1..9, spec 1..4
        if (std::strlen(stem) == 4 && (stem[0] == 'e' || stem[0] == 'E') &&
            stem[1] >= '1' && stem[1] <= '9' && stem[2] == '0' &&
            stem[3] >= '1' && stem[3] <= '4') {
            int prof = stem[1] - '0';
            int spec = stem[3] - '0';
            return static_cast<WORD>(10 + (prof - 1) * 4 + (spec - 1));
        }
        return 0;
    }

    bool read_resource(const char* stem, const unsigned char*& out_ptr, size_t& out_size) {
        HMODULE mod = self_module();
        if (!mod) return false;
        WORD id = resource_id_for(stem);
        if (id == 0) return false;

        HRSRC rsrc = FindResourceExA(mod, RT_RCDATA,
                                     MAKEINTRESOURCEA(id),
                                     MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL));
        if (!rsrc) rsrc = FindResourceA(mod, MAKEINTRESOURCEA(id), RT_RCDATA);
        if (!rsrc) return false;

        HGLOBAL glob = LoadResource(mod, rsrc);
        if (!glob) return false;
        void* data = LockResource(glob);
        DWORD size = SizeofResource(mod, rsrc);
        if (!data || size == 0) return false;
        out_ptr = static_cast<const unsigned char*>(data);
        out_size = size;
        return true;
    }

    ID3D11ShaderResourceView* create_srv_from_bytes(const unsigned char* bytes, size_t size) {
        int w = 0, h = 0, comp = 0;
        unsigned char* pixels = stbi_load_from_memory(
            bytes, static_cast<int>(size), &w, &h, &comp, 4);
        if (!pixels) return nullptr;

        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = w;
        desc.Height = h;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA init{};
        init.pSysMem = pixels;
        init.SysMemPitch = static_cast<UINT>(w * 4);

        ID3D11Texture2D* tex = nullptr;
        HRESULT hr = g_device->CreateTexture2D(&desc, &init, &tex);
        stbi_image_free(pixels);
        if (FAILED(hr) || !tex) return nullptr;

        D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc{};
        srv_desc.Format = desc.Format;
        srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srv_desc.Texture2D.MipLevels = 1;

        ID3D11ShaderResourceView* srv = nullptr;
        hr = g_device->CreateShaderResourceView(tex, &srv_desc, &srv);
        tex->Release();
        if (FAILED(hr)) return nullptr;
        return srv;
    }

    std::string default_icons_dir() {
        char path[MAX_PATH]{};
        HMODULE self = self_module();
        DWORD n = GetModuleFileNameA(self, path, MAX_PATH);
        if (n == 0 || n == MAX_PATH) return "individual_dps_icons";
        std::string dir(path, n);
        size_t slash = dir.find_last_of("\\/");
        if (slash == std::string::npos) return "individual_dps_icons";
        dir.resize(slash);
        dir += "\\individual_dps_icons";
        return dir;
    }

    void try_load(const std::string& dir, const char* stem) {
        const unsigned char* rdata = nullptr;
        size_t rsize = 0;
        if (read_resource(stem, rdata, rsize)) {
            if (auto* srv = create_srv_from_bytes(rdata, rsize)) {
                g_views[stem] = srv;
                return;
            }
        }
        std::string path = dir + "\\" + stem + ".png";
        std::vector<unsigned char> file_data;
        if (!read_file(path, file_data)) return;
        if (auto* srv = create_srv_from_bytes(file_data.data(), file_data.size())) {
            g_views[stem] = srv;
        }
    }
}

void icons_set_device(ID3D11Device* device) {
    if (!device) return;
    if (g_device) g_device->Release();
    g_device = device;
    g_device->AddRef();
}

void icons_ensure_loaded(const std::string& icons_dir) {
    if (g_loaded || !g_device) return;
    g_loaded = true;
    std::string dir = icons_dir.empty() ? default_icons_dir() : icons_dir;

    for (int p = 1; p <= 9; ++p) {
        char stem[8];
        std::snprintf(stem, sizeof(stem), "00%d", p);
        try_load(dir, stem);
    }
    for (int p = 1; p <= 9; ++p) {
        for (int s = 1; s <= 4; ++s) {
            char stem[8];
            std::snprintf(stem, sizeof(stem), "e%d0%d", p, s);
            try_load(dir, stem);
        }
    }
    log_line("icons loaded=%zu", g_views.size());
}

void icons_shutdown() {
    // Plugin unload runs on arcdps's release thread, not the D3D11 render
    // thread that created the resources. Calling Release() on SRVs or the
    // device from the wrong thread crashed inside the d3d driver on at
    // least one NVIDIA setup (same class of issue that forced SRV creation
    // to be deferred to mod_imgui in the first place — see icons.h). On
    // process exit the OS reclaims everything; on hot /reloadarcdps the
    // SRVs and one device-ref leak per reload (~9*4 SRVs, rare path).
    // Trade a small leak on the rare reload path for stable shutdown.
    g_views.clear();
    g_device = nullptr;
    g_loaded = false;
}

uint64_t icon_for(uint32_t prof, uint32_t elite) {
    if (elite != 0) {
        auto it = elite_map().find(elite);
        if (it != elite_map().end()) {
            auto v = g_views.find(it->second);
            if (v != g_views.end()) return reinterpret_cast<uint64_t>(v->second);
        }
    }
    if (prof >= 1 && prof <= 9) {
        char stem[8];
        std::snprintf(stem, sizeof(stem), "00%u", prof);
        auto v = g_views.find(stem);
        if (v != g_views.end()) return reinterpret_cast<uint64_t>(v->second);
    }
    return 0;
}

} // namespace idps
