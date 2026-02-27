#pragma once

#include <string_view>

namespace engine::ShaderProfiles {
namespace Hlsl {
inline constexpr std::string_view VertexSm50 = "vs_5_0";
inline constexpr std::string_view PixelSm50 = "ps_5_0";
inline constexpr std::string_view GeometrySm50 = "gs_5_0";
inline constexpr std::string_view HullSm50 = "hs_5_0";
inline constexpr std::string_view DomainSm50 = "ds_5_0";
inline constexpr std::string_view ComputeSm50 = "cs_5_0";

inline constexpr std::string_view VertexSm51 = "vs_5_1";
inline constexpr std::string_view PixelSm51 = "ps_5_1";
inline constexpr std::string_view GeometrySm51 = "gs_5_1";
inline constexpr std::string_view HullSm51 = "hs_5_1";
inline constexpr std::string_view DomainSm51 = "ds_5_1";
inline constexpr std::string_view ComputeSm51 = "cs_5_1";

inline constexpr std::string_view VertexSm60 = "vs_6_0";
inline constexpr std::string_view PixelSm60 = "ps_6_0";
inline constexpr std::string_view GeometrySm60 = "gs_6_0";
inline constexpr std::string_view HullSm60 = "hs_6_0";
inline constexpr std::string_view DomainSm60 = "ds_6_0";
inline constexpr std::string_view ComputeSm60 = "cs_6_0";

inline constexpr std::string_view VertexSm61 = "vs_6_1";
inline constexpr std::string_view PixelSm61 = "ps_6_1";
inline constexpr std::string_view GeometrySm61 = "gs_6_1";
inline constexpr std::string_view HullSm61 = "hs_6_1";
inline constexpr std::string_view DomainSm61 = "ds_6_1";
inline constexpr std::string_view ComputeSm61 = "cs_6_1";

inline constexpr std::string_view VertexSm62 = "vs_6_2";
inline constexpr std::string_view PixelSm62 = "ps_6_2";
inline constexpr std::string_view GeometrySm62 = "gs_6_2";
inline constexpr std::string_view HullSm62 = "hs_6_2";
inline constexpr std::string_view DomainSm62 = "ds_6_2";
inline constexpr std::string_view ComputeSm62 = "cs_6_2";

inline constexpr std::string_view VertexSm63 = "vs_6_3";
inline constexpr std::string_view PixelSm63 = "ps_6_3";
inline constexpr std::string_view GeometrySm63 = "gs_6_3";
inline constexpr std::string_view HullSm63 = "hs_6_3";
inline constexpr std::string_view DomainSm63 = "ds_6_3";
inline constexpr std::string_view ComputeSm63 = "cs_6_3";

inline constexpr std::string_view VertexSm64 = "vs_6_4";
inline constexpr std::string_view PixelSm64 = "ps_6_4";
inline constexpr std::string_view GeometrySm64 = "gs_6_4";
inline constexpr std::string_view HullSm64 = "hs_6_4";
inline constexpr std::string_view DomainSm64 = "ds_6_4";
inline constexpr std::string_view ComputeSm64 = "cs_6_4";

inline constexpr std::string_view VertexSm65 = "vs_6_5";
inline constexpr std::string_view PixelSm65 = "ps_6_5";
inline constexpr std::string_view GeometrySm65 = "gs_6_5";
inline constexpr std::string_view HullSm65 = "hs_6_5";
inline constexpr std::string_view DomainSm65 = "ds_6_5";
inline constexpr std::string_view ComputeSm65 = "cs_6_5";
inline constexpr std::string_view MeshSm65 = "ms_6_5";
inline constexpr std::string_view AmplificationSm65 = "as_6_5";

inline constexpr std::string_view VertexSm66 = "vs_6_6";
inline constexpr std::string_view PixelSm66 = "ps_6_6";
inline constexpr std::string_view GeometrySm66 = "gs_6_6";
inline constexpr std::string_view HullSm66 = "hs_6_6";
inline constexpr std::string_view DomainSm66 = "ds_6_6";
inline constexpr std::string_view ComputeSm66 = "cs_6_6";
inline constexpr std::string_view MeshSm66 = "ms_6_6";
inline constexpr std::string_view AmplificationSm66 = "as_6_6";
} // namespace Hlsl

namespace Glsl {
inline constexpr std::string_view Core330 = "330 core";
inline constexpr std::string_view Core400 = "400 core";
inline constexpr std::string_view Core410 = "410 core";
inline constexpr std::string_view Core420 = "420 core";
inline constexpr std::string_view Core430 = "430 core";
inline constexpr std::string_view Core440 = "440 core";
inline constexpr std::string_view Core450 = "450 core";
inline constexpr std::string_view Core460 = "460 core";

inline constexpr std::string_view Es300 = "300 es";
inline constexpr std::string_view Es310 = "310 es";
inline constexpr std::string_view Es320 = "320 es";
} // namespace Glsl

namespace SpirV {
inline constexpr std::string_view V10 = "spirv_1_0";
inline constexpr std::string_view V11 = "spirv_1_1";
inline constexpr std::string_view V12 = "spirv_1_2";
inline constexpr std::string_view V13 = "spirv_1_3";
inline constexpr std::string_view V14 = "spirv_1_4";
inline constexpr std::string_view V15 = "spirv_1_5";
inline constexpr std::string_view V16 = "spirv_1_6";
} // namespace SpirV

namespace Msl {
inline constexpr std::string_view V20 = "msl_2_0";
inline constexpr std::string_view V21 = "msl_2_1";
inline constexpr std::string_view V22 = "msl_2_2";
inline constexpr std::string_view V23 = "msl_2_3";
inline constexpr std::string_view V24 = "msl_2_4";
inline constexpr std::string_view V30 = "msl_3_0";
} // namespace Msl

namespace Wgsl {
inline constexpr std::string_view Core = "wgsl";
} // namespace Wgsl
} // namespace engine::ShaderProfiles
