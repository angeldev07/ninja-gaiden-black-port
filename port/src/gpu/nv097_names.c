/*
 * nv097_names.c: names of the NV2A 3D class methods (Kelvin, 0x97).
 *
 * Names and offsets from xemu hw/xbox/nv2a/nv2a_regs.h (which takes them from
 * envytools/nouveau); they are the hardware interface. Repeated groups (textures every
 * 0x40, lights every 0x80, matrices and transform programs) are named by their first entry
 * plus an index.
 */
#include <stdio.h>
#include <string.h>

#include "nv2a.h"

typedef struct method_name {
    uint16_t offset;
    const char *name;
} method_name_t;

static const method_name_t k_names[] = {
    {0x0000, "SET_OBJECT"},
    {0x0100, "NO_OPERATION"},
    {0x0110, "WAIT_FOR_IDLE"},
    {0x0120, "SET_FLIP_READ"},
    {0x0124, "SET_FLIP_WRITE"},
    {0x0128, "SET_FLIP_MODULO"},
    {0x012C, "FLIP_INCREMENT_WRITE"},
    {0x0130, "FLIP_STALL"},
    {0x0180, "SET_CONTEXT_DMA_NOTIFIES"},
    {0x0184, "SET_CONTEXT_DMA_A"},
    {0x0188, "SET_CONTEXT_DMA_B"},
    {0x0190, "SET_CONTEXT_DMA_STATE"},
    {0x0194, "SET_CONTEXT_DMA_COLOR"},
    {0x0198, "SET_CONTEXT_DMA_ZETA"},
    {0x019C, "SET_CONTEXT_DMA_VERTEX_A"},
    {0x01A0, "SET_CONTEXT_DMA_VERTEX_B"},
    {0x01A4, "SET_CONTEXT_DMA_SEMAPHORE"},
    {0x01A8, "SET_CONTEXT_DMA_REPORT"},
    {0x0200, "SET_SURFACE_CLIP_HORIZONTAL"},
    {0x0204, "SET_SURFACE_CLIP_VERTICAL"},
    {0x0208, "SET_SURFACE_FORMAT"},
    {0x020C, "SET_SURFACE_PITCH"},
    {0x0210, "SET_SURFACE_COLOR_OFFSET"},
    {0x0214, "SET_SURFACE_ZETA_OFFSET"},
    {0x0260, "SET_COMBINER_ALPHA_ICW"},
    {0x0288, "SET_COMBINER_SPECULAR_FOG_CW0"},
    {0x028C, "SET_COMBINER_SPECULAR_FOG_CW1"},
    {0x0290, "SET_CONTROL0"},
    {0x0294, "SET_LIGHT_CONTROL"},
    {0x0298, "SET_COLOR_MATERIAL"},
    {0x029C, "SET_FOG_MODE"},
    {0x02A0, "SET_FOG_GEN_MODE"},
    {0x02A4, "SET_FOG_ENABLE"},
    {0x02A8, "SET_FOG_COLOR"},
    {0x02B4, "SET_WINDOW_CLIP_TYPE"},
    {0x02C0, "SET_WINDOW_CLIP_HORIZONTAL"},
    {0x02E0, "SET_WINDOW_CLIP_VERTICAL"},
    {0x0300, "SET_ALPHA_TEST_ENABLE"},
    {0x0304, "SET_BLEND_ENABLE"},
    {0x0308, "SET_CULL_FACE_ENABLE"},
    {0x030C, "SET_DEPTH_TEST_ENABLE"},
    {0x0310, "SET_DITHER_ENABLE"},
    {0x0314, "SET_LIGHTING_ENABLE"},
    {0x0318, "SET_POINT_PARAMS_ENABLE"},
    {0x031C, "SET_POINT_SMOOTH_ENABLE"},
    {0x0320, "SET_LINE_SMOOTH_ENABLE"},
    {0x0324, "SET_POLY_SMOOTH_ENABLE"},
    {0x0328, "SET_SKIN_MODE"},
    {0x032C, "SET_STENCIL_TEST_ENABLE"},
    {0x0330, "SET_POLY_OFFSET_POINT_ENABLE"},
    {0x0334, "SET_POLY_OFFSET_LINE_ENABLE"},
    {0x0338, "SET_POLY_OFFSET_FILL_ENABLE"},
    {0x033C, "SET_ALPHA_FUNC"},
    {0x0340, "SET_ALPHA_REF"},
    {0x0344, "SET_BLEND_FUNC_SFACTOR"},
    {0x0348, "SET_BLEND_FUNC_DFACTOR"},
    {0x034C, "SET_BLEND_COLOR"},
    {0x0350, "SET_BLEND_EQUATION"},
    {0x0354, "SET_DEPTH_FUNC"},
    {0x0358, "SET_COLOR_MASK"},
    {0x035C, "SET_DEPTH_MASK"},
    {0x0360, "SET_STENCIL_MASK"},
    {0x0364, "SET_STENCIL_FUNC"},
    {0x0368, "SET_STENCIL_FUNC_REF"},
    {0x036C, "SET_STENCIL_FUNC_MASK"},
    {0x0370, "SET_STENCIL_OP_FAIL"},
    {0x0374, "SET_STENCIL_OP_ZFAIL"},
    {0x0378, "SET_STENCIL_OP_ZPASS"},
    {0x037C, "SET_SHADE_MODE"},
    {0x0384, "SET_POLYGON_OFFSET_SCALE_FACTOR"},
    {0x0388, "SET_POLYGON_OFFSET_BIAS"},
    {0x038C, "SET_FRONT_POLYGON_MODE"},
    {0x0390, "SET_BACK_POLYGON_MODE"},
    {0x0394, "SET_CLIP_MIN"},
    {0x0398, "SET_CLIP_MAX"},
    {0x039C, "SET_CULL_FACE"},
    {0x03A0, "SET_FRONT_FACE"},
    {0x03A4, "SET_NORMALIZATION_ENABLE"},
    {0x03A8, "SET_MATERIAL_EMISSION"},
    {0x03B4, "SET_MATERIAL_ALPHA"},
    {0x03B8, "SET_SPECULAR_ENABLE"},
    {0x03BC, "SET_LIGHT_ENABLE_MASK"},
    {0x03C0, "SET_TEXGEN_S"},
    {0x03C4, "SET_TEXGEN_T"},
    {0x03C8, "SET_TEXGEN_R"},
    {0x03CC, "SET_TEXGEN_Q"},
    {0x0420, "SET_TEXTURE_MATRIX_ENABLE"},
    {0x043C, "SET_POINT_SIZE"},
    {0x0440, "SET_PROJECTION_MATRIX"},
    {0x0480, "SET_MODEL_VIEW_MATRIX"},
    {0x0580, "SET_INVERSE_MODEL_VIEW_MATRIX"},
    {0x0680, "SET_COMPOSITE_MATRIX"},
    {0x06C0, "SET_TEXTURE_MATRIX"},
    {0x0840, "SET_TEXGEN_PLANE_S"},
    {0x0850, "SET_TEXGEN_PLANE_T"},
    {0x0860, "SET_TEXGEN_PLANE_R"},
    {0x0870, "SET_TEXGEN_PLANE_Q"},
    {0x09C0, "SET_FOG_PARAMS"},
    {0x09CC, "SET_TEXGEN_VIEW_MODEL"},
    {0x09D0, "SET_FOG_PLANE"},
    {0x09E0, "SET_SPECULAR_PARAMS"},
    {0x09FC, "SET_PROVOKING_VERTEX"},
    {0x0A10, "SET_SCENE_AMBIENT_COLOR"},
    {0x0A20, "SET_VIEWPORT_OFFSET"},
    {0x0A30, "SET_POINT_PARAMS"},
    {0x0A50, "SET_EYE_POSITION"},
    {0x0A60, "SET_COMBINER_FACTOR0"},
    {0x0A80, "SET_COMBINER_FACTOR1"},
    {0x0AA0, "SET_COMBINER_ALPHA_OCW"},
    {0x0AC0, "SET_COMBINER_COLOR_ICW"},
    {0x0AE0, "SET_COLOR_KEY_COLOR"},
    {0x0AF0, "SET_VIEWPORT_SCALE"},
    {0x0B00, "SET_TRANSFORM_PROGRAM"},
    {0x0B80, "SET_TRANSFORM_CONSTANT"},
    {0x0C00, "SET_BACK_LIGHT_AMBIENT_COLOR"},
    {0x0C0C, "SET_BACK_LIGHT_DIFFUSE_COLOR"},
    {0x0C18, "SET_BACK_LIGHT_SPECULAR_COLOR"},
    {0x1000, "SET_LIGHT_AMBIENT_COLOR"},
    {0x100C, "SET_LIGHT_DIFFUSE_COLOR"},
    {0x1018, "SET_LIGHT_SPECULAR_COLOR"},
    {0x1024, "SET_LIGHT_LOCAL_RANGE"},
    {0x1028, "SET_LIGHT_INFINITE_HALF_VECTOR"},
    {0x1034, "SET_LIGHT_INFINITE_DIRECTION"},
    {0x1040, "SET_LIGHT_SPOT_FALLOFF"},
    {0x104C, "SET_LIGHT_SPOT_DIRECTION"},
    {0x105C, "SET_LIGHT_LOCAL_POSITION"},
    {0x1068, "SET_LIGHT_LOCAL_ATTENUATION"},
    {0x1500, "SET_VERTEX3F"},
    {0x1518, "SET_VERTEX4F"},
    {0x1530, "SET_NORMAL3F"},
    {0x1540, "SET_NORMAL3S"},
    {0x1550, "SET_DIFFUSE_COLOR4F"},
    {0x1560, "SET_DIFFUSE_COLOR3F"},
    {0x156C, "SET_DIFFUSE_COLOR4UB"},
    {0x1570, "SET_SPECULAR_COLOR4F"},
    {0x1580, "SET_SPECULAR_COLOR3F"},
    {0x158C, "SET_SPECULAR_COLOR4UB"},
    {0x1590, "SET_TEXCOORD0_2F"},
    {0x1598, "SET_TEXCOORD0_2S"},
    {0x15A0, "SET_TEXCOORD0_4F"},
    {0x15B0, "SET_TEXCOORD0_4S"},
    {0x15B8, "SET_TEXCOORD1_2F"},
    {0x15C0, "SET_TEXCOORD1_2S"},
    {0x15C8, "SET_TEXCOORD1_4F"},
    {0x15D8, "SET_TEXCOORD1_4S"},
    {0x15E0, "SET_TEXCOORD2_2F"},
    {0x15E8, "SET_TEXCOORD2_2S"},
    {0x15F0, "SET_TEXCOORD2_4F"},
    {0x1600, "SET_TEXCOORD2_4S"},
    {0x1608, "SET_TEXCOORD3_2F"},
    {0x1610, "SET_TEXCOORD3_2S"},
    {0x1620, "SET_TEXCOORD3_4F"},
    {0x1630, "SET_TEXCOORD3_4S"},
    {0x1698, "SET_FOG_COORD"},
    {0x169C, "SET_WEIGHT1F"},
    {0x16A0, "SET_WEIGHT2F"},
    {0x16B0, "SET_WEIGHT3F"},
    {0x16C0, "SET_WEIGHT4F"},
    {0x1720, "SET_VERTEX_DATA_ARRAY_OFFSET"},
    {0x1760, "SET_VERTEX_DATA_ARRAY_FORMAT"},
    {0x17BC, "SET_LOGIC_OP_ENABLE"},
    {0x17C0, "SET_LOGIC_OP"},
    {0x17C8, "CLEAR_REPORT_VALUE"},
    {0x17CC, "SET_ZPASS_PIXEL_COUNT_ENABLE"},
    {0x17D0, "GET_REPORT"},
    {0x17E0, "SET_EYE_DIRECTION"},
    {0x17F8, "SET_SHADER_CLIP_PLANE_MODE"},
    {0x17FC, "SET_BEGIN_END"},
    {0x1800, "ARRAY_ELEMENT16"},
    {0x1808, "ARRAY_ELEMENT32"},
    {0x1810, "DRAW_ARRAYS"},
    {0x1818, "INLINE_ARRAY"},
    {0x181C, "SET_EYE_VECTOR"},
    {0x1880, "SET_VERTEX_DATA2F_M"},
    {0x1900, "SET_VERTEX_DATA2S"},
    {0x1940, "SET_VERTEX_DATA4UB"},
    {0x1980, "SET_VERTEX_DATA4S_M"},
    {0x1A00, "SET_VERTEX_DATA4F_M"},
    {0x1B00, "SET_TEXTURE_OFFSET"},
    {0x1B04, "SET_TEXTURE_FORMAT"},
    {0x1B08, "SET_TEXTURE_ADDRESS"},
    {0x1B0C, "SET_TEXTURE_CONTROL0"},
    {0x1B10, "SET_TEXTURE_CONTROL1"},
    {0x1B14, "SET_TEXTURE_FILTER"},
    {0x1B1C, "SET_TEXTURE_IMAGE_RECT"},
    {0x1B20, "SET_TEXTURE_PALETTE"},
    {0x1B24, "SET_TEXTURE_BORDER_COLOR"},
    {0x1B28, "SET_TEXTURE_SET_BUMP_ENV_MAT"},
    {0x1B38, "SET_TEXTURE_SET_BUMP_ENV_SCALE"},
    {0x1B3C, "SET_TEXTURE_SET_BUMP_ENV_OFFSET"},
    {0x1D6C, "SET_SEMAPHORE_OFFSET"},
    {0x1D70, "BACK_END_WRITE_SEMAPHORE_RELEASE"},
    {0x1D78, "SET_ZMIN_MAX_CONTROL"},
    {0x1D7C, "SET_ANTI_ALIASING_CONTROL"},
    {0x1D8C, "SET_ZSTENCIL_CLEAR_VALUE"},
    {0x1D90, "SET_COLOR_CLEAR_VALUE"},
    {0x1D94, "CLEAR_SURFACE"},
    {0x1D98, "SET_CLEAR_RECT_HORIZONTAL"},
    {0x1D9C, "SET_CLEAR_RECT_VERTICAL"},
    {0x1E20, "SET_SPECULAR_FOG_FACTOR"},
    {0x1E28, "SET_SPECULAR_PARAMS_BACK"},
    {0x1E40, "SET_COMBINER_COLOR_OCW"},
    {0x1E60, "SET_COMBINER_CONTROL"},
    {0x1E68, "SET_SHADOW_ZSLOPE_THRESHOLD"},
    {0x1E6C, "SET_SHADOW_DEPTH_FUNC"},
    {0x1E70, "SET_SHADER_STAGE_PROGRAM"},
    {0x1E78, "SET_SHADER_OTHER_STAGE_INPUT"},
    {0x1E80, "SET_TRANSFORM_DATA"},
    {0x1E90, "LAUNCH_TRANSFORM_PROGRAM"},
    {0x1E94, "SET_TRANSFORM_EXECUTION_MODE"},
    {0x1E98, "SET_TRANSFORM_PROGRAM_CXT_WRITE_EN"},
    {0x1E9C, "SET_TRANSFORM_PROGRAM_LOAD"},
    {0x1EA0, "SET_TRANSFORM_PROGRAM_START"},
    {0x1EA4, "SET_TRANSFORM_CONSTANT_LOAD"},
};

#define NNAMES (sizeof k_names / sizeof k_names[0])

const char *nv097_method_name(uint32_t method, char *buf, size_t buflen) {
    uint32_t m = method & 0x1FFCu, base = m, index = 0;
    size_t lo = 0, hi = NNAMES;
    /* Strided groups: 4 texture stages (0x40) and 8 lights (0x80). */
    if (m >= 0x1B00 && m < 0x1C00) {
        index = (m - 0x1B00) / 0x40;
        base = 0x1B00 + (m - 0x1B00) % 0x40;
    } else if (m >= 0x1000 && m < 0x1400) {
        index = (m - 0x1000) / 0x80;
        base = 0x1000 + (m - 0x1000) % 0x80;
    }
    /* The last entry with offset <= base. */
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        if (k_names[mid].offset <= base)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (!lo) {
        snprintf(buf, buflen, "M_%04X", m);
        return buf;
    }
    lo--;
    if (base == k_names[lo].offset && !index)
        snprintf(buf, buflen, "%s", k_names[lo].name);
    else if (base == k_names[lo].offset)
        snprintf(buf, buflen, "%s[%u]", k_names[lo].name, index);
    else if (index || (m >= 0x1B00 && m < 0x1C00))
        snprintf(buf, buflen, "%s[%u]+%u", k_names[lo].name, index, (base - k_names[lo].offset) / 4);
    else
        snprintf(buf, buflen, "%s+%u", k_names[lo].name, (base - k_names[lo].offset) / 4);
    return buf;
}
