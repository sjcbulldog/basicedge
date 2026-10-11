/******************************************************************************
 * File: basic_graphics.c
 * Description: Bounded BASIC-to-LVGL graphics bridge owned by the CM55 task.
 ******************************************************************************/

#include "basic_graphics.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "lvgl.h"
#include "my_basic.h"

#define BASIC_GRAPHICS_QUEUE_LENGTH (8U)
#define BASIC_GRAPHICS_MAX_OBJECTS  (64U)
#define BASIC_GRAPHICS_TEXT_LENGTH  (128U)
#define BASIC_GRAPHICS_TIMEOUT      (pdMS_TO_TICKS(500U))

typedef enum
{
    GFX_OP_WIDTH,
    GFX_OP_HEIGHT,
    GFX_OP_TICK,
    GFX_OP_SCREEN,
    GFX_OP_CREATE,
    GFX_OP_DELETE,
    GFX_OP_PARENT,
    GFX_OP_SET_POS,
    GFX_OP_SET_SIZE,
    GFX_OP_GET_X,
    GFX_OP_GET_Y,
    GFX_OP_GET_WIDTH,
    GFX_OP_GET_HEIGHT,
    GFX_OP_SHOW,
    GFX_OP_HIDE,
    GFX_OP_ENABLED,
    GFX_OP_TEXT_SET,
    GFX_OP_TEXT_GET,
    GFX_OP_VALUE_SET,
    GFX_OP_VALUE_GET,
    GFX_OP_RANGE,
    GFX_OP_CHECKED_SET,
    GFX_OP_CHECKED_GET,
    GFX_OP_COLOR_SET,
    GFX_OP_BG_COLOR,
    GFX_OP_OPACITY,
    GFX_OP_RADIUS,
    GFX_OP_ALIGN,
    GFX_OP_OPTIONS,
    GFX_OP_SELECTED_SET,
    GFX_OP_SELECTED_GET,
    GFX_OP_FONT,
    GFX_OP_LIST,
    GFX_OP_INVALIDATE,
    GFX_OP_REFRESH,
    GFX_OP_CLEAR
} basic_graphics_operation_t;

typedef struct
{
    basic_graphics_operation_t operation;
    int32_t value[4];
    char name[BASIC_GRAPHICS_TEXT_LENGTH];
} basic_graphics_request_t;

typedef struct
{
    int32_t status;
    int32_t value;
    int32_t auxiliary;
    char text[BASIC_GRAPHICS_TEXT_LENGTH];
} basic_graphics_response_t;

typedef struct
{
    lv_obj_t *object;
    bool used;
} basic_graphics_handle_t;

static QueueHandle_t request_queue;
static QueueHandle_t response_queue;
static basic_graphics_handle_t handles[BASIC_GRAPHICS_MAX_OBJECTS];
static bool graphics_ready;

static bool text_equal(const char *left, const char *right)
{
    while ((*left != '\0') && (*right != '\0'))
    {
        if (toupper((unsigned char)*left) != toupper((unsigned char)*right))
        {
            return false;
        }
        ++left;
        ++right;
    }
    return (*left == '\0') && (*right == '\0');
}

static lv_obj_t *object_from_handle(int32_t handle)
{
    if ((handle <= 0) || ((uint32_t)handle > BASIC_GRAPHICS_MAX_OBJECTS) ||
        !handles[handle - 1].used)
    {
        return NULL;
    }
    return handles[handle - 1].object;
}

static int32_t handle_from_object(lv_obj_t *object)
{
    for (uint32_t index = 0U; index < BASIC_GRAPHICS_MAX_OBJECTS; ++index)
    {
        if (handles[index].used && (handles[index].object == object))
        {
            return (int32_t)(index + 1U);
        }
    }
    return 0;
}

static int32_t allocate_handle(lv_obj_t *object)
{
    for (uint32_t index = 0U; index < BASIC_GRAPHICS_MAX_OBJECTS; ++index)
    {
        if (!handles[index].used)
        {
            handles[index].object = object;
            handles[index].used = true;
            return (int32_t)(index + 1U);
        }
    }
    return 0;
}

static void release_object_handles(lv_obj_t *object)
{
    for (uint32_t index = 0U; index < BASIC_GRAPHICS_MAX_OBJECTS; ++index)
    {
        lv_obj_t *parent = handles[index].object;
        bool is_descendant = false;
        while (NULL != parent)
        {
            parent = lv_obj_get_parent(parent);
            if (parent == object)
            {
                is_descendant = true;
                break;
            }
        }
        if (handles[index].used &&
            ((handles[index].object == object) ||
             is_descendant))
        {
            handles[index].used = false;
            handles[index].object = NULL;
        }
    }
}

static lv_obj_t *create_object(const char *name, lv_obj_t *parent)
{
    if (text_equal(name, "OBJ")) return lv_obj_create(parent);
    if (text_equal(name, "LABEL")) return lv_label_create(parent);
    if (text_equal(name, "BUTTON")) return lv_button_create(parent);
    if (text_equal(name, "BAR")) return lv_bar_create(parent);
    if (text_equal(name, "SLIDER")) return lv_slider_create(parent);
    if (text_equal(name, "CHECKBOX")) return lv_checkbox_create(parent);
    if (text_equal(name, "SWITCH")) return lv_switch_create(parent);
    if (text_equal(name, "DROPDOWN")) return lv_dropdown_create(parent);
    if (text_equal(name, "TEXTAREA")) return lv_textarea_create(parent);
    if (text_equal(name, "KEYBOARD")) return lv_keyboard_create(parent);
    if (text_equal(name, "IMAGE")) return lv_image_create(parent);
    if (text_equal(name, "ARC")) return lv_arc_create(parent);
    if (text_equal(name, "LED")) return lv_led_create(parent);
    return NULL;
}

static lv_align_t alignment_from_name(const char *name)
{
    if (text_equal(name, "TOP_LEFT")) return LV_ALIGN_TOP_LEFT;
    if (text_equal(name, "TOP_MID")) return LV_ALIGN_TOP_MID;
    if (text_equal(name, "TOP_RIGHT")) return LV_ALIGN_TOP_RIGHT;
    if (text_equal(name, "LEFT_MID")) return LV_ALIGN_LEFT_MID;
    if (text_equal(name, "CENTER")) return LV_ALIGN_CENTER;
    if (text_equal(name, "RIGHT_MID")) return LV_ALIGN_RIGHT_MID;
    if (text_equal(name, "BOTTOM_LEFT")) return LV_ALIGN_BOTTOM_LEFT;
    if (text_equal(name, "BOTTOM_MID")) return LV_ALIGN_BOTTOM_MID;
    if (text_equal(name, "BOTTOM_RIGHT")) return LV_ALIGN_BOTTOM_RIGHT;
    return LV_ALIGN_DEFAULT;
}

static const lv_font_t *font_from_name(const char *name)
{
    if (text_equal(name, "MONTSERRAT_12")) return &lv_font_montserrat_12;
    if (text_equal(name, "MONTSERRAT_14")) return &lv_font_montserrat_14;
    if (text_equal(name, "MONTSERRAT_16")) return &lv_font_montserrat_16;
    return NULL;
}

static const char *object_type_name(lv_obj_t *object)
{
    if (lv_obj_check_type(object, &lv_label_class)) return "LABEL";
    if (lv_obj_check_type(object, &lv_button_class)) return "BUTTON";
    if (lv_obj_check_type(object, &lv_slider_class)) return "SLIDER";
    if (lv_obj_check_type(object, &lv_bar_class)) return "BAR";
    if (lv_obj_check_type(object, &lv_checkbox_class)) return "CHECKBOX";
    if (lv_obj_check_type(object, &lv_switch_class)) return "SWITCH";
    if (lv_obj_check_type(object, &lv_dropdown_class)) return "DROPDOWN";
    if (lv_obj_check_type(object, &lv_textarea_class)) return "TEXTAREA";
    if (lv_obj_check_type(object, &lv_keyboard_class)) return "KEYBOARD";
    if (lv_obj_check_type(object, &lv_image_class)) return "IMAGE";
    if (lv_obj_check_type(object, &lv_arc_class)) return "ARC";
    if (lv_obj_check_type(object, &lv_led_class)) return "LED";
    return "OBJ";
}

static void execute_request(const basic_graphics_request_t *request,
                            basic_graphics_response_t *response)
{
    lv_obj_t *object;

    response->status = -1;
    response->value = 0;
    response->auxiliary = 0;
    response->text[0] = '\0';

    if (!graphics_ready && (request->operation != GFX_OP_WIDTH) &&
        (request->operation != GFX_OP_HEIGHT) &&
        (request->operation != GFX_OP_TICK))
    {
        return;
    }

    switch (request->operation)
    {
        case GFX_OP_WIDTH:
            response->value = graphics_ready ? lv_display_get_horizontal_resolution(NULL) : 0;
            response->status = 0;
            break;
        case GFX_OP_HEIGHT:
            response->value = graphics_ready ? lv_display_get_vertical_resolution(NULL) : 0;
            response->status = 0;
            break;
        case GFX_OP_TICK:
            response->value = (int32_t)lv_tick_get();
            response->status = 0;
            break;
        case GFX_OP_SCREEN:
            response->value = handle_from_object(lv_screen_active());
            if (response->value == 0) response->value = allocate_handle(lv_screen_active());
            response->status = (response->value != 0) ? 0 : -1;
            break;
        case GFX_OP_CREATE:
            object = create_object(request->name,
                                   (request->value[0] == 0) ? lv_screen_active() :
                                   object_from_handle(request->value[0]));
            response->value = (NULL != object) ? allocate_handle(object) : 0;
            if ((NULL != object) && (response->value == 0)) lv_obj_delete(object);
            response->status = (response->value != 0) ? 0 : -1;
            break;
        case GFX_OP_DELETE:
            object = object_from_handle(request->value[0]);
            if ((NULL == object) || (object == lv_screen_active())) break;
            release_object_handles(object);
            lv_obj_delete(object);
            response->status = 0;
            break;
        case GFX_OP_PARENT:
            object = object_from_handle(request->value[0]);
            if (NULL != object)
            {
                response->value = handle_from_object(lv_obj_get_parent(object));
                response->status = 0;
            }
            break;
        case GFX_OP_SET_POS:
            object = object_from_handle(request->value[0]);
            if (NULL != object) { lv_obj_set_pos(object, request->value[1], request->value[2]); response->status = 0; }
            break;
        case GFX_OP_SET_SIZE:
            object = object_from_handle(request->value[0]);
            if ((NULL != object) && (request->value[1] >= 0) && (request->value[2] >= 0)) { lv_obj_set_size(object, request->value[1], request->value[2]); response->status = 0; }
            break;
        case GFX_OP_GET_X: case GFX_OP_GET_Y: case GFX_OP_GET_WIDTH: case GFX_OP_GET_HEIGHT:
            object = object_from_handle(request->value[0]);
            if (NULL != object) { response->value = (request->operation == GFX_OP_GET_X) ? lv_obj_get_x(object) : (request->operation == GFX_OP_GET_Y) ? lv_obj_get_y(object) : (request->operation == GFX_OP_GET_WIDTH) ? lv_obj_get_width(object) : lv_obj_get_height(object); response->status = 0; }
            break;
        case GFX_OP_SHOW: case GFX_OP_HIDE:
            object = object_from_handle(request->value[0]);
            if (NULL != object) { if (request->operation == GFX_OP_SHOW) lv_obj_clear_flag(object, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN); response->status = 0; }
            break;
        case GFX_OP_ENABLED:
            object = object_from_handle(request->value[0]);
            if ((NULL != object) && ((request->value[1] == 0) || (request->value[1] == 1)))
            {
                if (request->value[1] != 0) lv_obj_remove_state(object, LV_STATE_DISABLED);
                else lv_obj_add_state(object, LV_STATE_DISABLED);
                response->status = 0;
            }
            break;
        case GFX_OP_TEXT_SET: case GFX_OP_TEXT_GET:
            object = object_from_handle(request->value[0]);
            if (NULL != object)
            {
                lv_obj_t *label = lv_obj_check_type(object, &lv_label_class) ? object : lv_obj_get_child(object, 0);
                if ((NULL != label) && lv_obj_check_type(label, &lv_label_class))
                {
                    if (request->operation == GFX_OP_TEXT_SET) { lv_label_set_text(label, request->name); response->status = 0; }
                    else { (void)snprintf(response->text, sizeof(response->text), "%s", lv_label_get_text(label)); response->status = 0; }
                }
            }
            break;
        case GFX_OP_VALUE_SET: case GFX_OP_VALUE_GET:
            object = object_from_handle(request->value[0]);
            if (NULL != object)
            {
                bool is_set = request->operation == GFX_OP_VALUE_SET;
                if (lv_obj_check_type(object, &lv_slider_class)) { if (is_set) lv_slider_set_value(object, request->value[1], LV_ANIM_OFF); else response->value = lv_slider_get_value(object); response->status = 0; }
                else if (lv_obj_check_type(object, &lv_bar_class)) { if (is_set) lv_bar_set_value(object, request->value[1], LV_ANIM_OFF); else response->value = lv_bar_get_value(object); response->status = 0; }
                else if (lv_obj_check_type(object, &lv_arc_class)) { if (is_set) lv_arc_set_value(object, request->value[1]); else response->value = lv_arc_get_value(object); response->status = 0; }
                else if (lv_obj_check_type(object, &lv_switch_class)) { if (is_set) { if (request->value[1] != 0) lv_obj_add_state(object, LV_STATE_CHECKED); else lv_obj_remove_state(object, LV_STATE_CHECKED); } else response->value = lv_obj_has_state(object, LV_STATE_CHECKED) ? 1 : 0; response->status = 0; }
            }
            break;
        case GFX_OP_RANGE:
            object = object_from_handle(request->value[0]);
            if ((NULL != object) && (request->value[1] <= request->value[2])) { lv_slider_set_range(object, request->value[1], request->value[2]); response->status = 0; }
            break;
        case GFX_OP_CHECKED_SET: case GFX_OP_CHECKED_GET:
            object = object_from_handle(request->value[0]);
            if (NULL != object)
            {
                if (request->operation == GFX_OP_CHECKED_SET)
                {
                    if ((request->value[1] != 0) && (request->value[1] != 1)) break;
                    if (request->value[1] != 0) lv_obj_add_state(object, LV_STATE_CHECKED); else lv_obj_remove_state(object, LV_STATE_CHECKED);
                }
                else response->value = lv_obj_has_state(object, LV_STATE_CHECKED) ? 1 : 0;
                response->status = 0;
            }
            break;
        case GFX_OP_OPTIONS:
            object = object_from_handle(request->value[0]);
            if ((NULL != object) && lv_obj_check_type(object, &lv_dropdown_class)) { lv_dropdown_set_options(object, request->name); response->status = 0; }
            break;
        case GFX_OP_SELECTED_SET: case GFX_OP_SELECTED_GET:
            object = object_from_handle(request->value[0]);
            if ((NULL != object) && lv_obj_check_type(object, &lv_dropdown_class))
            {
                if (request->operation == GFX_OP_SELECTED_SET) lv_dropdown_set_selected(object, (uint32_t)request->value[1]);
                else response->value = (int32_t)lv_dropdown_get_selected(object);
                response->status = 0;
            }
            break;
        case GFX_OP_COLOR_SET: case GFX_OP_BG_COLOR:
            object = object_from_handle(request->value[0]);
            if (NULL != object) { lv_obj_set_style_bg_color(object, lv_color_hex((uint32_t)request->value[1]), LV_PART_MAIN); response->status = 0; }
            break;
        case GFX_OP_OPACITY:
            object = object_from_handle(request->value[0]);
            if ((NULL != object) && (request->value[1] >= 0) && (request->value[1] <= 255)) { lv_obj_set_style_opa(object, request->value[1], LV_PART_MAIN); response->status = 0; }
            break;
        case GFX_OP_RADIUS:
            object = object_from_handle(request->value[0]);
            if ((NULL != object) && (request->value[1] >= 0)) { lv_obj_set_style_radius(object, request->value[1], LV_PART_MAIN); response->status = 0; }
            break;
        case GFX_OP_ALIGN:
            object = object_from_handle(request->value[0]);
            if ((NULL != object) && (alignment_from_name(request->name) != LV_ALIGN_DEFAULT)) { lv_obj_align(object, alignment_from_name(request->name), request->value[1], request->value[2]); response->status = 0; }
            break;
        case GFX_OP_FONT:
            object = object_from_handle(request->value[0]);
            if ((NULL != object) && (NULL != font_from_name(request->name))) { lv_obj_set_style_text_font(object, font_from_name(request->name), LV_PART_MAIN); response->status = 0; }
            break;
        case GFX_OP_LIST:
            if ((request->value[0] >= 0) && ((uint32_t)request->value[0] < BASIC_GRAPHICS_MAX_OBJECTS) && handles[request->value[0]].used)
            {
                object = handles[request->value[0]].object;
                response->value = request->value[0] + 1;
                response->auxiliary = handle_from_object(lv_obj_get_parent(object));
                (void)snprintf(response->text, sizeof(response->text), "%s", object_type_name(object));
                response->status = 0;
            }
            break;
        case GFX_OP_INVALIDATE:
            object = object_from_handle(request->value[0]);
            if (NULL != object) { lv_obj_invalidate(object); response->status = 0; }
            break;
        case GFX_OP_REFRESH:
            lv_obj_invalidate(lv_screen_active()); response->status = 0; break;
        case GFX_OP_CLEAR:
            for (uint32_t index = 0U; index < BASIC_GRAPHICS_MAX_OBJECTS; ++index)
            {
                if (handles[index].used && (handles[index].object != lv_screen_active()))
                {
                    lv_obj_delete(handles[index].object);
                    handles[index].used = false;
                    handles[index].object = NULL;
                }
            }
            response->status = 0;
            break;
        default:
            break;
    }
}

static int call_graphics(struct mb_interpreter_t *interpreter,
                         void **list,
                         basic_graphics_request_t *request,
                         basic_graphics_response_t *response)
{
    if ((request_queue == NULL) || (response_queue == NULL) ||
        (xQueueSend(request_queue, request, BASIC_GRAPHICS_TIMEOUT) != pdPASS) ||
        (xQueueReceive(response_queue, response, BASIC_GRAPHICS_TIMEOUT) != pdPASS))
    {
        (void)interpreter;
        (void)list;
        return MB_FUNC_ERR;
    }
    return (response->status == 0) ? MB_FUNC_OK : MB_FUNC_ERR;
}

static int no_arg_int(struct mb_interpreter_t *i, void **l, basic_graphics_operation_t operation)
{
    basic_graphics_request_t r = { .operation = operation }; basic_graphics_response_t s;
    mb_check(mb_attempt_open_bracket(i,l)); mb_check(mb_attempt_close_bracket(i,l)); mb_check(call_graphics(i,l,&r,&s)); mb_check(mb_push_int(i,l,s.value)); return MB_FUNC_OK;
}
static int gfx_width(struct mb_interpreter_t *i, void **l) { return no_arg_int(i,l,GFX_OP_WIDTH); }
static int gfx_height(struct mb_interpreter_t *i, void **l) { return no_arg_int(i,l,GFX_OP_HEIGHT); }
static int gfx_tick(struct mb_interpreter_t *i, void **l) { return no_arg_int(i,l,GFX_OP_TICK); }
static int gfx_screen(struct mb_interpreter_t *i, void **l) { return no_arg_int(i,l,GFX_OP_SCREEN); }

static int one_handle_int(struct mb_interpreter_t *i, void **l, basic_graphics_operation_t o)
{
    basic_graphics_request_t r={.operation=o}; basic_graphics_response_t s;
    mb_check(mb_attempt_open_bracket(i,l)); mb_check(mb_pop_int(i,l,(int_t*)&r.value[0])); mb_check(mb_attempt_close_bracket(i,l)); mb_check(call_graphics(i,l,&r,&s)); mb_check(mb_push_int(i,l,s.value)); return MB_FUNC_OK;
}
static int one_handle_status(struct mb_interpreter_t *i, void **l, basic_graphics_operation_t o)
{
    basic_graphics_request_t r={.operation=o}; basic_graphics_response_t s;
    mb_check(mb_attempt_open_bracket(i,l)); mb_check(mb_pop_int(i,l,(int_t*)&r.value[0])); mb_check(mb_attempt_close_bracket(i,l)); mb_check(call_graphics(i,l,&r,&s)); return MB_FUNC_OK;
}
#define GFX_GETTER(name, op) static int name(struct mb_interpreter_t *i, void **l) { return one_handle_int(i,l,op); }
#define GFX_STATUS(name, op) static int name(struct mb_interpreter_t *i, void **l) { return one_handle_status(i,l,op); }
GFX_GETTER(gfx_parent,GFX_OP_PARENT) GFX_GETTER(gfx_x,GFX_OP_GET_X) GFX_GETTER(gfx_y,GFX_OP_GET_Y) GFX_GETTER(gfx_width_of,GFX_OP_GET_WIDTH) GFX_GETTER(gfx_height_of,GFX_OP_GET_HEIGHT)
GFX_STATUS(gfx_delete,GFX_OP_DELETE) GFX_STATUS(gfx_show,GFX_OP_SHOW) GFX_STATUS(gfx_hide,GFX_OP_HIDE) GFX_STATUS(gfx_invalidate,GFX_OP_INVALIDATE)

static int gfx_create(struct mb_interpreter_t *i, void **l)
{
    basic_graphics_request_t r={.operation=GFX_OP_CREATE}; basic_graphics_response_t s; char *name;
    mb_check(mb_attempt_open_bracket(i,l)); mb_check(mb_pop_string(i,l,&name)); (void)snprintf(r.name,sizeof(r.name),"%s",name); if (mb_has_arg(i,l)) mb_check(mb_pop_int(i,l,(int_t*)&r.value[0])); mb_check(mb_attempt_close_bracket(i,l)); mb_check(call_graphics(i,l,&r,&s)); mb_check(mb_push_int(i,l,s.value)); return MB_FUNC_OK;
}
static int gfx_set_pos(struct mb_interpreter_t *i, void **l)
{ basic_graphics_request_t r={.operation=GFX_OP_SET_POS}; basic_graphics_response_t s; mb_check(mb_attempt_open_bracket(i,l)); mb_check(mb_pop_int(i,l,(int_t*)&r.value[0])); mb_check(mb_pop_int(i,l,(int_t*)&r.value[1])); mb_check(mb_pop_int(i,l,(int_t*)&r.value[2])); mb_check(mb_attempt_close_bracket(i,l)); mb_check(call_graphics(i,l,&r,&s)); return MB_FUNC_OK; }
static int gfx_set_size(struct mb_interpreter_t *i, void **l)
{ basic_graphics_request_t r={.operation=GFX_OP_SET_SIZE}; basic_graphics_response_t s; mb_check(mb_attempt_open_bracket(i,l)); mb_check(mb_pop_int(i,l,(int_t*)&r.value[0])); mb_check(mb_pop_int(i,l,(int_t*)&r.value[1])); mb_check(mb_pop_int(i,l,(int_t*)&r.value[2])); mb_check(mb_attempt_close_bracket(i,l)); mb_check(call_graphics(i,l,&r,&s)); return MB_FUNC_OK; }
static int gfx_text(struct mb_interpreter_t *i, void **l)
{ basic_graphics_request_t r={.operation=GFX_OP_TEXT_SET}; basic_graphics_response_t s; char *text; mb_check(mb_attempt_open_bracket(i,l)); mb_check(mb_pop_int(i,l,(int_t*)&r.value[0])); if (mb_has_arg(i,l)) { mb_check(mb_pop_string(i,l,&text)); (void)snprintf(r.name,sizeof(r.name),"%s",text); } else r.operation=GFX_OP_TEXT_GET; mb_check(mb_attempt_close_bracket(i,l)); mb_check(call_graphics(i,l,&r,&s)); if (r.operation == GFX_OP_TEXT_GET) mb_check(mb_push_string(i,l,s.text)); return MB_FUNC_OK; }
static int gfx_color(struct mb_interpreter_t *i, void **l)
{ int_t red,green,blue; mb_check(mb_attempt_open_bracket(i,l)); mb_check(mb_pop_int(i,l,&red)); mb_check(mb_pop_int(i,l,&green)); mb_check(mb_pop_int(i,l,&blue)); mb_check(mb_attempt_close_bracket(i,l)); if ((red<0)||(red>255)||(green<0)||(green>255)||(blue<0)||(blue>255)) return MB_FUNC_ERR; mb_check(mb_push_int(i,l,(red<<16)|(green<<8)|blue)); return MB_FUNC_OK; }
static int gfx_rgb(struct mb_interpreter_t *i, void **l)
{ int_t value; mb_check(mb_attempt_open_bracket(i,l)); mb_check(mb_pop_int(i,l,&value)); mb_check(mb_attempt_close_bracket(i,l)); if ((value < 0) || (value > 0xFFFFFF)) return MB_FUNC_ERR; mb_check(mb_push_int(i,l,value)); return MB_FUNC_OK; }
static int gfx_style_int(struct mb_interpreter_t *i, void **l, basic_graphics_operation_t o)
{ basic_graphics_request_t r={.operation=o}; basic_graphics_response_t s; mb_check(mb_attempt_open_bracket(i,l)); mb_check(mb_pop_int(i,l,(int_t*)&r.value[0])); mb_check(mb_pop_int(i,l,(int_t*)&r.value[1])); mb_check(mb_attempt_close_bracket(i,l)); mb_check(call_graphics(i,l,&r,&s)); return MB_FUNC_OK; }
static int gfx_value(struct mb_interpreter_t *i, void **l)
{ basic_graphics_request_t r={.operation=GFX_OP_VALUE_SET}; basic_graphics_response_t s; mb_check(mb_attempt_open_bracket(i,l)); mb_check(mb_pop_int(i,l,(int_t*)&r.value[0])); if (mb_has_arg(i,l)) mb_check(mb_pop_int(i,l,(int_t*)&r.value[1])); else r.operation=GFX_OP_VALUE_GET; mb_check(mb_attempt_close_bracket(i,l)); mb_check(call_graphics(i,l,&r,&s)); if (r.operation == GFX_OP_VALUE_GET) mb_check(mb_push_int(i,l,s.value)); return MB_FUNC_OK; }
static int gfx_range(struct mb_interpreter_t *i, void **l) { basic_graphics_request_t r={.operation=GFX_OP_RANGE}; basic_graphics_response_t s; mb_check(mb_attempt_open_bracket(i,l)); mb_check(mb_pop_int(i,l,(int_t*)&r.value[0])); mb_check(mb_pop_int(i,l,(int_t*)&r.value[1])); mb_check(mb_pop_int(i,l,(int_t*)&r.value[2])); mb_check(mb_attempt_close_bracket(i,l)); mb_check(call_graphics(i,l,&r,&s)); return MB_FUNC_OK; }
static int gfx_enabled(struct mb_interpreter_t *i, void **l) { return gfx_style_int(i,l,GFX_OP_ENABLED); }
static int gfx_checked(struct mb_interpreter_t *i, void **l)
{ basic_graphics_request_t r={.operation=GFX_OP_CHECKED_SET}; basic_graphics_response_t s; mb_check(mb_attempt_open_bracket(i,l)); mb_check(mb_pop_int(i,l,(int_t*)&r.value[0])); if (mb_has_arg(i,l)) mb_check(mb_pop_int(i,l,(int_t*)&r.value[1])); else r.operation=GFX_OP_CHECKED_GET; mb_check(mb_attempt_close_bracket(i,l)); mb_check(call_graphics(i,l,&r,&s)); if (r.operation == GFX_OP_CHECKED_GET) mb_check(mb_push_int(i,l,s.value)); return MB_FUNC_OK; }
static int gfx_selected(struct mb_interpreter_t *i, void **l)
{ basic_graphics_request_t r={.operation=GFX_OP_SELECTED_SET}; basic_graphics_response_t s; mb_check(mb_attempt_open_bracket(i,l)); mb_check(mb_pop_int(i,l,(int_t*)&r.value[0])); if (mb_has_arg(i,l)) mb_check(mb_pop_int(i,l,(int_t*)&r.value[1])); else r.operation=GFX_OP_SELECTED_GET; mb_check(mb_attempt_close_bracket(i,l)); mb_check(call_graphics(i,l,&r,&s)); if (r.operation == GFX_OP_SELECTED_GET) mb_check(mb_push_int(i,l,s.value)); return MB_FUNC_OK; }
static int gfx_options(struct mb_interpreter_t *i, void **l)
{ basic_graphics_request_t r={.operation=GFX_OP_OPTIONS}; basic_graphics_response_t s; char *text; mb_check(mb_attempt_open_bracket(i,l)); mb_check(mb_pop_int(i,l,(int_t*)&r.value[0])); mb_check(mb_pop_string(i,l,&text)); (void)snprintf(r.name,sizeof(r.name),"%s",text); mb_check(mb_attempt_close_bracket(i,l)); mb_check(call_graphics(i,l,&r,&s)); return MB_FUNC_OK; }
static int gfx_align(struct mb_interpreter_t *i, void **l)
{ basic_graphics_request_t r={.operation=GFX_OP_ALIGN}; basic_graphics_response_t s; char *mode; mb_check(mb_attempt_open_bracket(i,l)); mb_check(mb_pop_int(i,l,(int_t*)&r.value[0])); mb_check(mb_pop_string(i,l,&mode)); (void)snprintf(r.name,sizeof(r.name),"%s",mode); if (mb_has_arg(i,l)) mb_check(mb_pop_int(i,l,(int_t*)&r.value[1])); if (mb_has_arg(i,l)) mb_check(mb_pop_int(i,l,(int_t*)&r.value[2])); mb_check(mb_attempt_close_bracket(i,l)); mb_check(call_graphics(i,l,&r,&s)); return MB_FUNC_OK; }
static int gfx_font(struct mb_interpreter_t *i, void **l)
{ basic_graphics_request_t r={.operation=GFX_OP_FONT}; basic_graphics_response_t s; char *name; mb_check(mb_attempt_open_bracket(i,l)); mb_check(mb_pop_int(i,l,(int_t*)&r.value[0])); mb_check(mb_pop_string(i,l,&name)); (void)snprintf(r.name,sizeof(r.name),"%s",name); mb_check(mb_attempt_close_bracket(i,l)); mb_check(call_graphics(i,l,&r,&s)); return MB_FUNC_OK; }
static int gfx_color_set(struct mb_interpreter_t *i, void **l) { return gfx_style_int(i,l,GFX_OP_COLOR_SET); }
static int gfx_bg_color(struct mb_interpreter_t *i, void **l) { return gfx_style_int(i,l,GFX_OP_BG_COLOR); }
static int gfx_opacity(struct mb_interpreter_t *i, void **l) { return gfx_style_int(i,l,GFX_OP_OPACITY); }
static int gfx_radius(struct mb_interpreter_t *i, void **l) { return gfx_style_int(i,l,GFX_OP_RADIUS); }
static int no_arg_status(struct mb_interpreter_t *i, void **l, basic_graphics_operation_t o)
{ basic_graphics_request_t r={.operation=o}; basic_graphics_response_t s; mb_check(mb_attempt_open_bracket(i,l)); mb_check(mb_attempt_close_bracket(i,l)); mb_check(call_graphics(i,l,&r,&s)); return MB_FUNC_OK; }
static int gfx_refresh(struct mb_interpreter_t *i, void **l) { return no_arg_status(i,l,GFX_OP_REFRESH); }

void basic_graphics_initialize(void)
{
    request_queue = xQueueCreate(BASIC_GRAPHICS_QUEUE_LENGTH, sizeof(basic_graphics_request_t));
    response_queue = xQueueCreate(1U, sizeof(basic_graphics_response_t));
    graphics_ready = false;
    (void)memset(handles, 0, sizeof(handles));
}

void basic_graphics_register(struct mb_interpreter_t *interpreter)
{
    (void)mb_register_func(interpreter,"GFX_WIDTH",gfx_width); (void)mb_register_func(interpreter,"GFX_HEIGHT",gfx_height); (void)mb_register_func(interpreter,"GFX_TICK",gfx_tick); (void)mb_register_func(interpreter,"GFX_SCREEN",gfx_screen); (void)mb_register_func(interpreter,"GFX_CREATE",gfx_create); (void)mb_register_func(interpreter,"GFX_DELETE",gfx_delete); (void)mb_register_func(interpreter,"GFX_PARENT",gfx_parent); (void)mb_register_func(interpreter,"GFX_SET_POS",gfx_set_pos); (void)mb_register_func(interpreter,"GFX_SET_SIZE",gfx_set_size); (void)mb_register_func(interpreter,"GFX_X",gfx_x); (void)mb_register_func(interpreter,"GFX_Y",gfx_y); (void)mb_register_func(interpreter,"GFX_WIDTH_OF",gfx_width_of); (void)mb_register_func(interpreter,"GFX_HEIGHT_OF",gfx_height_of); (void)mb_register_func(interpreter,"GFX_ALIGN",gfx_align); (void)mb_register_func(interpreter,"GFX_SHOW",gfx_show); (void)mb_register_func(interpreter,"GFX_HIDE",gfx_hide); (void)mb_register_func(interpreter,"GFX_ENABLED",gfx_enabled); (void)mb_register_func(interpreter,"GFX_INVALIDATE",gfx_invalidate); (void)mb_register_func(interpreter,"GFX_TEXT",gfx_text); (void)mb_register_func(interpreter,"GFX_COLOR",gfx_color); (void)mb_register_func(interpreter,"GFX_RGB",gfx_rgb); (void)mb_register_func(interpreter,"GFX_COLOR_SET",gfx_color_set); (void)mb_register_func(interpreter,"GFX_BG_COLOR",gfx_bg_color); (void)mb_register_func(interpreter,"GFX_OPACITY",gfx_opacity); (void)mb_register_func(interpreter,"GFX_RADIUS",gfx_radius); (void)mb_register_func(interpreter,"GFX_VALUE",gfx_value); (void)mb_register_func(interpreter,"GFX_RANGE",gfx_range); (void)mb_register_func(interpreter,"GFX_SELECTED",gfx_selected); (void)mb_register_func(interpreter,"GFX_OPTIONS",gfx_options); (void)mb_register_func(interpreter,"GFX_CHECKED",gfx_checked); (void)mb_register_func(interpreter,"GFX_FONT",gfx_font); (void)mb_register_func(interpreter,"GFX_REFRESH",gfx_refresh);
}

void basic_graphics_mark_ready(void) { graphics_ready = true; }

void basic_graphics_process(void)
{
    basic_graphics_request_t request; basic_graphics_response_t response;
    if ((request_queue == NULL) || !graphics_ready) return;
    while (xQueueReceive(request_queue, &request, 0U) == pdPASS) { execute_request(&request, &response); (void)xQueueSend(response_queue, &response, 0U); }
}

bool basic_graphics_command(const char *arguments)
{
    basic_graphics_request_t request;
    basic_graphics_response_t response;

    if (text_equal(arguments,"INIT")) { printf("Graphics: %s\r\n",graphics_ready ? "ready" : "not ready"); return true; }
    if (text_equal(arguments,"INFO"))
    {
        int32_t width = 0;
        int32_t height = 0;
        if (graphics_ready)
        {
            request.operation = GFX_OP_WIDTH;
            if (call_graphics(NULL, NULL, &request, &response) == MB_FUNC_OK) width = response.value;
            request.operation = GFX_OP_HEIGHT;
            if (call_graphics(NULL, NULL, &request, &response) == MB_FUNC_OK) height = response.value;
        }
        printf("Graphics: %s, %ld x %ld, max handles %u\r\n",graphics_ready ? "ready" : "not ready",(long)width,(long)height,(unsigned int)BASIC_GRAPHICS_MAX_OBJECTS);
        return true;
    }
    if (text_equal(arguments,"CLEAR") || text_equal(arguments,"RESET")) { basic_graphics_request_t r={.operation=GFX_OP_CLEAR}; basic_graphics_response_t s; if (call_graphics(NULL,NULL,&r,&s)==MB_FUNC_OK) printf("Graphics cleared.\r\n"); else printf("Graphics unavailable.\r\n"); return true; }
    if (text_equal(arguments,"LIST"))
    {
        if (!graphics_ready) { printf("Graphics unavailable.\r\n"); return true; }
        printf("Graphics handles:\r\n");
        for (uint32_t index = 0U; index < BASIC_GRAPHICS_MAX_OBJECTS; ++index)
        {
            request.operation = GFX_OP_LIST;
            request.value[0] = (int32_t)index;
            if (call_graphics(NULL, NULL, &request, &response) == MB_FUNC_OK)
            {
                printf("  %ld: %s parent=%ld\r\n", (long)response.value, response.text, (long)response.auxiliary);
            }
        }
        return true;
    }
    if (text_equal(arguments,"UPDATE")) { printf("Graphics update is handled by the graphics task.\r\n"); return true; }
    if (text_equal(arguments,"HELP")) { printf("GFX INIT | CLEAR | RESET | UPDATE | INFO | LIST | HELP\r\n"); return true; }
    return false;
}