#include "dcui_engine.h"
#include "../render/renderer.h"
#include "audio_driver.h"
#include "disc_service.h"
#include "sysinfo_service.h"
#include <dc/maple/controller.h>
#include <string.h>
#include <math.h>

static dcui_vm_state_t s_vm;
bios_screen_t g_next_screen_request = SCREEN_MAIN_MENU;
extern const uint8_t DEFAULT_MENU_DCUI[];

static float s_anim_time = 0.0f;

int dcui_engine_init(const uint8_t *blob) {
    const uint8_t *src = blob ? blob : DEFAULT_MENU_DCUI;
    dcui_vm_init(&s_vm, src);
    g_next_screen_request = SCREEN_MAIN_MENU;
    s_anim_time = 0.0f;
    dcui_vm_dispatch_event(&s_vm, DCUI_EVENT_ON_INIT, -1);
    return 0;
}

/* Color Modulation Helper for 3D Shading */
static inline uint16_t shade_color(uint16_t c, float intensity) {
    if (intensity > 1.0f) intensity = 1.0f;
    if (intensity < 0.20f) intensity = 0.20f;
    uint32_t r = (c >> 11) & 0x1F;
    uint32_t g = (c >> 5) & 0x3F;
    uint32_t b = c & 0x1F;
    r = (uint32_t)(r * intensity);
    g = (uint32_t)(g * intensity);
    b = (uint32_t)(b * intensity);
    if (r > 0x1F) r = 0x1F;
    if (g > 0x3F) g = 0x3F;
    if (b > 0x1F) b = 0x1F;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

/* 3D Camera & Projection */
typedef struct {
    float x, y, z;
} vec3_f;

typedef struct {
    int x, y;
    float z;
} point2d_f;

typedef struct {
    point2d_f p0, p1, p2;
    uint16_t  color;
    float     depth;
} render_tri_t;

static vec3_f s_active_cam = { 0.0f, -4.6f, 3.2f };

static int project_vertex(float wx, float wy, float wz, point2d_f *out) {
    float rx = wx - s_active_cam.x;
    float ry = wy - s_active_cam.y;
    float rz = wz - s_active_cam.z;

    /* Camera orthonormal basis look-at transform (~55 degree pitch) */
    float cx = rx;
    float cy = ry * 0.571f + rz * 0.821f;
    float cz = ry * 0.821f - rz * 0.571f;

    if (cz < 0.2f) return 0; /* Near plane clip */

    float fov = 340.0f;
    out->x = (int)(320.0f + (cx * fov / cz));
    out->y = (int)(240.0f - (cy * fov / cz));
    out->z = cz;
    return 1;
}

/* Draw 3D Perspective Background Grid Floor */
static void draw_3d_grid(void) {
    uint16_t grid_col = 0x18C5; /* Dark cyber navy */
    for (float x = -6.0f; x <= 6.0f; x += 1.5f) {
        point2d_f p0, p1;
        if (project_vertex(x, -4.0f, -0.6f, &p0) && project_vertex(x, 6.0f, -0.6f, &p1)) {
            draw_line(p0.x, p0.y, p1.x, p1.y, grid_col);
        }
    }
    for (float y = -4.0f; y <= 6.0f; y += 1.5f) {
        point2d_f p0, p1;
        if (project_vertex(-6.0f, y, -0.6f, &p0) && project_vertex(6.0f, y, -0.6f, &p1)) {
            draw_line(p0.x, p0.y, p1.x, p1.y, grid_col);
        }
    }
}

void dcui_engine_render(void) {
    s_anim_time += 0.04f;

    if (!s_vm.header || !s_vm.objects || !s_vm.vertices || !s_vm.indices) return;

    /* 1. Camera */
    s_active_cam.x = s_vm.header->cam_x;
    s_active_cam.y = s_vm.header->cam_y;
    s_active_cam.z = s_vm.header->cam_z;

    /* 2. Header using authentic Sega 12x24 BFont */
    draw_bfont(MARGIN_X, 22, COLOR_WHITE, "DREAMCAST 3D INTERACTIVE BIOS");
    draw_rect(MARGIN_X, 50, SCREEN_W - (MARGIN_X * 2), 1, COLOR_DARK_GRAY);

    /* 3. Render 3D Background Grid Floor */
    draw_3d_grid();

    /* 4. Directional Light Source */
    float lx = 0.4f, ly = -0.5f, lz = 0.8f;
    float l_len = sqrtf(lx*lx + ly*ly + lz*lz);
    lx /= l_len; ly /= l_len; lz /= l_len;

    /* 5. Sort all visible objects by camera depth (cz) descending (Painter's Algorithm) */
    uint32_t draw_order[64];
    float draw_depth[64];
    uint32_t count = 0;

    for (uint32_t i = 0; i < s_vm.header->obj_count && count < 64; i++) {
        const dcui_mesh_object_t *obj = &s_vm.objects[i];
        if (!(obj->flags & DCUI_NODE_FLAG_VISIBLE)) continue;

        float ry = obj->cy - s_active_cam.y;
        float rz = obj->cz - s_active_cam.z;
        float cz = ry * 0.821f - rz * 0.571f;

        draw_order[count] = i;
        draw_depth[count] = cz;
        count++;
    }

    /* Insertion sort objects by depth descending (farthest first) */
    for (uint32_t i = 1; i < count; i++) {
        uint32_t cur_idx = draw_order[i];
        float cur_d = draw_depth[i];
        int j = (int)i - 1;
        while (j >= 0 && draw_depth[j] < cur_d) {
            draw_order[j + 1] = draw_order[j];
            draw_depth[j + 1] = draw_depth[j];
            j--;
        }
        draw_order[j + 1] = cur_idx;
        draw_depth[j + 1] = cur_d;
    }

    /* 6. Render 3D Triangulated Mesh Objects */
    render_tri_t tri_buffer[128];

    for (uint32_t k = 0; k < count; k++) {
        uint32_t i = draw_order[k];
        const dcui_mesh_object_t *obj = &s_vm.objects[i];
        int is_interactable = (obj->flags & DCUI_NODE_FLAG_INTERACTABLE);
        int is_focused = is_interactable && ((int32_t)i == s_vm.current_focus_node);

        uint16_t base_color = obj->color_565 ? obj->color_565 : 0xCE59;
        float hover_z = 0.0f;
        if (is_focused) {
            hover_z = 0.12f + 0.04f * sinf(s_anim_time * 3.0f);
            base_color = shade_color(base_color, 1.25f);
        }

        uint32_t tri_count = 0;

        /* Filter & Project Triangles */
        for (uint32_t t = 0; t + 2 < obj->idx_count && tri_count < 128; t += 3) {
            uint16_t i0 = s_vm.indices[obj->start_idx + t];
            uint16_t i1 = s_vm.indices[obj->start_idx + t + 1];
            uint16_t i2 = s_vm.indices[obj->start_idx + t + 2];

            const dcui_vertex_t *v0 = &s_vm.vertices[obj->start_v + i0];
            const dcui_vertex_t *v1 = &s_vm.vertices[obj->start_v + i1];
            const dcui_vertex_t *v2 = &s_vm.vertices[obj->start_v + i2];

            point2d_f p0, p1, p2;
            if (!project_vertex(v0->vx, v0->vy, v0->vz + hover_z, &p0)) continue;
            if (!project_vertex(v1->vx, v1->vy, v1->vz + hover_z, &p1)) continue;
            if (!project_vertex(v2->vx, v2->vy, v2->vz + hover_z, &p2)) continue;

            /* Strict 2D Backface Culling (cross < 0 is front-facing) */
            int cross = (p1.x - p0.x) * (p2.y - p0.y) - (p1.y - p0.y) * (p2.x - p0.x);
            if (cross >= 0) continue;

            /* Directional Shading */
            float nx = v0->nx;
            float ny = v0->ny;
            float nz = v0->nz;
            float dot = nx * lx + ny * ly + nz * lz;
            float intensity = 0.45f + 0.55f * fmaxf(0.0f, dot);

            tri_buffer[tri_count].p0 = p0;
            tri_buffer[tri_count].p1 = p1;
            tri_buffer[tri_count].p2 = p2;
            tri_buffer[tri_count].color = shade_color(base_color, intensity);
            tri_buffer[tri_count].depth = (p0.z + p1.z + p2.z) * 0.3333f;
            tri_count++;
        }

        /* Sort Triangles by depth descending */
        for (uint32_t ti = 1; ti < tri_count; ti++) {
            render_tri_t cur_t = tri_buffer[ti];
            int tj = (int)ti - 1;
            while (tj >= 0 && tri_buffer[tj].depth < cur_t.depth) {
                tri_buffer[tj + 1] = tri_buffer[tj];
                tj--;
            }
            tri_buffer[tj + 1] = cur_t;
        }

        /* Rasterize Sorted Triangles */
        for (uint32_t ti = 0; ti < tri_count; ti++) {
            const render_tri_t *t = &tri_buffer[ti];
            draw_triangle_filled(t->p0.x, t->p0.y, t->p1.x, t->p1.y, t->p2.x, t->p2.y, t->color);
            draw_line(t->p0.x, t->p0.y, t->p1.x, t->p1.y, shade_color(t->color, 1.25f));
            draw_line(t->p1.x, t->p1.y, t->p2.x, t->p2.y, shade_color(t->color, 1.25f));
            draw_line(t->p2.x, t->p2.y, t->p0.x, t->p0.y, shade_color(t->color, 1.25f));
        }

        /* Anchor Sega 12x24 BFont Text on Interactable Buttons */
        if (is_interactable) {
            point2d_f pt;
            if (project_vertex(obj->cx, obj->cy, obj->cz + hover_z + 0.15f, &pt)) {
                const char *label = obj->text_binding[0] ? obj->text_binding : obj->name;
                uint16_t txt_color = is_focused ? COLOR_GOLD : ((base_color == COLOR_WHITE) ? COLOR_BLACK : COLOR_WHITE);

                if (is_focused) {
                    draw_bfont_centered(pt.x, pt.y - 12, txt_color, label);
                    draw_bfont(pt.x - 90, pt.y - 12, COLOR_GOLD, ">");
                } else {
                    draw_bfont_centered(pt.x, pt.y - 12, txt_color, label);
                }
            }
        }
    }

    /* 7. Navigation Footer */
    draw_bfont_centered(SCREEN_W / 2, 442, COLOR_LIGHT_GRAY,
        "(A) Select   (START) Fast-Boot   (D-PAD) Move");
}

bios_screen_t dcui_engine_handle_input(uint32_t pressed) {
    g_next_screen_request = SCREEN_MAIN_MENU;

    if (!s_vm.header || !s_vm.objects) return SCREEN_MAIN_MENU;

    int32_t cur = s_vm.current_focus_node;
    const dcui_mesh_object_t *obj = (cur >= 0 && cur < (int32_t)s_vm.header->obj_count) ? &s_vm.objects[cur] : NULL;

    if (pressed & CONT_DPAD_UP) {
        if (obj && obj->nav_u >= 0) {
            s_vm.current_focus_node = obj->nav_u;
            audio_play_click();
        } else {
            for (int32_t i = cur - 1; i >= 0; i--) {
                if (s_vm.objects[i].flags & DCUI_NODE_FLAG_INTERACTABLE) {
                    s_vm.current_focus_node = i;
                    audio_play_click();
                    break;
                }
            }
        }
    }
    if (pressed & CONT_DPAD_DOWN) {
        if (obj && obj->nav_d >= 0) {
            s_vm.current_focus_node = obj->nav_d;
            audio_play_click();
        } else {
            for (uint32_t i = cur + 1; i < s_vm.header->obj_count; i++) {
                if (s_vm.objects[i].flags & DCUI_NODE_FLAG_INTERACTABLE) {
                    s_vm.current_focus_node = (int32_t)i;
                    audio_play_click();
                    break;
                }
            }
        }
    }
    if (pressed & CONT_DPAD_LEFT) {
        if (obj && obj->nav_l >= 0) {
            s_vm.current_focus_node = obj->nav_l;
            audio_play_click();
        }
    }
    if (pressed & CONT_DPAD_RIGHT) {
        if (obj && obj->nav_r >= 0) {
            s_vm.current_focus_node = obj->nav_r;
            audio_play_click();
        }
    }

    if (pressed & CONT_A) {
        dcui_vm_dispatch_event(&s_vm, DCUI_EVENT_ON_PRESS_A, cur);
    }
    if (pressed & CONT_B) {
        dcui_vm_dispatch_event(&s_vm, DCUI_EVENT_ON_PRESS_B, cur);
    }
    if (pressed & CONT_X) {
        dcui_vm_dispatch_event(&s_vm, DCUI_EVENT_ON_PRESS_X, cur);
    }
    if (pressed & CONT_Y) {
        dcui_vm_dispatch_event(&s_vm, DCUI_EVENT_ON_PRESS_Y, cur);
    }
    if (pressed & CONT_START) {
        dcui_vm_dispatch_event(&s_vm, DCUI_EVENT_ON_PRESS_START, cur);
    }

    return g_next_screen_request;
}
