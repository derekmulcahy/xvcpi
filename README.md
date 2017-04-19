# Xilinx Virtual Cable Server for Raspberry Pi

[Xilinx Virtual Cable](https://github.com/Xilinx/XilinxVirtualCable/) (XVC) is a TCP/IP-based protocol that  acts like a JTAG cable and provides a means to access and debug your  FPGA or SoC design without using a physical cable.
A full description of Xilinx Virtual Cable in action is provided in the [XAPP1252 application note](https://www.xilinx.com/support/documentation/application_notes/xapp1251-xvc-zynq-petalinux.pdf).

**Xvcpi** implements an XVC server to allow a Xilinx FPGA or SOC to be controlled remotely by Xilinx Vivado using the Xilinx Virtual Cable protocol. Xvcpi uses TCP port 2542.

The **xvcpi** server runs on a Raspberry Pi which is connected, using JTAG, to the target device. **Xvcpi** bitbangs the JTAG control signals on the Pi pins. The bitbanging code was originally extracted from [OpenOCD](http://openocd.org).

# Wiring
Note: The Raspberry Pi is a 3.3V device. Ensure that the target device and the Pi are electrically compatible before connecting. 100 Ohm resistors may be placed inline on all of the JTAG signals to provide a degree of electrical isolation.

JTAG uses 4 signals, TMS, TDI, TDO and, TCK.
From the Raspberry Pi perspective, TMS, TDI and TCK are outputs, and TDO is an input.
The pin mappings for the Raspberry Pi header are:
```
TMS=25, TDI=10, TCK=11, TDO=9
```
In addition a ground connection is required. Pin 23 is a conveniently placed GND.

Note that XVC does not provide control of either SRST or TRST and **xvcpi** does not support a RST signal.

# Usage
Start **xvcpi** on the Raspberry Pi. An optional -v flag can be used for verbose output.

Vivado connects to **xvcpi** via an intermediate software server called hw_server. To allow Vivado "autodiscovery" of **xvcpi** via hw_server run:

```
hw_server -e 'set auto-open-servers xilinx-xvc:<xvcpi-server>:2542'
```

Alternatively, the following tcl commands can be used in the Vivado Tcl console to initiate a connection.

```
connect_hw_server
open_hw_target -xvc_url <xvcpi-server>:2542
```

Full instructions can be found in [ProdDoc_XVC_2014 3](ProdDoc_XVC_2014_3.pdf).

# Snickerdoodle
The initial purpose of **xvcpi** was to provide a simple means of programming the [Snickerdoodle](http://snickerdoodle.io).
# Licensing
This work, "xvcpi.c", is a derivative of "xvcServer.c" (https://github.com/Xilinx/XilinxVirtualCable)

"xvcServer.c" is licensed under CC0 1.0 Universal (http://creativecommons.org/publicdomain/zero/1.0/)
by Avnet and is used by Xilinx for XAPP1251.

"xvcServer.c", is a derivative of "xvcd.c" (https://github.com/tmbinc/xvcd)
by tmbinc, used under CC0 1.0 Universal (http://creativecommons.org/publicdomain/zero/1.0/).

Portions of "xvcpi.c" are derived from OpenOCD (http://openocd.org)

"xvcpi.c" is licensed under CC0 1.0 Universal (http://creativecommons.org/publicdomain/zero/1.0/)
by Derek Mulcahy.