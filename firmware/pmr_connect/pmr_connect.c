#include <furi.h>
#include <furi_hal.h>
#include <furi_hal_bt_serial.h>
#include <bt/bt_service/bt.h>
#include <gui/gui.h>
#include <gui/view_port.h>
#include <input/input.h>
#include <cli/cli.h>
#include <stdio.h>
#include <string.h>
#include <stm32wbxx_ll_spi.h>
#include <stm32wbxx_ll_tim.h>
#include "pmr_dsp.h"
#include "pmr_radio_core.h"
#include "pmr_codec.h"
#include "pmr_rx_gate.h"

#define RX_BLOCKS 8U
#define TX_RING 4096U
#define WIRE_MAX 180U
#define ACK_FLAG 1U
#define PTT_MAX_MS 60000U

typedef struct {uint16_t size;uint8_t data[WIRE_MAX];} Packet;
typedef struct {uint8_t type,epoch;uint16_t n;uint8_t data[VOICE_BYTES];} Command;
typedef struct {
    ViewPort* view;FuriMessageQueue* keys;FuriMessageQueue* commands;FuriMessageQueue* outgoing;
    FuriStreamBuffer* incoming;FuriEventFlag* flags;FuriThread* worker;Bt* bt;
    volatile bool running,connected,ble_ready,attached,usb_active,usb_abort,listening,ptt_pending,ptt_stop,ptt_draining;
    volatile uint32_t mode,state,channel,last_contact,last_audio,error;
    volatile uint32_t rx_dropped,tx_dropped,underruns,tx_started,tx_marcstate,tx_tests,drain_until;
    Packet latest_status;volatile bool status_pending;
    volatile uint32_t ble_packets,ble_ms,ble_max_ms,probe_until,probe_next;
    int32_t trigger_dbm,audio_release_dbm,audio_floor_dbm;
    PmrRxGate rx_gate;uint32_t receive_rate,carrier_checks;
    volatile uint32_t sample_drops,queue_drops,ble_timeouts,ble_failures;volatile int32_t rssi;
    bool spi_owned,rx_timer,was_bt_active;
    char original_name[32];
    PmrDsp dsp;uint32_t resample_phase;int16_t previous;
    int16_t rx[RX_BLOCKS][VOICE_SAMPLES];volatile uint32_t rx_head,rx_tail;uint32_t rx_count;
    volatile int32_t rx_sum;volatile uint32_t rx_measurements;volatile uint32_t read_errors;
    int32_t encoder_index;
    int16_t tx[TX_RING];volatile uint32_t tx_head,tx_tail;volatile uint8_t epoch;
    uint32_t pdm_phase,pdm_slot;int32_t pdm_error;int16_t pdm_sample;bool pdm_carry,carry_valid;
    uint16_t rx_sequence;uint8_t parser[WIRE_MAX];uint32_t parser_size;
} App;

// Radio states: 0 idle, 1 listening, 2 receiving, 3 filling microphone buffer, 4 transmitting.
static uint32_t cycles_now(void){return furi_hal_cortex_timer_get(0).start;}
static bool radio_register(uint8_t reg,uint8_t* value,bool read) {
    FuriHalSpiBusHandle* h=&furi_hal_spi_bus_handle_subghz;
    furi_hal_spi_acquire(h);uint8_t tx[2]={read?(reg|0xC0):reg,read?0:*value},rx[2]={0};
    furi_hal_gpio_write(h->cs,false);uint32_t start=cycles_now();
    while(furi_hal_gpio_read(h->miso))if((uint32_t)(cycles_now()-start)>64000){furi_hal_gpio_write(h->cs,true);furi_hal_spi_release(h);return false;}
    bool ok=furi_hal_spi_bus_trx(h,tx,rx,2,5);furi_hal_gpio_write(h->cs,true);furi_hal_spi_release(h);
    if(read)*value=rx[1];
    return ok && !(rx[0]&0x80);
}
static const uint8_t preset[]={
    0x02,0x0D,0x0B,0x06,0x08,0x32,0x07,0x00,0x15,0x04,0x14,0x00,0x13,0x02,
    0x12,0x00,0x11,0x83,0x10,0x67,0x18,0x18,0x19,0x16,0x1D,0x90,0x1C,0x00,
    0x1B,0x07,0x20,0xFB,0x22,0x10,0x21,0x56,0,0,0,0,0,0,0,0,0,0};
static bool radio_rx(App* a,bool audio) {
    furi_hal_subghz_idle();furi_hal_subghz_load_custom_preset(preset);
    const uint8_t regs[]={0x10,0x19,0x12,0x1B,0x1D};uint8_t values[]={0xFA,0,0,3,0x90};
    for(unsigned i=0;i<sizeof(regs);i++)if(!radio_register(regs[i],&values[i],false))return false;
    furi_hal_subghz_set_frequency_and_path(pmr_channel_hz(a->channel)-(audio?35000:0));
    furi_hal_gpio_init(&gpio_cc1101_g0,GpioModeInput,GpioPullNo,GpioSpeedLow);furi_hal_subghz_rx();furi_delay_ms(30);return true;
}
static int32_t radio_rssi(void) {
    uint8_t raw=0;if(!radio_register(0x34,&raw,true))return -127;return (int32_t)(int8_t)raw/2-74;
}
static void calibrate(App* a) {
    a->trigger_dbm=-90;a->audio_release_dbm=-97;a->audio_floor_dbm=-103;
    if(!radio_rx(a,false)){a->error=4;return;}
    int32_t center=radio_rssi();
    if(center< -80 && radio_rx(a,true)) {
        int32_t floor=radio_rssi();a->audio_floor_dbm=floor;a->trigger_dbm=CLAMP(center+10,-80,-100);a->audio_release_dbm=CLAMP(floor+4,-85,-105);
    }
    radio_rx(a,false);
}
static void packet_send(App* a,uint8_t type,uint8_t epoch,uint16_t sequence,const void* data,uint16_t n) {
    if(n>VOICE_BYTES || !a->connected)return;
    Packet p={.size=n+8};p.data[0]='P';p.data[1]='R';p.data[2]=type;p.data[3]=epoch;
    p.data[4]=n;p.data[5]=n>>8;p.data[6]=sequence;p.data[7]=sequence>>8;
    if(n)memcpy(p.data+8,data,n);
    if(type==0x81){FURI_CRITICAL_ENTER();a->latest_status=p;a->status_pending=true;FURI_CRITICAL_EXIT();}
    else if(furi_message_queue_put(a->outgoing,&p,0)!=FuriStatusOk){a->rx_dropped++;a->queue_drops++;}
}
static bool packet_next(App* a,Packet* p) {
    bool status;FURI_CRITICAL_ENTER();status=a->status_pending;if(status){*p=a->latest_status;a->status_pending=false;}FURI_CRITICAL_EXIT();
    return status || furi_message_queue_get(a->outgoing,p,5)==FuriStatusOk;
}
static void status_send(App* a) {
    uint8_t b[26]={a->channel,a->mode,a->state,a->listening,(uint8_t)(int8_t)a->rssi,a->epoch,a->error,3};
    uint16_t buffered=a->tx_head-a->tx_tail;memcpy(b+8,&buffered,2);
    uint16_t rate=VOICE_RATE;memcpy(b+10,&rate,2);
    uint32_t values[]={a->rx_dropped,a->underruns,a->tx_dropped};memcpy(b+12,values,12);uint16_t rx_rate=a->receive_rate;memcpy(b+24,&rx_rate,2);
    packet_send(a,0x81,a->epoch,0,b,sizeof(b));
}
static void input(InputEvent* e,void* context) {App* a=context;furi_message_queue_put(a->keys,e,0);}
static void draw(Canvas* c,void* context) {
    App* a=context;char s[64];canvas_clear(c);canvas_set_font(c,FontPrimary);canvas_draw_str(c,2,10,"PMR Connect");
    canvas_set_font(c,FontSecondary);snprintf(s,sizeof(s),"%s | CH %02lu | %ld dBm",a->mode?"USB":"BT",a->channel,a->rssi);canvas_draw_str(c,2,22,s);
    const char* state=!a->connected?(a->mode?"Connect browser via USB":"Pair in your web browser"):a->state==4?"TX: browser microphone":a->state==3?"Preparing microphone...":a->state==2?"RECEIVING to browser":a->listening?"Listening to selected channel":"Connected | Listening paused";
    canvas_draw_str(c,2,34,state);
    if(a->error){snprintf(s,sizeof(s),"Status %lu | See browser",a->error);canvas_draw_str(c,2,44,s);}
    else canvas_draw_str(c,2,44,"Browser: mic + speaker");
    canvas_draw_str(c,2,54,"L/R: channel  OK: BT / USB");canvas_draw_str(c,2,64,"Back: stop and exit");
}
static bool sample_rssi_irq(uint8_t* rssi) {
    const FuriHalSpiBusHandle* h=&furi_hal_spi_bus_handle_subghz;SPI_TypeDef* spi=h->bus->spi;
    uint32_t start=cycles_now();uint8_t rx[2]={0};bool ok=false;furi_hal_gpio_write(h->cs,false);
    while(furi_hal_gpio_read(h->miso))if((uint32_t)(cycles_now()-start)>2048)goto end;
    for(unsigned i=0;i<2;i++){
        while(!LL_SPI_IsActiveFlag_TXE(spi))if((uint32_t)(cycles_now()-start)>2048)goto end;
        LL_SPI_TransmitData8(spi,i?0:0xF4);
        while(!LL_SPI_IsActiveFlag_RXNE(spi))if((uint32_t)(cycles_now()-start)>2048)goto end;
        rx[i]=LL_SPI_ReceiveData8(spi);
    }
    while(LL_SPI_IsActiveFlag_BSY(spi))if((uint32_t)(cycles_now()-start)>2048)goto end;
    *rssi=rx[1];ok=!(rx[0]&0x80);
end:furi_hal_gpio_write(h->cs,true);return ok;
}
static void rx_irq(void* context) {
    if(!LL_TIM_IsActiveFlag_UPDATE(TIM2))return;
    LL_TIM_ClearFlag_UPDATE(TIM2);App* a=context;
    uint8_t raw=0;bool valid=sample_rssi_irq(&raw);
    int16_t pcm=valid?pmr_dsp_sample_rate(&a->dsp,(float)(int8_t)raw*.5f,a->receive_rate<4000):0;
    if(valid){a->rx_sum+=(int8_t)raw;a->rx_measurements++;}else a->read_errors++;
    a->resample_phase+=a->receive_rate;
    if(a->resample_phase>=8000){
        a->resample_phase-=8000;
        int32_t sample=pcm+((int32_t)a->previous-pcm)*(int32_t)a->resample_phase/(int32_t)a->receive_rate;
        if(a->rx_head-a->rx_tail<RX_BLOCKS){
            a->rx[a->rx_head%RX_BLOCKS][a->rx_count++]=(int16_t)sample;
            if(a->rx_count==VOICE_SAMPLES){a->rx_count=0;__DMB();a->rx_head++;}
        }else{a->rx_dropped++;a->sample_drops++;}
    }
    a->previous=pcm;
}
static void rx_pause(App* a) {
    if(a->rx_timer){LL_TIM_DisableIT_UPDATE(TIM2);LL_TIM_DisableCounter(TIM2);LL_TIM_ClearFlag_UPDATE(TIM2);furi_hal_interrupt_set_isr(FuriHalInterruptIdTIM2,NULL,NULL);furi_hal_bus_disable(FuriHalBusTIM2);a->rx_timer=false;}
    if(a->spi_owned){furi_hal_spi_release(&furi_hal_spi_bus_handle_subghz);a->spi_owned=false;}
}
static bool rx_start(App* a,bool fresh) {
    if(!radio_rx(a,true))return false;
    if(fresh){a->rx_head=a->rx_tail=0;a->rx_count=0;a->encoder_index=0;a->read_errors=0;memset(&a->dsp,0,sizeof(a->dsp));a->resample_phase=0;a->previous=0;}
    a->rx_sum=0;a->rx_measurements=0;pmr_rx_gate_start(&a->rx_gate,a->audio_floor_dbm,furi_get_tick());
    furi_hal_spi_acquire(&furi_hal_spi_bus_handle_subghz);a->spi_owned=true;
    furi_hal_bus_enable(FuriHalBusTIM2);LL_TIM_InitTypeDef cfg={0};cfg.Prescaler=63;cfg.Autoreload=124;
    LL_TIM_Init(TIM2,&cfg);LL_TIM_SetClockSource(TIM2,LL_TIM_CLOCKSOURCE_INTERNAL);LL_TIM_DisableARRPreload(TIM2);
    furi_hal_interrupt_set_isr(FuriHalInterruptIdTIM2,rx_irq,a);LL_TIM_ClearFlag_UPDATE(TIM2);LL_TIM_EnableIT_UPDATE(TIM2);LL_TIM_EnableCounter(TIM2);a->rx_timer=true;a->state=2;return true;
}
static void rx_drain(App* a) {
    while(a->rx_tail<a->rx_head){uint8_t data[VOICE_BYTES];ima_encode(a->rx[a->rx_tail%RX_BLOCKS],data,&a->encoder_index);__DMB();a->rx_tail++;packet_send(a,0x82,0,a->rx_sequence++,data,sizeof(data));}
}
static bool tx_bit(App* a) {
    if(a->pdm_phase==0){
        uint32_t tail=a->tx_tail;
        if(tail!=a->tx_head){a->pdm_sample=a->tx[tail&(TX_RING-1)];__DMB();a->tx_tail=tail+1;}
        else{a->pdm_sample=0;if(!a->ptt_draining)a->underruns++;}
    }
    a->pdm_phase=(a->pdm_phase+1)%8;
    a->pdm_error+=a->pdm_sample;bool bit=a->pdm_error>=0;a->pdm_error+=bit?-32767:32768;return bit;
}
static LevelDuration tx_yield(void* context) {
    App* a=context;bool level=a->carry_valid?a->pdm_carry:tx_bit(a);a->carry_valid=false;uint32_t us=0;
    for(unsigned run=0;run<256;run++){
        a->pdm_slot++;us+=a->pdm_slot==6?20:21;if(a->pdm_slot==6)a->pdm_slot=0;
        bool next=tx_bit(a);if(next!=level || run==255){a->pdm_carry=next;a->carry_valid=true;break;}
    }
    return level_duration_make(level,us-1);
}
static bool tx_start(App* a) {
    if(!furi_hal_subghz_is_tx_allowed(pmr_channel_hz(a->channel))){a->error=2;return false;}
    rx_pause(a);furi_hal_subghz_idle();furi_hal_subghz_load_custom_preset(preset);
    const uint8_t regs[]={0x12,0x15,0x22,0x10,0x11};uint8_t values[]={0,4,0x10,0x6A,0xE4};
    for(unsigned i=0;i<sizeof(regs);i++)if(!radio_register(regs[i],&values[i],false)){a->error=4;return false;}
    const uint8_t pa[8]={0x60,0,0,0,0,0,0,0};furi_hal_subghz_load_patable(pa);
    furi_hal_subghz_set_frequency_and_path(pmr_channel_hz(a->channel));furi_hal_subghz_set_async_mirror_pin(NULL);
    a->pdm_phase=a->pdm_slot=0;a->pdm_error=0;a->carry_valid=false;
    if(!furi_hal_subghz_start_async_tx(tx_yield,a)){a->error=2;return false;}
    a->state=4;a->tx_started=furi_get_tick();a->tx_tests++;furi_delay_ms(10);
    uint8_t state=0;radio_register(0x35,&state,true);a->tx_marcstate=state&31;return true;
}
static void radio_stop(App* a) {
    rx_pause(a);if(a->state==4)furi_hal_subghz_stop_async_tx();
    a->ptt_pending=false;a->ptt_stop=false;a->ptt_draining=false;a->state=a->listening?1:0;radio_rx(a,false);status_send(a);
}
static void accept_packet(App* a,const uint8_t* p) {
    uint16_t n=p[4]|((uint16_t)p[5]<<8);a->last_contact=furi_get_tick();
    if(p[2]==5){
        if((!a->ptt_pending && a->state!=4) || p[3]!=a->epoch || n!=VOICE_BYTES)return;
        int16_t pcm[VOICE_SAMPLES];if(!ima_decode(p+8,n,pcm)){a->error=8;return;}
        uint32_t head=a->tx_head;
        if(TX_RING-(head-a->tx_tail)<VOICE_SAMPLES){a->tx_dropped++;return;}
        for(unsigned i=0;i<VOICE_SAMPLES;i++)a->tx[(head+i)&(TX_RING-1)]=pcm[i];
        __DMB();a->tx_head=head+VOICE_SAMPLES;a->last_audio=furi_get_tick();return;
    }
    if(p[2]==1)return; // heartbeat
    if(p[2]==6){if(p[3]==a->epoch){if(n==1 && p[8] && a->state==4){a->ptt_draining=true;a->drain_until=furi_get_tick()+180;}else a->ptt_stop=true;}return;}
    Command cmd={.type=p[2],.epoch=p[3],.n=n};if(n)memcpy(cmd.data,p+8,n);
    if(furi_message_queue_put(a->commands,&cmd,0)!=FuriStatusOk)a->error=9;
}
static void consume(App* a,const uint8_t* data,size_t n) {
    for(size_t i=0;i<n;i++){
        if(a->parser_size==0 && data[i]!='P')continue;
        if(a->parser_size==1 && data[i]!='R'){a->parser_size=data[i]=='P'?1:0;continue;}
        if(a->parser_size>=WIRE_MAX){a->parser_size=0;continue;}
        a->parser[a->parser_size++]=data[i];
        if(a->parser_size>=8){
            uint16_t length=a->parser[4]|((uint16_t)a->parser[5]<<8);
            if(length>VOICE_BYTES){a->parser_size=0;a->error=8;continue;}
            if(a->parser_size==8U+length){accept_packet(a,a->parser);a->parser_size=0;}
        }
    }
}
static uint16_t ble_event(SerialServiceEvent e,void* context) {
    App* a=context;
    if(e.event==SerialServiceEventTypeDataSent)furi_event_flag_set(a->flags,ACK_FLAG);
    else if(e.event==SerialServiceEventTypeDataReceived){
        size_t n=furi_stream_buffer_send(a->incoming,e.data.buffer,e.data.size,0);if(n!=e.data.size)a->error=9;
    }else if(e.event==SerialServiceEventTypesBleResetRequest){a->connected=false;a->ptt_stop=true;}
    return furi_stream_buffer_spaces_available(a->incoming);
}
static void bt_status(BtStatus status,void* context) {
    App* a=context;a->ble_ready=status==BtStatusConnected;
}
static int32_t transport_worker(void* context) {
    App* a=context;uint8_t bytes[256];uint32_t last_status=0;
    while(a->running){
        if(a->mode || a->usb_active){furi_delay_ms(10);continue;}
        bool connected=a->ble_ready && furi_hal_bt_is_connected();
        if(connected && !a->attached){
            furi_delay_ms(80);bt_close_rpc_connection(a->bt);
            furi_hal_bt_serial_set_event_callback(4096,ble_event,a);furi_hal_bt_serial_set_rpc_status(FuriHalBtSerialRpcStatusActive);
            a->parser_size=0;a->error=0;a->status_pending=false;furi_message_queue_reset(a->outgoing);a->attached=true;a->connected=true;a->last_contact=furi_get_tick();
        }else if(!connected && a->attached){a->attached=false;a->connected=false;a->ptt_stop=true;a->listening=false;}
        if(!connected){furi_hal_bt_start_advertising();furi_delay_ms(10);continue;}
        size_t n=furi_stream_buffer_receive(a->incoming,bytes,sizeof(bytes),0);if(n)consume(a,bytes,n);
        if(furi_stream_buffer_is_empty(a->incoming))furi_hal_bt_serial_notify_buffer_is_empty();
        if(furi_get_tick()-last_status>(a->state==2?2000U:1000U)){status_send(a);last_status=furi_get_tick();}
        Packet p;
        if(packet_next(a,&p)){
            uint32_t started=furi_get_tick();
            furi_event_flag_clear(a->flags,ACK_FLAG);
            if(!furi_hal_bt_serial_tx(p.data,p.size)){a->ble_failures++;if(p.data[2]==0x82)a->rx_dropped++;furi_delay_ms(5);continue;}
            uint32_t flags=furi_event_flag_wait(a->flags,ACK_FLAG,FuriFlagWaitAny,500);
            uint32_t elapsed=furi_get_tick()-started;a->ble_packets++;a->ble_ms+=elapsed;if(elapsed>a->ble_max_ms)a->ble_max_ms=elapsed;
            if(flags&FuriFlagError){a->ble_timeouts++;if(p.data[2]==0x82)a->rx_dropped++;}
        }
    }
    return 0;
}
static void usb_session(Cli* cli,App* a) {
    if(!a->mode || a->usb_active){printf("SELECT_USB_MODE\r\n");return;}
    a->usb_active=true;a->usb_abort=false;a->error=0;a->connected=true;a->parser_size=0;a->last_contact=furi_get_tick();
    printf("PMRCONNECT1\r\n");uint8_t data[256];uint32_t last_status=0;
    while(a->running && !a->usb_abort && cli_is_connected(cli)){
        size_t n=cli_read_timeout(cli,data,sizeof(data),5);if(n)consume(a,data,n);
        if(furi_get_tick()-last_status>1000){status_send(a);last_status=furi_get_tick();}
        Packet p;for(unsigned i=0;i<4 && packet_next(a,&p);i++)cli_write(cli,p.data,p.size);
    }
    a->connected=false;a->listening=false;a->ptt_stop=true;a->usb_active=false;printf("\r\nCONNECT_END\r\n");
}
static void cli_command(Cli* cli,FuriString* args,void* context) {
    App* a=context;const char* text=furi_string_get_cstr(args);
    if(!strcmp(text,"linktest")){a->probe_until=furi_get_tick()+8000;a->probe_next=furi_get_tick();}
    if(!strcmp(text,"usb")){usb_session(cli,a);return;}
    if(!strcmp(text,"mode usb") || !strcmp(text,"mode bluetooth")){
        Command c={.type=20,.n=1,.data={!strcmp(text,"mode usb")}};furi_message_queue_put(a->commands,&c,0);
    }
    printf("version=3 mode=%lu connected=%u attached=%u channel=%lu state=%lu listen=%u rssi=%ld error=%lu rx_dropped=%lu tx_dropped=%lu underruns=%lu tx_tests=%lu tx_marcstate=%lu read_errors=%lu bt_active=%u bt_phy=%u bt_ready=%u ble_packets=%lu ble_ms=%lu ble_max=%lu\r\n",a->mode,a->connected,a->attached,a->channel,a->state,a->listening,a->rssi,a->error,a->rx_dropped,a->tx_dropped,a->underruns,a->tx_tests,a->tx_marcstate,a->read_errors,furi_hal_bt_is_active(),furi_hal_bt_is_connected(),a->ble_ready,a->ble_packets,a->ble_ms,a->ble_max_ms);
    printf("rx_rate=%lu carrier_checks=%lu sample_drops=%lu queue_drops=%lu ble_timeouts=%lu ble_failures=%lu audio_floor=%ld gate_reference=%ld gate_ambiguous=%u\r\n",a->receive_rate,a->carrier_checks,a->sample_drops,a->queue_drops,a->ble_timeouts,a->ble_failures,a->audio_floor_dbm,a->rx_gate.reference,a->rx_gate.ambiguous);
}
static void mode_set(App* a,uint32_t mode) {
    uint32_t previous=a->mode;a->listening=false;radio_stop(a);a->usb_abort=true;
    // Quiesce the transport worker before restarting Bluetooth's second core.
    a->mode=2;a->connected=false;furi_delay_ms(250);
    if(previous==0){bt_disconnect(a->bt);furi_hal_bt_serial_set_event_callback(0,NULL,NULL);a->attached=false;a->ble_ready=false;}
    a->parser_size=0;a->error=0;furi_stream_buffer_reset(a->incoming);furi_message_queue_reset(a->outgoing);a->status_pending=false;
    if(!mode){bt_set_profile(a->bt,BtProfileSerial);furi_hal_bt_start_advertising();}
    else furi_hal_bt_stop_advertising();
    a->receive_rate=mode?RECEIVE_RATE:RECEIVE_RATE_BLE;a->mode=mode;
}
int32_t pmr_connect_app(void* args) {
    UNUSED(args);App* a=malloc(sizeof(App));memset(a,0,sizeof(*a));a->channel=9;a->receive_rate=RECEIVE_RATE_BLE;a->rssi=-127;a->running=true;
    a->keys=furi_message_queue_alloc(8,sizeof(InputEvent));a->commands=furi_message_queue_alloc(8,sizeof(Command));
    a->outgoing=furi_message_queue_alloc(8,sizeof(Packet));a->incoming=furi_stream_buffer_alloc(4096,1);a->flags=furi_event_flag_alloc();
    Gui* gui=furi_record_open(RECORD_GUI);a->view=view_port_alloc();view_port_draw_callback_set(a->view,draw,a);view_port_input_callback_set(a->view,input,a);gui_add_view_port(gui,a->view,GuiLayerFullscreen);
    Cli* cli=furi_record_open(RECORD_CLI);cli_add_command(cli,"pmr_connect",CliCommandFlagParallelSafe,cli_command,a);
    furi_hal_power_insomnia_enter();furi_hal_subghz_reset();calibrate(a);
    a->bt=furi_record_open(RECORD_BT);a->was_bt_active=furi_hal_bt_is_active();snprintf(a->original_name,sizeof(a->original_name),"%s",bt_get_profile_adv_name(a->bt));
    bt_disconnect(a->bt);bt_set_status_changed_callback(a->bt,bt_status,a);bt_set_profile(a->bt,BtProfileSerial);furi_hal_bt_start_advertising();
    a->worker=furi_thread_alloc_ex("PmrLink",4096,transport_worker,a);furi_thread_start(a->worker);
    uint32_t next_ui=0,stable=0,last_measure=0;
    while(a->running){
        uint32_t now=furi_get_tick();InputEvent key;
        if((int32_t)(a->probe_until-now)>0 && now>=a->probe_next){uint8_t zeros[VOICE_BYTES]={0};packet_send(a,0x83,0,a->rx_sequence++,zeros,sizeof(zeros));a->probe_next+=(VOICE_SAMPLES*1000U+a->receive_rate-1)/a->receive_rate;}
        while(furi_message_queue_get(a->keys,&key,0)==FuriStatusOk){
            if(key.type!=InputTypeShort)continue;
            if(key.key==InputKeyBack){a->running=false;break;}
            if(key.key==InputKeyOk){mode_set(a,!a->mode);stable=0;}
            else if((key.key==InputKeyLeft || key.key==InputKeyRight) && a->state!=4){
                radio_stop(a);a->channel=key.key==InputKeyRight?(a->channel%16+1):(a->channel==1?16:a->channel-1);calibrate(a);status_send(a);stable=0;
            }
        }
        Command cmd;
        while(furi_message_queue_get(a->commands,&cmd,0)==FuriStatusOk){
            if(cmd.type==20){mode_set(a,cmd.data[0]);continue;}
            if(!a->connected)continue;
            if(cmd.type==2 && cmd.n==1){a->listening=cmd.data[0]!=0;if(a->state!=4)radio_stop(a);a->error=0;stable=0;}
            else if(cmd.type==3 && cmd.n==1 && cmd.data[0]>=1 && cmd.data[0]<=16){
                if(a->state==4 || a->ptt_pending)a->error=3;else{radio_stop(a);a->channel=cmd.data[0];calibrate(a);stable=0;}
            }else if(cmd.type==4 && cmd.n==1 && cmd.data[0]==a->channel && a->state!=4 && !a->ptt_pending){
                radio_stop(a);a->rssi=radio_rssi();
                if(a->rssi>a->trigger_dbm){a->error=1;}
                else{a->error=0;a->underruns=0;a->tx_head=a->tx_tail=0;a->epoch=cmd.epoch;a->last_audio=now;a->ptt_pending=true;a->ptt_stop=false;a->state=3;}
            }else if(cmd.type==7){a->listening=false;a->ptt_stop=true;a->usb_abort=true;}
            status_send(a);
        }
        if(a->connected && (int32_t)(now-a->last_contact)>1500){a->listening=false;a->ptt_stop=true;a->error=5;}
        if(a->state==1 && (!a->connected || !a->listening)){a->state=0;status_send(a);}
        if(a->state==4 || a->ptt_pending){
            bool expired=!a->connected || (int32_t)(now-a->last_audio)>400 || (a->state==4 && now-a->tx_started>PTT_MAX_MS);
            if(a->ptt_stop || expired || (a->ptt_draining && (int32_t)(now-a->drain_until)>=0)){if(expired)a->error=5;radio_stop(a);stable=0;}
            else if(a->ptt_pending && a->tx_head-a->tx_tail>=VOICE_SAMPLES*2){
                if(tx_start(a))a->ptt_pending=false;else radio_stop(a);status_send(a);
            }
        }else if(a->state==2){
            rx_drain(a);
            if(!a->connected || !a->listening || a->read_errors>8){radio_stop(a);stable=0;}
            else{
                if(now-last_measure>=20){
                    FURI_CRITICAL_ENTER();int32_t sum=a->rx_sum;uint32_t n=a->rx_measurements;a->rx_sum=0;a->rx_measurements=0;FURI_CRITICAL_EXIT();
                    if(n){a->rssi=sum/(int32_t)(2*n)-74;pmr_rx_gate_update(&a->rx_gate,a->rssi,now);}
                    last_measure=now;
                }
                if(pmr_rx_gate_needs_check(&a->rx_gate,now)){
                    a->carrier_checks++;rx_pause(a);radio_rx(a,false);int32_t rssi=radio_rssi();
                    if(rssi<a->trigger_dbm-4){rx_drain(a);a->state=a->listening?1:0;stable=0;status_send(a);}
                    else if(!rx_start(a,false)){a->error=4;a->listening=false;radio_stop(a);}
                }
            }
        }else if(a->connected && a->listening){
            a->state=1;a->rssi=radio_rssi();stable=a->rssi>a->trigger_dbm?stable+1:0;
            if(stable>=3){if(!rx_start(a,true)){a->error=4;a->listening=false;}last_measure=now;stable=0;status_send(a);}
        }
        if(now>=next_ui){view_port_update(a->view);next_ui=now+150;}
        furi_delay_ms(10);
    }
    a->listening=false;radio_stop(a);a->usb_abort=true;
    while(a->usb_active)furi_delay_ms(10);
    furi_thread_join(a->worker);furi_thread_free(a->worker);bt_set_status_changed_callback(a->bt,NULL,NULL);bt_disconnect(a->bt);furi_hal_bt_serial_set_event_callback(0,NULL,NULL);
    bt_set_profile(a->bt,BtProfileSerial);if(!a->was_bt_active)furi_hal_bt_stop_advertising();
    furi_record_close(RECORD_BT);furi_hal_subghz_idle();furi_hal_subghz_sleep();furi_hal_power_insomnia_exit();
    cli_delete_command(cli,"pmr_connect");furi_record_close(RECORD_CLI);
    view_port_enabled_set(a->view,false);gui_remove_view_port(gui,a->view);view_port_free(a->view);furi_record_close(RECORD_GUI);
    furi_message_queue_free(a->keys);furi_message_queue_free(a->commands);furi_message_queue_free(a->outgoing);furi_stream_buffer_free(a->incoming);furi_event_flag_free(a->flags);free(a);return 0;
}
