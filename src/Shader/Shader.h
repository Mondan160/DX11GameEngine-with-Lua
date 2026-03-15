#pragma once
#include <Windows.h>

static const char* shaderCode =
"Texture2D txDiffuse : register(t0);"
"SamplerState samLinear : register(s0);"

"cbuffer cb : register(b0) {"
"  matrix mWorld; matrix mView; matrix mProj;"
"};"

"struct VS_IN { "
"  float3 pos : POSITION; "
"  float4 col : COLOR; "
"  float2 tex : TEXCOORD; "
"};"

"struct PS_IN { "
"  float4 pos : SV_POSITION; "
"  float2 tex : TEXCOORD; "
"  float2 worldPos : TEXCOORD1; " // グリッド計算用にワールド座標を渡す
"};"

// --- 頂点シェーダー ---
"PS_IN VS(VS_IN input) {"
"  PS_IN output;"
"  float4 wPos = mul(float4(input.pos, 1.0f), mWorld);"
"  output.pos = mul(wPos, mView);"
"  output.pos = mul(output.pos, mProj);"
"  output.tex = input.tex;"
"  output.worldPos = wPos.xz;" // 床の広がり(X,Z)を保存
"  return output;"
"}"

// --- 通常のテクスチャ用ピクセルシェーダー ---
"float4 PS(PS_IN input) : SV_Target {"
"  return txDiffuse.Sample(samLinear, input.tex);"
"}"

// --- グリッド専用ピクセルシェーダー (PS_Grid) ---
"float4 PS_Grid(PS_IN input) : SV_Target {"
"  float2 uv = input.worldPos;" // ワールド座標をベースに線を引く
"  float2 grid = abs(frac(uv - 0.5) - 0.5) / fwidth(uv);"
"  float lineIntensity = min(grid.x, grid.y);"
"  float alpha = 1.0 - min(lineIntensity, 1.0);"
// 線の色はグレー、背景は透明にする
"  if (alpha < 0.1) discard;" // 線じゃない場所は描画しない（背景を透かす）
"  return float4(0.4, 0.4, 0.4, alpha);"
"}";