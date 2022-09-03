#include "osapi.h"
#include "ip_addr.h"
#include "espconn.h"
#include "user_interface.h"

static os_timer_t init_timer;
static os_timer_t run_timer, unblink_timer;

struct _esp_udp master_conn_udp = {
  .remote_port = 4242,
  .local_port = 4242,
  .remote_ip = { 192, 168, 1, 160 },
};

struct espconn master_conn = {
  .type = ESPCONN_UDP,
  .proto.udp = &master_conn_udp,
};

#define STATUS_INIT   2
#define STATUS_OK     1
#define STATUS_BAD    3

static uint8_t status = STATUS_INIT;
#define BLINK_SET(n)  GPIO_OUTPUT_SET(2, (n))

static uint32_t counter = 0;

void unblink(void *arg)
{
  BLINK_SET(1);
}

void data_send(void *arg)
{
  counter++;
  espconn_sendto(&master_conn, (uint8_t *) &counter, sizeof(uint32_t));
  BLINK_SET(0);
  os_timer_arm(&unblink_timer, 200, 1);
}

uint8_t data_init(void)
{
  if (espconn_create(&master_conn))
    return STATUS_BAD;

  os_timer_disarm(&init_timer);

  os_timer_disarm(&unblink_timer);
  os_timer_setfn(&unblink_timer, (os_timer_func_t *) unblink, NULL);

  os_timer_disarm(&run_timer);
  os_timer_setfn(&run_timer, (os_timer_func_t *) data_send, NULL);
  os_timer_arm(&run_timer, 1000, 1);
  return STATUS_OK;
}

void status_blink(void *arg)
{
  static uint8_t cur = 0;

  if (cur < status * 2)
    BLINK_SET(cur & 1);

  if (wifi_station_get_connect_status() == 5)
  {
    status = data_init();
    if (status == STATUS_OK)
    {
      cur = 0;
      return;
    }
  }
  
  cur++;
  cur %= 10;
}

void ICACHE_FLASH_ATTR user_init(void)
{
    gpio_init();

    uart_init(115200, 115200);
    os_printf("SDK version:%s\n", system_get_sdk_version());

    wifi_set_opmode(STATION_MODE);
    struct station_config wifi_conf = {
      .ssid = "ucw",
      .password = "NeniDobrAniZel",
    };
    wifi_station_set_config(&wifi_conf);

    PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO2_U, FUNC_GPIO2);

    os_timer_disarm(&init_timer);
    os_timer_setfn(&init_timer, (os_timer_func_t *)status_blink, NULL);
    os_timer_arm(&init_timer, 100, 1);
}
