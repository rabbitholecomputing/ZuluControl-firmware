# ZuluControl-firmware
ZuluControl-firmware provides a web-based remote control interface that runs on a separate RP2050 or RP2350 based boards with a RM2 wireless module, including Pico 1W and Pico 2W development boards and communicates with the ZuluIDE or ZuluSCSI via I2C.

## Building and Installing

You can obtain a UF2 image built automatically via GitHub by going to the [releases](https://github.com/rabbitholecomputing/ZuluControl-firmware/releases) page.

If you want to build your own image, follow the instructions in [Getting started with Raspberry Pi Pico](https://datasheets.raspberrypi.com/pico/getting-started-with-pico.pdf). Chapter 2 provides instructions on how to obtain the SDK and setup the build tools. **Note:** be sure to correctly set the `PICO_SDK_PATH` environment variable (e.g., `export PICO_SDK_PATH=/home/myusername/repos/pico-sdk/`)

After you have setup the SDK and build tools run the following instructions:

1. Obtain the ZuluControl-firmware repository by running: `git clone git@github.com:rabbitholecomputing/ZuluControl-firmware.git`
2. From within a terminal, change to the directory containing the ZuluControl-firmware repository
3. Configure the project with `cmake -B build -DPICO_BOARD=pico_w` (or `pico2_w`)
4. Build the project with `cmake --build build`

After doing this, you should find the `zulucontrol.uf2` file in the build directory.

To build an universal binary that works on both Pico W and Pico 2W, see [universal_binary/CMakeLists.txt](universal_binary/CMakeLists.txt).

## Configuring WiFi Settings on ZuluIDE SD Card

ZuluControl reads the WiFi SSID and password from the ZuluIDE via I2C. You set the values for these by creating (or editing) the `zuluide.ini` file on the SD card and adding the `[UI]` section with the `wifipassword` and `wifissid` fields as shown below.


    [UI]
    wifissid="MY_NETWORK_SSID" # SSID for the WIFI network
    wifipassword="MY_PASSWORD" # Password for the WIFI network. 
    # Any special characters must be in double quotes, as per the example above
    # Static IP configuration (optional, DHCP is the default)
    wifi_static_ip="192.168.1.42"
    wifi_static_netmask="255.255.255.0"
    wifi_static_gateway="192.168.1.1"

## Configuring WiFi Setting on ZuluSCSI SD Card

ZuluControl read the WiFi SSID and password from the ZuluSCSI
via I2C, You set the values by creating (or editing) the `zuluscsi.ini` file on the SD card by adding the `[WebUI]` section with the `WiFiPassword` and `WiFiSSID` fields. You can optionally set a static IP. The `[WebUI]` fields shown below. 

    [WebUI]
    WiFiSSID = "MY_NETWORK_SSID"
    WiFiPassword = "MY_PASSWORD"

    # Static IP configuration (optional, DHCP is the default)
    StaticIP = "192.168.1.42"
    Netmask = ""255.255.255.0"
    Gateway = "192.168.1.1"

## Connecting ZuluControl on a Pico W/2W to ZuluIDE or ZuluSCSI

Normally the ZuluIDE or ZuluSCSI would be connected to a ZuluControl board designed for a specifically to run the ZuluControl-firmware, but it can be wired directly to a Pico W/2W.

After installing ZuluControl-firmware onto a Pico W/2W, the Pico must be connected to the ZuluIDE or ZuluSCSI via 3 wires. The following diagram shows which pins on the Pico W must be connected to the ZuluIDE. Additionally, you must power the Pico (e.g., via its USB port or any other methods described by the Raspberry PI Pico documentation).

Lastly, be sure to restart the ZuluIDE or ZuluSCSI with the ZuluControl connected. ZuluIDE and ZuluSCSI firmware does not support hot plugging on the I2C connection used by the ZuluControl.

![Wiring ZuluControl to ZuluIDE [^1] ](pico-pinout-zuluide.svg)
![Wiring ZuluControl to ZuluSCSI[^1] ](pico-pinout-zuluscsi.svg)

## Using the Web Page

The included web page is a very basic proof-of-concept for how to use the web services. You access the web site by opening a browser and going to `index.html` using the IP address assigned to the ZuluControl via DHCP. For example, if your DHCP server assigned the ZuluControl `10.0.0.13` then you would open `http://10.0.0.13/index.html` in your browser. Be warned, there is no security of any kind built into this included website.

## Using the Web Service

The web service allows you to build your own interface or custom integration for controlling the ZuluIDE. Be warned, there is no security of any kind build into these web-service endpoints. The included web-page (`index.html`) provides an example of how these web service endpoints can be used.

### `/status`

#### ZuluIDE
Get request that returns a JSON representation of the current state of the.
#### ZuluSCSI
Get request the returns a JSON representation of the current state of all removable devices.

### `/filenames`

#### ZuluIDE
Get request that returns all of the images in the system in a JSON array. It will return a `{"status":"wait"}` JSON document when it is in the processes of fetching the images. Using this endpoint to retrieve all of the images in a single operation will load all of the images into the ZuluControl's memory.

#### ZuluSCSI
Same behavior as ZuluIDE except use `/filenames?scsiId=x` where x is the SCSI ID of device's images should be listed. 

### `/eject`

#### ZuluIDE
Get request that causes the ZuluIDE to eject an image. Always returns a `{"status":"OK"}` JSON document.

#### ZuluSCSI
`/eject?scsiId=x` where `x` is the the SCSI ID of the device send eject to. Always returns a `{"status":"OK"}` JSON document

### `/image?imageName=myimage.iso`

#### ZuluIDE

Get request that causes the ZuluIDE to load the image passed via the `imageName` query parameter.

### `/image?scsiId=x&imageName=myimage.iso`

#### ZuluSCSI

Get request that causes the ZuluSCSI to load the image passed via the `imageName` query parameter to the device set by `scsiId=x` where `x` is the SCSI ID of the target device.


[^1]: Pico Pinout image is © 2012-2024 Raspberry Pi Ltd and is licensed under a [Creative Commons Attribution-ShareAlike 4.0 International](https://creativecommons.org/licenses/by-sa/4.0/) (CC BY-SA) licence.
