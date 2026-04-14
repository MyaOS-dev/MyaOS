# Network Driver Matrix

Ниже реальное состояние драйверов в ядре на текущий момент.

Легенда:
- `[x]` есть рабочий драйвер в ядре
- `[~]` устройство распознаётся в PCI-каталоге, но драйвер не реализован
- `[ ]` сейчас не поддерживается данным стеком (часто это не PCI-устройство)

## Реально поддержано
- `[x]` Realtek RTL8139 (`net.rtl8139`)
- `[x]` Realtek RTL8111/8168/8211/8411 (`net.rtl8169`)
- `[x]` Intel 82540EM / 82574L (`net.e1000`)
- `[x]` Intel I210 / I219 / I350 в режиме e1000-compatible (`net.e1000`)
- `[x]` virtio-net (legacy virtio-pci `0x1000`, `net.virtio`)

## Распознаётся, но драйвера пока нет
- `[~]` Realtek RTL8125
- `[~]` Intel X520 / X540 / X710 / E810 / PRO100
- `[~]` Intel Wi-Fi 6E AX210/AX1675 [Typhoon Peak] (`net.ax210`, PCI probe + `wlan0`, ищет firmware в `/lib/firmware/ax210.ucode` или `/lib/firmware/iwlwifi-ty-a0-gf-a0.ucode`; диагностика доступна через `/sys/net_nic_firmware_*` и `wifi firmware-*`, data path пока disabled)
- `[~]` Broadcom BCM5700 / BCM5719 / BCM5720 / NetXtreme II
- `[~]` Marvell Yukon 88E8056 / Alaska 88E1512
- `[~]` Qualcomm Atheros AR8131 / AR8161
- `[~]` Mellanox ConnectX-3 / ConnectX-4 / ConnectX-5
- `[~]` Chelsio T4 / T5 / T6
- `[~]` Solarflare SFN5122F / SFN7000
- `[~]` vmxnet3 / Hyper-V netvsc
- `[~]` virtio-net modern pci (`0x1041`)
- `[~]` 3Com 3C905 / DEC Tulip 21140 / NE2000
- `[~]` Cisco VIC 1240 / 1380
- `[~]` Amazon ENA / Google gVNIC / Netronome NFP4000 / BlueField DPU

## Вне текущей PCI-модели ядра
- `[ ]` ASIX AX88772 / AX88179 (USB NIC)
- `[ ]` Microchip ENC28J60 / LAN8720 / LAN9252 (SPI/PHY/industrial)
- `[ ]` Synopsys DesignWare MAC / Cadence GEM / Broadcom BCM2711 Ethernet (обычно SoC/Platform bus)
