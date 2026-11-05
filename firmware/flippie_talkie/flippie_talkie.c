#include <furi.h>
#include <gui/view_port.h>
#include <input/input.h>
#include "pmr_ui.h"
#include "pmr_preferences.h"

int32_t pmr_capture_app(void* args);
int32_t pmr_connect_app(void* args);

typedef struct {
    ViewPort* view;
    FuriMessageQueue* keys;
    Storage* storage;
    PmrPreferences prefs;
    uint32_t screen, selected;
    bool save_error;
} Shell;

static void shell_draw(Canvas* c,void* context) {
    Shell* s=context;char value[24];
    if(s->screen==2) {
        pmr_ui_header(c,"Quick start","0.5");
        canvas_set_font(c,FontSecondary);
        canvas_draw_str(c,3,25,"Use the same radio channel.");
        canvas_draw_str(c,3,36,"Privacy code: 0 / off.");
        canvas_draw_str(c,3,47,"Browser: mic + speaker.");
        elements_button_center(c,"Back");return;
    }
    pmr_ui_header(c,s->screen?"Settings":"Flippie Talkie",s->screen?(s->save_error?"SD error":"PMR446"):"");
    if(!s->screen) {
        const char* labels[]={"Listen on Flipper","Browser link","Settings","Quick start"};
        const char* values[]={"",s->prefs.transport?"USB":"Bluetooth","", ""};
        for(uint32_t i=0;i<4;i++)pmr_ui_row(c,i,s->selected==i,labels[i],values[i]);
    } else {
        const char* labels[]={"Channel","Speaker","Auto-record","Connection"};
        for(uint32_t i=0;i<4;i++) {
            if(i==0)snprintf(value,sizeof(value),"< %02u >",s->prefs.channel);
            else if(i==1)snprintf(value,sizeof(value),"< %u/8 >",s->prefs.volume);
            else if(i==2)snprintf(value,sizeof(value),"< %s >",s->prefs.record?"On":"Off");
            else snprintf(value,sizeof(value),"< %s >",s->prefs.transport?"USB":"BT");
            pmr_ui_row(c,i,s->selected==i,labels[i],value);
        }
    }
}
static void shell_input(InputEvent* e,void* context) {
    Shell* s=context;furi_message_queue_put(s->keys,e,0);
}
int32_t flippie_talkie_app(void* args) {
    UNUSED(args);Shell* s=calloc(1,sizeof(Shell));
    s->storage=furi_record_open(RECORD_STORAGE);pmr_preferences_load(s->storage,&s->prefs);
    s->keys=furi_message_queue_alloc(8,sizeof(InputEvent));s->view=view_port_alloc();
    view_port_draw_callback_set(s->view,shell_draw,s);view_port_input_callback_set(s->view,shell_input,s);
    Gui* gui=furi_record_open(RECORD_GUI);gui_add_view_port(gui,s->view,GuiLayerFullscreen);
    bool running=true;InputEvent e;
    while(running) {
        if(furi_message_queue_get(s->keys,&e,FuriWaitForever)!=FuriStatusOk)continue;
        if(e.type!=InputTypeShort && e.type!=InputTypeRepeat)continue;
        bool short_press=e.type==InputTypeShort;
        if(e.key==InputKeyBack && short_press) {
            if(s->screen){s->screen=0;s->selected=0;}else running=false;
        } else if(s->screen==2) {
            if(e.key==InputKeyOk && short_press){s->screen=0;s->selected=3;}
        } else if(e.key==InputKeyUp)s->selected=(s->selected+3)%4;
        else if(e.key==InputKeyDown)s->selected=(s->selected+1)%4;
        else if(s->screen==1 && (e.key==InputKeyLeft || e.key==InputKeyRight || (e.key==InputKeyOk && short_press))) {
            int delta=e.key==InputKeyLeft?-1:1;
            if(s->selected==0)s->prefs.channel=(s->prefs.channel+15+delta)%16+1;
            else if(s->selected==1)s->prefs.volume=CLAMP((int)s->prefs.volume+delta,8,0);
            else if(s->selected==2)s->prefs.record=!s->prefs.record;
            else s->prefs.transport=!s->prefs.transport;
            s->save_error=!pmr_preferences_save(s->storage,&s->prefs);
        } else if(!s->screen && e.key==InputKeyOk && short_press) {
            if(s->selected<2) {
                // Each mode owns and releases the radio before returning to this menu.
                view_port_enabled_set(s->view,false);
                if(s->selected==0)pmr_capture_app(NULL);else pmr_connect_app(NULL);
                pmr_preferences_load(s->storage,&s->prefs);
                furi_message_queue_reset(s->keys);view_port_enabled_set(s->view,true);
            }else {s->screen=s->selected==2?1:2;s->selected=0;}
        }
        view_port_update(s->view);
    }
    view_port_enabled_set(s->view,false);gui_remove_view_port(gui,s->view);view_port_free(s->view);
    furi_message_queue_free(s->keys);furi_record_close(RECORD_STORAGE);furi_record_close(RECORD_GUI);free(s);return 0;
}
