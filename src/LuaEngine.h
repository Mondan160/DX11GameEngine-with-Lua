#pragma once

#include <Windows.h>
#include <string>
#include <vector>
#include <memory>  // std::unique_ptr のために必要
#include <DirectXMath.h>
#include "sol/sol.hpp"

ID3D11ShaderResourceView* CreateTextureFromFile(const char* filename);

struct CubeInstance {
    DirectX::XMFLOAT3 pos;
    float Size;
    DirectX::XMFLOAT3 Rotate;
    struct ID3D11ShaderResourceView* pTexture;
};
extern std::vector<CubeInstance> g_Cubes;

class ScriptEngine {
private:
    inline static std::unique_ptr<sol::state> lua_ptr;

public:
    // ImGuiから触れるように public に
    inline static std::vector<std::string> LogMessage;

    static void Init() {
        lua_ptr = std::make_unique<sol::state>();
        lua_ptr->open_libraries(sol::lib::base, sol::lib::math, sol::lib::package);

        // --- print ---
        lua_ptr->set_function("print", [](std::string msg) {
            OutputDebugStringA((msg + "\n").c_str());
            LogMessage.push_back(msg);
            if (LogMessage.size() > 50) LogMessage.erase(LogMessage.begin());
            });

        // --- spawn_cube ---
        lua_ptr->set_function("spawn_cube", [](float x, float y, float z, float size, float rx, float ry, float rz, std::string texPath) {
            CubeInstance cube;
            cube.pos = DirectX::XMFLOAT3(x, y, z);
            cube.Size = size;
            cube.Rotate = DirectX::XMFLOAT3(rx, ry, rz);

            if (!texPath.empty()) {
                cube.pTexture = CreateTextureFromFile(texPath.c_str());
            }
            else {
                cube.pTexture = nullptr;
            }
            
            g_Cubes.push_back(cube);
            });
    }

    static void ExecuteFile(const std::string& filename) {
        if (!lua_ptr) return;
        try {
            // シンプルにスクリプトを実行するだけ
            auto result = lua_ptr->script_file(filename);
            if (!result.valid()) {
                sol::error err = result;
                MessageBoxA(NULL, err.what(), "Lua Logic Error", MB_OK);
            }
        }
        catch (const sol::error& e) {
            MessageBoxA(NULL, e.what(), "Lua Syntax/Exception", MB_OK);
        }
    }
};