#include "osapi.h"
#include "ip_addr.h"
#include "espconn.h"
#include "user_interface.h"
#include "driver/i2c_master.h"

#include "temphum.h"

#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))

static os_timer_t blink_timer, mainloop_timer, data_acquire_timer;

struct upstream {
  struct _esp_udp udp;
  struct espconn conn;
} connections[] = {
  {
    .udp = {
      .remote_port = 4242,
      .local_port = 4242,
      .remote_ip = { 192, 168, 1, 1 },
    },
  }, {
    .udp = {
      .remote_port = 4242,
      .local_port = 4242,
      .remote_ip = { 46, 36, 37, 149 },
    },
  },
};

uint8_t local_mac[6];

#define DATA_ACQUIRE_DELAY  3

#define STATUS_OK     1
#define STATUS_NOUDP  2
#define STATUS_NOWIFI 3
#define STATUS_NOMAC  4

static uint8_t status = STATUS_NOMAC;
static uint8_t blink_request = STATUS_NOMAC;
static uint8_t blink_hold = 0;

#define BLINK_SET(n)  GPIO_OUTPUT_SET(2, (n))

void data_acquire(void);
void data_request(void)
{
  blink_hold = 1;
  BLINK_SET(0);

#define WB(x)  do { \
  packet.status++; \
  i2c_master_writeByte(x); \
  if (!i2c_master_checkAck()) { os_printf("bad request: %u\n", packet.status); i2c_master_stop(); return; } \
} while (0)

  i2c_master_start();
  WB(0x88);
  WB(0x24);
  WB(0x0B);
  //  WB(0x2C);
  //  WB(0x0D);
  i2c_master_stop();

  os_timer_disarm(&data_acquire_timer);
  os_timer_setfn(&data_acquire_timer, (os_timer_func_t *) data_acquire, NULL);
  os_timer_arm(&data_acquire_timer, DATA_ACQUIRE_DELAY, 0);
}

void data_acquire(void)
{
  packet.status = 0;
  packet.version = PACKET_VERSION;
  packet.temp = packet.hum = 0;
  packet.temp_crc = packet.hum_crc = 0;
  memcpy(packet.mac, local_mac, sizeof(local_mac));

  uint8_t msb, lsb;
  uint16_t delay = 0;

  i2c_master_start();
  i2c_master_writeByte(0x89);

  if (!i2c_master_checkAck())
  {
    i2c_master_stop();
    os_printf("not ready\n");
    os_timer_arm(&data_acquire_timer, DATA_ACQUIRE_DELAY, 0);
    return;
  }

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
  packet.counter++;

  os_printf("Packet: %04x %04x %02x %02x %02x cnt=%u\n",
      packet.temp, packet.hum, packet.temp_crc, packet.hum_crc, packet.status, packet.counter);
  for (int i=0; i<ARRAY_SIZE(connections); i++)
    espconn_sendto(&connections[i].conn, (uint8_t *) &packet, sizeof(packet));

  blink_hold = 2;
}

static void reset_wifi(void)
{
  wifi_set_opmode(STATION_MODE);
  struct station_config wifi_conf = {
    .ssid = "ucw",
    .password = "NeniDobrAniZel",
  };
  wifi_station_set_config(&wifi_conf);
}

static void mainloop(void *_)
{
  switch (status) {
    case STATUS_NOMAC:
      if (!wifi_get_macaddr(STATION_IF, local_mac))
	break;
      status = STATUS_NOWIFI;
      reset_wifi();
      /* fall through */

    case STATUS_NOWIFI:
      if (wifi_station_get_connect_status() != 5)
	break;

      for (int i=0; i<ARRAY_SIZE(connections); i++)
      {
	connections[i].conn.type = ESPCONN_UDP;
	connections[i].conn.proto.udp = &connections[i].udp;
	espconn_create(&connections[i].conn);
      }

      /* fall through */
    
    case STATUS_OK:
      if (wifi_station_get_connect_status() != 5)
      {
	reset_wifi();
	status = STATUS_NOWIFI;
	break;
      }

      data_request();

    default:
      os_printf("BAD! status %d\n", status);
      status = STATUS_NOMAC;
      break;
  }

  blink_request = status;
}

void blinker(void *_)
{
  static uint8_t blink_status = 0;

  switch (blink_hold)
  {
    case 0: break;
    case 1: return;
    case 2: blink_hold = 0;
	    BLINK_SET(1);
	    return;
  }

  if (!blink_status)
    if (blink_request)
      blink_status = blink_request * 2;
    else
      return;
  
  BLINK_SET(blink_status-- & 1);
}

void ICACHE_FLASH_ATTR user_init(void)
{
  /* Setup GPIO */
  gpio_init();
  i2c_master_gpio_init();
  i2c_master_init();

  /* Setup debug */
  uart_init(115200, 115200);
  os_printf("SDK version:%s\n", system_get_sdk_version());

  /* Setup blinker */
  PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO2_U, FUNC_GPIO2);

  os_timer_disarm(&blink_timer);
  os_timer_setfn(&blink_timer, (os_timer_func_t *) blinker, NULL);
  os_timer_arm(&blink_timer, 100, 1);

  /* Setup mainloop */
  os_timer_disarm(&mainloop_timer);
  os_timer_setfn(&mainloop_timer, (os_timer_func_t *) mainloop, NULL);
  os_timer_arm(&mainloop_timer, 5000, 1);

  /* Run mainloop also now */
  mainloop(NULL);
}
