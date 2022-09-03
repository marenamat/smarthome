#include "osapi.h"
#include "ip_addr.h"
#include "espconn.h"
#include "user_interface.h"
#include "driver/i2c_master.h"

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

void unblink(void *arg)
{
  BLINK_SET(1);
}

static struct packet {
  uint16_t temp;
  uint16_t hum;
  uint8_t temp_crc;
  uint8_t hum_crc;
  uint8_t status;
  uint32_t counter;
} packet;

void try_i2c(void)
{
  packet.status = 0;
  packet.temp = packet.hum = 0;
  packet.temp_crc = packet.hum_crc = 0;

#define WB(x)  do { \
  packet.status++; \
  i2c_master_writeByte(x); \
  if (!i2c_master_checkAck()) { i2c_master_stop(); return; } \
} while (0)

  i2c_master_start();
  WB(0x88);
  WB(0x24);
  WB(0x0B);
//  WB(0x2C);
//  WB(0x0D);

  uint8_t msb, lsb;

  do {
    i2c_master_stop();
    os_printf("check\n");
//    os_delay_us(10);
    i2c_master_start();
    i2c_master_writeByte(0x89);
  } while (!i2c_master_checkAck());

  msb = i2c_master_readByte();
  i2c_master_send_ack();
  lsb = i2c_master_readByte();
  i2c_master_send_ack();
  packet.temp = (msb << 8) | lsb;
  packet.temp_crc = i2c_master_readByte();
  i2c_master_send_ack();

  msb = i2c_master_readByte();
  i2c_master_send_ack();
  lsb = i2c_master_readByte();
  i2c_master_send_ack();
  packet.hum = (msb << 8) | lsb;
  packet.hum_crc = i2c_master_readByte();
  i2c_master_send_nack();

  i2c_master_stop();

  packet.status = 0;
}

void data_send(void *arg)
{
  try_i2c();
  packet.counter++;

  os_printf("Packet: %04x %04x %02x %02x %02x cnt=%u\n",
      packet.temp, packet.hum, packet.temp_crc, packet.hum_crc, packet.status, packet.counter);
  espconn_sendto(&master_conn, (uint8_t *) &packet, sizeof(packet));
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
  os_timer_arm(&run_timer, 8000, 1);
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

    PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO2_U, FUNC_GPIO2);

    wifi_set_opmode(STATION_MODE);
    struct station_config wifi_conf = {
      .ssid = "ucw",
      .password = "NeniDobrAniZel",
    };
    wifi_station_set_config(&wifi_conf);

    i2c_master_gpio_init();
    i2c_master_init();

    os_timer_disarm(&init_timer);
    os_timer_setfn(&init_timer, (os_timer_func_t *)status_blink, NULL);
    os_timer_arm(&init_timer, 100, 1);
}
