from machine import Pin, I2C
import onewire, ds18x20, time
from sh1106 import SH1106


# -------------------------------------------------
# OLED OTOMATIK ALGILAMA (SH1106 → SSD1306)
# -------------------------------------------------

# Güç ilk verildiğinde biraz bekle (OLED'in ayağa kalkması için)
time.sleep(0.3)

# I2C başlat
i2c = I2C(0, scl=Pin(1), sda=Pin(0), freq=400000)
address = 0x3C

oled = None
oled_var = False

# OLED gelene kadar I2C hattını tara
print("I2C tarama basladi...")
scan_list = i2c.scan()
while 0x3C not in scan_list:
    print("OLED yok, tekrar taraniyor...", scan_list)
    time.sleep(0.05)
    scan_list = i2c.scan()

print("OLED I2C'de goruldu:", scan_list)

# Önce SH1106 dene
try:
    oled_test = SH1106(128, 64, i2c)
    oled_test.fill(0)
    oled_test.text("SH1106 OK", 0, 0)
    oled_test.show()
    time.sleep(1)
    oled = oled_test
    oled_var = True
    print("SH1106 bulundu!")
except Exception as e:
    print("SH1106 yok → SSD1306 deneniyor...", e)

# SH1106 olmazsa SSD1306 dene
if not oled_var:
    try:
        oled_test = SSD1306_I2C(128, 64, i2c, addr=address)
        oled_test.fill(0)
        oled_test.text("SSD1306 OK", 0, 0)
        oled_test.show()
        time.sleep(1)
        oled = oled_test
        oled_var = True
        print("SSD1306 bulundu!")
    except Exception as e:
        print("OLED bulunamadi!", e)
        oled_var = False

# OLED bulunduysa ekranı temizle
if oled_var:
    oled.fill(0)
    oled.show()

# -------------------------------------------------
# RGB LED AYARLARI (ORTAK ANOT)
# -------------------------------------------------
COMMON_ANODE = True

RGB_PINS = [
    {"r": 7,  "g": 8,  "b": 9},    # LED 1
    {"r": 4,  "g": 5,  "b": 6},    # LED 2
    {"r": 11, "g": 12, "b": 13},   # LED 3
]

rgb_leds = []
for cfg in RGB_PINS:
    rgb_leds.append({
        "r": Pin(cfg["r"], Pin.OUT),
        "g": Pin(cfg["g"], Pin.OUT),
        "b": Pin(cfg["b"], Pin.OUT),
    })

def write_chan(pin, on):
    if COMMON_ANODE:
        pin.value(0 if on else 1)
    else:
        pin.value(1 if on else 0)

def set_all(r, g, b):
    for led in rgb_leds:
        write_chan(led["r"], r)
        write_chan(led["g"], g)
        write_chan(led["b"], b)

# DÜZELTİLMİŞ RENK FONKSİYONLARI
def color_blue():   set_all(True, False, False)   # Mavi
def color_green():  set_all(False, True, False)   # Yeşil
def color_red():    set_all(False, False, True)   # Kırmızı
def color_off():   set_all(False, False, False)

# -------------------------------------------------
# SICAKLIK EŞİKLERİ
# -------------------------------------------------
DEFAULT_ESIK_ALT = 15.0
DEFAULT_ESIK_UST = 25.0

esik_alt = DEFAULT_ESIK_ALT
esik_ust = DEFAULT_ESIK_UST

ESIK_MIN = 0.0
ESIK_MAX = 40.0
ESIK_ADIM = 0.5     # 0.5°C artır/azalt

# -------------------------------------------------
# DS18B20 AYARLARI
# -------------------------------------------------
data_pin = Pin(2)
ow = onewire.OneWire(data_pin)
ds = ds18x20.DS18X20(ow)
roms = ds.scan()

sensor_var = bool(roms)
son_sicaklik = 0.0

# -------------------------------------------------
# OLED SICAKLIK EKRANI
# -------------------------------------------------
def guncelle_oled():
    if not oled_var:
        return
    oled.fill(0)
    oled.text("Sicaklik:", 0, 0)
    oled.text("{:.2f} C".format(son_sicaklik), 0, 16)
    oled.text("SOGUK   : <{:.1f}C".format(esik_alt), 0, 36)
    oled.text("OPTIMUM : {:.1f}-{:.1f}C".format(esik_alt, esik_ust), 0, 46)
    oled.text("SICAK   : >{:.1f}C".format(esik_ust), 0, 56)
    oled.show()

# -------------------------------------------------
# BUTONLAR
# -------------------------------------------------
btn_up    = Pin(16, Pin.IN, Pin.PULL_UP)  # Eşik +
btn_down  = Pin(17, Pin.IN, Pin.PULL_UP)  # Eşik -
btn_left  = Pin(18, Pin.IN, Pin.PULL_UP)  # Parlaklık -
btn_right = Pin(19, Pin.IN, Pin.PULL_UP)  # Parlaklık +
btn_reset = Pin(20, Pin.IN, Pin.PULL_UP)  # Reset eşikler

# Debounce
DEBOUNCE_MS = 120
last_state = {16:1, 17:1, 18:1, 19:1, 20:1}
last_time  = {16:0, 17:0, 18:0, 19:0, 20:0}

# -------------------------------------------------
# PARLAKLIK (5 KADEME)
# -------------------------------------------------
brightness_levels = [0x00, 0x20, 0x80, 0xC0, 0xFF]
bright_index = 2

def apply_brightness():
    if not oled_var:
        return
    value = brightness_levels[bright_index]
    oled.write_cmd(0x81)
    oled.write_cmd(value)

apply_brightness()

# -------------------------------------------------
# SISTEM BASLANGICI
# -------------------------------------------------
color_off()
if oled_var:
    oled.fill(0)
    if sensor_var:
        oled.text("Sicaklik Sistemi", 0, 0)
    else:
        oled.text("DS18B20 YOK", 0, 0)
    oled.show()
    time.sleep(1)

# -------------------------------------------------
# ANA LOOP
# -------------------------------------------------
last_temp = time.ticks_ms()
temp_busy = False
convert_start = 0

while True:
    now = time.ticks_ms()

    # ----------------------------
    # BUTON TARAMA
    # ----------------------------
    for pin_num in [16, 17, 18, 19, 20]:
        val = Pin(pin_num, Pin.IN, Pin.PULL_UP).value()

        if val == 0 and last_state[pin_num] == 1:
            if time.ticks_diff(now, last_time[pin_num]) > DEBOUNCE_MS:
                last_time[pin_num] = now

                # EŞİK + 0.5
                if pin_num == 16:
                    if esik_ust + ESIK_ADIM <= ESIK_MAX:
                        esik_alt += ESIK_ADIM
                        esik_ust += ESIK_ADIM

                # EŞİK - 0.5
                if pin_num == 17:
                    if esik_alt - ESIK_ADIM >= ESIK_MIN:
                        esik_alt -= ESIK_ADIM
                        esik_ust -= ESIK_ADIM

                # PARLAKLIK -
                if pin_num == 18:
                    if bright_index > 0:
                        bright_index -= 1
                        apply_brightness()

                # PARLAKLIK +
                if pin_num == 19:
                    if bright_index < len(brightness_levels) - 1:
                        bright_index += 1
                        apply_brightness()

                # RESET → Eşikleri defaulta çek
                if pin_num == 20:
                    esik_alt = DEFAULT_ESIK_ALT
                    esik_ust = DEFAULT_ESIK_UST

                guncelle_oled()

        last_state[pin_num] = val

    # ----------------------------
    # DS18B20 SICAKLIK ÖLÇÜMÜ
    # ----------------------------
    if sensor_var:
        if not temp_busy:
            if time.ticks_diff(now, last_temp) >= 1000:
                ds.convert_temp()
                convert_start = now
                temp_busy = True
        else:
            if time.ticks_diff(now, convert_start) >= 750:
                son_sicaklik = ds.read_temp(roms[0])
                temp_busy = False
                last_temp = now
                guncelle_oled()

                if son_sicaklik < esik_alt:
                    color_blue()
                elif son_sicaklik < esik_ust:
                    color_green()
                else:
                    color_red()

    time.sleep_ms(2)
