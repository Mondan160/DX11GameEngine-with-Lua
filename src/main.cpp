#define STB_IMAGE_IMPLEMENTATION 
#include "stb_image.h"

#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"

#include <commdlg.h>
#pragma comment(lib, "comdlg32.lib")

#include <Windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <vector>  // 追加
#include <string>  // デバッグ表示用
#include "Shader/Shader.h"
#include "Imgui/imgui.h"
#include "Imgui/imgui_impl_dx11.h"
#include "Imgui/imgui_impl_win32.h"
#include <shellapi.h>

// Lua
#include "sol/sol.hpp"
#include "LuaEngine.h"

using namespace DirectX;

struct Vertex { float x, y, z; float r, g, b, a; float u, v; };
struct ConstantBuffer { XMMATRIX mWorld; XMMATRIX mView; XMMATRIX mProjection; };


struct ModelData {
    ID3D11Buffer* pVB = nullptr;
    ID3D11Buffer* pIB = nullptr;
    UINT indexCount = 0;
};

struct MeshInstance {
    XMFLOAT3 pos = { 0, 0, 0 };
    float Size = 1.0f;
    XMFLOAT3 Rotate;
    ModelData* pModel = nullptr;
    ID3D11ShaderResourceView* pTex = nullptr;
};

static ID3D11Buffer* g_pVB = nullptr;
static ID3D11Buffer* g_pIB = nullptr;
static ID3D11Buffer* g_pCB = nullptr;
static ID3D11VertexShader* g_pVS = nullptr;
static ID3D11PixelShader* g_pPS = nullptr;
static ID3D11InputLayout* g_pLayout = nullptr;
static ID3D11RasterizerState* g_pRS = nullptr;
static ID3D11PixelShader* g_pPSGrid = nullptr;

static ID3D11ShaderResourceView* g_pTexture = nullptr;
static ID3D11SamplerState* g_pSampler = nullptr;

static ID3D11Device* Device = nullptr;
static ID3D11DeviceContext* Context = nullptr;

static std::vector<CubeInstance> g_Cubes;

static std::vector<MeshInstance> g_Meshes;

// Camera movement
static float g_camX = 0.0f, g_camY = 5.0f, g_camZ = -10.0f;
static float g_Yaw = 0.0f, g_Pitch = 0.0f;
static float g_FovDegree = 70.0f;

float speed = 0.1f;

static float CubeSize = 1.0f;

// Load image
std::string OpenFile(HWND owner) {
    OPENFILENAMEA ofn;       
    char szFile[260] = { 0 }; 

    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;      
    ofn.lpstrFile = szFile;     
    ofn.nMaxFile = sizeof(szFile);

    ofn.lpstrFilter = "Image Files\0*.png;*.jpg;*.jpeg;*.tga\0All Files\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = NULL; 
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameA(&ofn)) {
        return std::string(szFile);
    }
    return "";
}

// load Model -.obj-
std::string OpenFileModel(HWND owner) {
    OPENFILENAMEA ofn;
    char szFile[260] = { 0 };

    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);

    ofn.lpstrFilter = "Model Files\0*.obj;*.fbx\0All Files\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = NULL;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameA(&ofn)) {
        return std::string(szFile);
    }
    return "";
}

ModelData LoadOBJ(const char* filename) {
    ModelData model;
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn, err;

    if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, filename)) {
        return model;
    }

    std::vector<Vertex> vertices;
    std::vector<unsigned short> indices;

    for (const auto& shape : shapes) {
        for (const auto& index : shape.mesh.indices) {
            Vertex v = {};
            v.x = attrib.vertices[3 * index.vertex_index + 0];
            v.y = attrib.vertices[3 * index.vertex_index + 1];
            v.z = attrib.vertices[3 * index.vertex_index + 2];
            v.r = v.g = v.b = v.a = 1.0f;

            if (index.texcoord_index >= 0) {
                v.u = attrib.texcoords[2 * index.texcoord_index + 0];
                v.v = 1.0f - attrib.texcoords[2 * index.texcoord_index + 1];
            }
            vertices.push_back(v);
            indices.push_back((unsigned short)indices.size());
        }
    }

    D3D11_BUFFER_DESC vbd = { (UINT)(sizeof(Vertex) * vertices.size()), D3D11_USAGE_DEFAULT, D3D11_BIND_VERTEX_BUFFER };
    D3D11_SUBRESOURCE_DATA vsd = { vertices.data() };
    Device->CreateBuffer(&vbd, &vsd, &model.pVB);

    D3D11_BUFFER_DESC ibd = { (UINT)(sizeof(unsigned short) * indices.size()), D3D11_USAGE_DEFAULT, D3D11_BIND_INDEX_BUFFER };
    D3D11_SUBRESOURCE_DATA isd = { indices.data() };
    Device->CreateBuffer(&ibd, &isd, &model.pIB);

    model.indexCount = (UINT)indices.size();
    return model;
}


ID3D11ShaderResourceView* CreateTextureFromFile(const char* filename) {
    int width, height, channels;

    stbi_set_flip_vertically_on_load(true);

    unsigned char* data = stbi_load(filename, &width, &height, &channels, 4);
    if (!data) return nullptr;

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = (UINT)width;
    desc.Height = (UINT)height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA subData = {};
    subData.pSysMem = data;
    subData.SysMemPitch = (UINT)width * 4;

    ID3D11Texture2D* pTexture2D = nullptr;
    HRESULT hr = Device->CreateTexture2D(&desc, &subData, &pTexture2D);

    ID3D11ShaderResourceView* pSRV = nullptr;
    if (SUCCEEDED(hr) && pTexture2D) {
        Device->CreateShaderResourceView(pTexture2D, nullptr, &pSRV);
        pTexture2D->Release();
    }

    stbi_image_free(data);

    return pSRV;
}

extern "C" __declspec(dllexport) void CheckEngine(HWND hwnd, ID3D11Device* p_Device, ID3D11DeviceContext* pContext) {
    Device = p_Device;
    Context = pContext;

    ImGui::CreateContext();
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(Device, Context);

    Vertex vertices[] = {
        { -0.5f,  0.5f, -0.5f,  1, 1, 1, 1,  0, 0 }, {  0.5f,  0.5f, -0.5f,  1, 1, 1, 1,  1, 0 },
        {  0.5f, -0.5f, -0.5f,  1, 1, 1, 1,  1, 1 }, { -0.5f, -0.5f, -0.5f,  1, 1, 1, 1,  0, 1 },
        { -0.5f,  0.5f,  0.5f,  1, 1, 1, 1,  1, 0 }, {  0.5f,  0.5f,  0.5f,  1, 1, 1, 1,  0, 0 },
        {  0.5f, -0.5f,  0.5f,  1, 1, 1, 1,  0, 1 }, { -0.5f, -0.5f,  0.5f,  1, 1, 1, 1,  1, 1 },
        { -0.5f,  0.5f,  0.5f,  1, 1, 1, 1,  0, 0 }, {  0.5f,  0.5f,  0.5f,  1, 1, 1, 1,  1, 0 },
        {  0.5f,  0.5f, -0.5f,  1, 1, 1, 1,  1, 1 }, { -0.5f,  0.5f, -0.5f,  1, 1, 1, 1,  0, 1 },
        { -0.5f, -0.5f,  0.5f,  1, 1, 1, 1,  0, 1 }, {  0.5f, -0.5f,  0.5f,  1, 1, 1, 1,  1, 1 },
        {  0.5f, -0.5f, -0.5f,  1, 1, 1, 1,  1, 0 }, { -0.5f, -0.5f, -0.5f,  1, 1, 1, 1,  0, 0 },
        {  0.5f,  0.5f, -0.5f,  1, 1, 1, 1,  0, 0 }, {  0.5f,  0.5f,  0.5f,  1, 1, 1, 1,  1, 0 },
        {  0.5f, -0.5f,  0.5f,  1, 1, 1, 1,  1, 1 }, {  0.5f, -0.5f, -0.5f,  1, 1, 1, 1,  0, 1 },
        { -0.5f,  0.5f, -0.5f,  1, 1, 1, 1,  1, 0 }, { -0.5f,  0.5f,  0.5f,  1, 1, 1, 1,  0, 0 },
        { -0.5f, -0.5f,  0.5f,  1, 1, 1, 1,  0, 1 }, { -0.5f, -0.5f, -0.5f,  1, 1, 1, 1,  1, 1 }
    };
    unsigned short indices[] = {
        0,1,2, 0,2,3, 4,6,5, 4,7,6, 8,9,10, 8,10,11,
        12,14,13, 12,15,14, 16,17,18, 16,18,19, 20,22,21, 20,23,22
    };

    D3D11_BUFFER_DESC vbd = { sizeof(vertices), D3D11_USAGE_DEFAULT, D3D11_BIND_VERTEX_BUFFER };
    D3D11_SUBRESOURCE_DATA vsd = { vertices };
    Device->CreateBuffer(&vbd, &vsd, &g_pVB);

    D3D11_BUFFER_DESC ibd = { sizeof(indices), D3D11_USAGE_DEFAULT, D3D11_BIND_INDEX_BUFFER };
    D3D11_SUBRESOURCE_DATA isd = { indices };
    Device->CreateBuffer(&ibd, &isd, &g_pIB);

    D3D11_BUFFER_DESC cbd = { sizeof(ConstantBuffer), D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER };
    Device->CreateBuffer(&cbd, nullptr, &g_pCB);

    // Lua load
    ScriptEngine::Init();
    ScriptEngine::ExecuteFile("C:\\Users\\dafen\\source\\repos\\DX11GameEngine\\x64\\Release\\Scripts\\Init.lua");

    // Shader compile
    ID3DBlob* vsBlob = nullptr;
    D3DCompile(shaderCode, strlen(shaderCode), NULL, NULL, NULL, "VS", "vs_4_0", 0, 0, &vsBlob, NULL);
    Device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), NULL, &g_pVS);

    D3D11_INPUT_ELEMENT_DESC ied[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 28, D3D11_INPUT_PER_VERTEX_DATA, 0 }
    };
    Device->CreateInputLayout(ied, 3, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &g_pLayout);
    vsBlob->Release();

    ID3DBlob* psBlob = nullptr;
    D3DCompile(shaderCode, strlen(shaderCode), NULL, NULL, NULL, "PS", "ps_4_0", 0, 0, &psBlob, NULL);
    Device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), NULL, &g_pPS);
    psBlob->Release();

    D3D11_RASTERIZER_DESC rd = { D3D11_FILL_SOLID, D3D11_CULL_BACK, false, 0, 0.0f, 0.0f, true };
    Device->CreateRasterizerState(&rd, &g_pRS);
}

extern "C" __declspec(dllexport) void RenderEngine(ID3D11DeviceContext* pContext, ID3D11Device* pDevice, HWND hwnd) {
    // --- Viewport setting ---
    D3D11_VIEWPORT vp;
    RECT rc;
    GetClientRect(hwnd, &rc);
    float width = (float)(rc.right - rc.left);
    float height = (float)(rc.bottom - rc.top);
    vp.Width = width;
    vp.Height = height;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    vp.TopLeftX = 0;
    vp.TopLeftY = 0;
    pContext->RSSetViewports(1, &vp);

    // --- Input system ---
    static bool isUIMode = false;
    static bool tabPressed = false;
    if (GetAsyncKeyState(VK_TAB) & 0x8000) {
        if (!tabPressed) {
            isUIMode = !isUIMode;
            if (isUIMode) while (ShowCursor(TRUE) < 0);
            else while (ShowCursor(FALSE) >= 0);
            tabPressed = true;
        }
    }
    else { tabPressed = false; }

    // --- camera ---
    if (!isUIMode) {
        POINT p; GetCursorPos(&p);
        POINT c = { (int)width / 2, (int)height / 2 };
        ClientToScreen(hwnd, &c);
        g_Yaw += (p.x - c.x) * 0.002f;
        g_Pitch -= (p.y - c.y) * 0.002f;
        g_Pitch = __max(-1.5f, __min(1.5f, g_Pitch));
        SetCursorPos(c.x, c.y);
    }

    // Movement
    float lookX = cosf(g_Pitch) * sinf(g_Yaw);
    float lookY = sinf(g_Pitch);
    float lookZ = cosf(g_Pitch) * cosf(g_Yaw);

    float rightX = cosf(g_Yaw);
    float rightZ = -sinf(g_Yaw);

    if (!isUIMode) {
        if (GetAsyncKeyState('W') & 0x8000) { g_camX += lookX * speed; g_camZ += lookZ * speed; }
        if (GetAsyncKeyState('S') & 0x8000) { g_camX -= lookX * speed; g_camZ -= lookZ * speed; }
        if (GetAsyncKeyState('A') & 0x8000) { g_camX -= rightX * speed; g_camZ -= rightZ * speed; }
        if (GetAsyncKeyState('D') & 0x8000) { g_camX += rightX * speed; g_camZ += rightZ * speed; }

        if (GetAsyncKeyState('Q') & 0x8000) { g_camY += speed; }
        if (GetAsyncKeyState('E') & 0x8000) { g_camY -= speed; }
    }

    XMMATRIX mView = XMMatrixLookAtLH(XMVectorSet(g_camX, g_camY, g_camZ, 0),
        XMVectorSet(g_camX + lookX, g_camY + lookY, g_camZ + lookZ, 0),
        XMVectorSet(0, 1, 0, 0));
    XMMATRIX mProj = XMMatrixPerspectiveFovLH(XMConvertToRadians(g_FovDegree), width / height, 0.1f, 1000.0f);

    UINT stride = sizeof(Vertex);
    UINT offset = 0;

    pContext->OMSetDepthStencilState(NULL, 1);
    pContext->IASetVertexBuffers(0, 1, &g_pVB, &stride, &offset);
    pContext->IASetIndexBuffer(g_pIB, DXGI_FORMAT_R16_UINT, 0);
    pContext->IASetInputLayout(g_pLayout);
    pContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    pContext->VSSetShader(g_pVS, NULL, 0);
    pContext->PSSetShader(g_pPS, NULL, 0);
    pContext->RSSetState(g_pRS);

    pContext->VSSetConstantBuffers(0, 1, &g_pCB);

    pContext->IASetVertexBuffers(0, 1, &g_pVB, &stride, &offset);
    pContext->IASetIndexBuffer(g_pIB, DXGI_FORMAT_R16_UINT, 0);


    for (const auto& cube : g_Cubes) {
        if (cube.pTexture) {
            pContext->PSSetShaderResources(0, 1, &cube.pTexture);
        }
        else {
            pContext->PSSetShaderResources(0, 1, &cube.pTexture);
        }

        XMMATRIX mScale = XMMatrixScaling(cube.Size, cube.Size, cube.Size);
        XMMATRIX mTrans = XMMatrixTranslation(cube.pos.x, cube.pos.y, cube.pos.z);
        XMMATRIX mRotateX = XMMatrixRotationX(cube.Rotate.x);
        XMMATRIX mRotateY = XMMatrixRotationY(cube.Rotate.y);
        XMMATRIX mRotateZ = XMMatrixRotationZ(cube.Rotate.z);
        // 1. scale 2. rotation 3. transform
        XMMATRIX mWorld = mScale * mRotateX * mRotateY * mRotateZ * mTrans;

        ConstantBuffer cb;
        cb.mWorld = XMMatrixTranspose(mWorld);
        cb.mView = XMMatrixTranspose(mView);
        cb.mProjection = XMMatrixTranspose(mProj);

        pContext->UpdateSubresource(g_pCB, 0, NULL, &cb, 0, 0);
        pContext->DrawIndexed(36, 0, 0);
    }


    for (const auto& mesh : g_Meshes) {
        if (!mesh.pModel) continue;

        pContext->IASetVertexBuffers(0, 1, &mesh.pModel->pVB, &stride, &offset);
        pContext->IASetIndexBuffer(mesh.pModel->pIB, DXGI_FORMAT_R16_UINT, 0);

        if (mesh.pTex) {
            pContext->PSSetShaderResources(0, 1, &mesh.pTex);
        }
        else {
            pContext->PSSetShaderResources(0, 1, &g_pTexture);
        }

        XMMATRIX mScale = XMMatrixScaling(mesh.Size, mesh.Size, mesh.Size);
        XMMATRIX mTrans = XMMatrixTranslation(mesh.pos.x, mesh.pos.y, mesh.pos.z);
        XMMATRIX mRotateX = XMMatrixRotationX(mesh.Rotate.x);
        XMMATRIX mRotateY = XMMatrixRotationY(mesh.Rotate.y);
        XMMATRIX mRotateZ = XMMatrixRotationZ(mesh.Rotate.z);
        XMMATRIX mWorld = mScale * mRotateX * mRotateY * mRotateZ * mTrans;

        ConstantBuffer cb;
        cb.mWorld = XMMatrixTranspose(mWorld);
        cb.mView = XMMatrixTranspose(mView);
        cb.mProjection = XMMatrixTranspose(mProj);

        pContext->UpdateSubresource(g_pCB, 0, NULL, &cb, 0, 0);
        pContext->DrawIndexed(mesh.pModel->indexCount, 0, 0);
    }
}

extern "C" __declspec(dllexport) void ImguiUIEngine(HWND hwnd) {
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    ImGui::Begin("Engine");

    // =================================================
    //                     ImGui

    ImGui::Begin("Lua Log");
    if (ImGui::Button("Clear")) {
        ScriptEngine::LogMessage.clear();
    }
    ImGui::Separator();
    ImGui::BeginChild("ScrollingRegion", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

    for (size_t i = 0; i < ScriptEngine::LogMessage.size(); i++) {
        ImGui::TextUnformatted(ScriptEngine::LogMessage[i].c_str());
    }

    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
        ImGui::SetScrollHereY(1.0f);
    }

    ImGui::EndChild();
    ImGui::End();


    if (ImGui::Button("How to Operate")) {
        ShellExecute(NULL, L"open", L"https://www.mollypotter.com/blog/youre-stupid/", NULL, NULL, NULL);
    }

    // Begin tab
    if (ImGui::BeginTabBar("Main")) {


        // Cube settings
        if (ImGui::BeginTabItem("Cube")) {

            // Spawn Cube
            if (ImGui::Button("Spawn Cube", ImVec2(100, 30))) {
                g_Cubes.push_back({ XMFLOAT3(g_camX, g_camY, g_camZ + 3.0f), CubeSize });
            }

            ImGui::Text("Total Cubes: %zu", g_Cubes.size());

            ImGui::Separator();

            ImGui::Text("Cube setting");

            // Cube settings
            for (int i = 0; i < g_Cubes.size(); i++) {
                if (ImGui::TreeNode((void*)(intptr_t)i, "Cube %d", i)) {

                    ImGui::SliderFloat("Size", &g_Cubes[i].Size, 0.1f, 10.0f);

                    ImGui::DragFloat3("Position", &g_Cubes[i].pos.x, 0.1f);

                    ImGui::DragFloat3("Rotation", &g_Cubes[i].Rotate.x, 0.1f);

                    if (ImGui::Button("SelectTextureFile")) {
                        std::string path = OpenFile(hwnd);
                        if (!path.empty()) {
                            ID3D11ShaderResourceView* newTex = CreateTextureFromFile(path.c_str());

                            if (newTex) {
                                g_Cubes[i].pTexture = newTex;
                            }
                        }
                    }

                    // Delete cube
                    if (ImGui::Button("Delete")) {
                        g_Cubes.erase(g_Cubes.begin() + i);
                    }

                    ImGui::TreePop();
                }
            }
            ImGui::EndTabItem();
        }

        // Model settings
        if (ImGui::BeginTabItem("Model")) {
            ImGui::Text("Load .obj file");

            if (ImGui::Button("Spawn Model", ImVec2(100, 30))) {
                std::string path = OpenFileModel(hwnd);
                if (!path.empty()) {
                    ModelData* newModel = new ModelData();
                    *newModel = LoadOBJ(path.c_str());
                    if (newModel->pVB) {
                        g_Meshes.push_back({ {g_camX, g_camY, g_camZ + 5.0f}, 1.0f, {0,0,0}, newModel });
                    }
                }
            }
            ImGui::Separator();

            for (int i = 0; i < (int)g_Meshes.size(); i++) {
                if (ImGui::TreeNode((void*)(intptr_t)i, "Mesh %d", i)) {

                    ImGui::SliderFloat("Scale", &g_Meshes[i].Size, 0.1f, 10.0f);

                    ImGui::DragFloat3("Position", &g_Meshes[i].pos.x, 0.1f);

                    ImGui::DragFloat3("Rotation", &g_Meshes[i].Rotate.x, 0.1f);

                    if (ImGui::Button("Delete Mesh")) {
                        g_Meshes.erase(g_Meshes.begin() + i);
                        i--;
                    }
                    ImGui::TreePop();
                }
            }
            ImGui::EndTabItem();
        }

        // Camera movement
        if (ImGui::BeginTabItem("Camera")) {

            ImGui::SliderFloat("Change camera speed: %f", &speed, 0.1f, 1.0f);

            ImGui::SliderFloat("Fov", &g_FovDegree, 1.0f, 120.0f);

            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    // =================================================

    ImGui::End();
    ImGui::Render();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);
extern "C" __declspec(dllexport) LRESULT ImguiWndProc_DLL(HWND h, UINT m, WPARAM w, LPARAM l) {
    return ImGui_ImplWin32_WndProcHandler(h, m, w, l);
}