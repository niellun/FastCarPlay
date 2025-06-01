# FastCarPlay
This is C++ implementation of carplay receiver for "Autobox" dongles. 
The purpose of the project is to make application lightweight to run on Raspberry PI Zero 2W using hardware decoding.

## Dongles
The dongles are readily available from Amazon or Aliexpress labeled by Carlinkit. They also seems to have official web site https://www.carlinkit.com/.
Devices might have different vendor and product id's. Check your with lsusb and update settings if necessary.

## Setup
### Dependencies
The project is based on SDL2, FFMPEG, LIBUSB. It use XXD for resource embedding.
```
sudo apt install build-essential xxd libsdl2-dev libsdl2-ttf-dev libavformat-dev libavcodec-dev libavutil-dev libswscale-dev libusb-1.0-0-dev libssl-dev
```
To run the application you also need to install runtime
```
sudo apt install ffmpeg libsdl2-2.0-0 libsdl2-ttf-2.0-0 libusb-1.0-0 libssl3
```

### USB Permissions and device id
On linux app may not have permisiions to read USB device. You need to create udev rule to grant persmissions for dongle.
First you need to figure out your idVendor and idProduct. 
```
lsusb
```
You will see list of devices, try to find the one that looks like dongle. Also you might need to run lsusb several time, cause my dongle seems to be disconnecting all the time. You need to find line like
```
Bus 003 Device 066: ID 1314:1520 Magic Communication Tec. Auto Box
```
So in my case ID 1314:1520 shows idVendor 1314 and idProduct 1520. We need to use those to create udev rules. If yours are different you also need to put them in settings.txt and run application with settings.txt as argument. Remember that in settings.txt values are in decimal, so you need to convert hex values to base 10 first. 
Create udev rules, replace <__Vendor__> <__Product__> and <__Your user__> with your informations
```
echo 'SUBSYSTEM=="usb", ATTRS{idVendor}=="<Vendor>", ATTRS{idProduct}=="<Product>", GROUP="<Your user>", MODE="0660"' | sudo tee /etc/udev/rules.d/50-carlinkit.rules
## example
## SUBSYSTEM=="usb", ATTRS{idVendor}=="1314", ATTRS{idProduct}=="1520", GROUP="linux", MODE="0660"
sudo udevadm control --reload-rules
sudo udevadm trigger
```

### Build and run
The application can be started with settings file. Sample of the settings file can be found in settings.txt. Make sure that you put proper device id in settings and app have access to usb (see previous step)
The project is using make. You can edit ./conf/settings.txt after copy if needed. From the repository root run following
```
make clean
make release
mkdir ./conf
cp ./settings.txt /conf
./out/app ./conf/settings.txt
```

### Customisation
You can change font and background images by replacing files in ./src/resource
- background.bmp for background image. Use BMP format only.
- font.ttf for font. Use TTF fdrmat only

The names of the file need to be exactly same. Resources are embedded in executable, remake the project to regenerate resources
```
make clean
make release
```

### Keys
The following keys have been mapped:
- Left - navigate left
- Right - navigate right
- Enter - select active item
- Backspace - Go back
- f - toggle fullscreen mode

## Status
What is working:
- Video
- Audio (multiple channels)
- Key navigation
- Simple touch

What is not working:
- Multi touch - i have no means to test it
- Microphone - that's next step for me to figure out how to feed sound
- Telephone - the listening part will work, but because there is no mic implementation you can't speak

### Notes
Regardless the resolution there are 2 types of settings
- source-width source-height source-fps - defines what video parameters will be requested from device
- width height fps - defines video drawing resolution and fps

The SDL will anyway scale the image to your screen/window size using internal HW scaling, so ideally you should use same values for width and height.
If the source parameters will be different the scaling will be applied, however that scaling is not hardware accelerated and can consume a lot of CPU.
You can set the 'scaler' in setting to define scaling algorithm. On my Raspbery Pi Zero 2W even easiest algorithm loads CPU to 100% and cause fram drop.

Increasing FPS above Source-FPS will cause app to run UI loop with less delays and do more event polling. This can increase responsivenes of the system, but also will make X11 to use more resources.

### Progress and plans
Done
- ✓ Implement direct buffer transfer from video decoder to renderer (should reduce amount of memory copies and CPU load)
- ✓ Control audio buffers better (now system use 3 decoding threads but in reality only 2 required)
- ✓ Reduce music volume when there is navigation messages
- ✓ Add abilities to run script on device connect and device disconnect
- ✓ Add encrypted USB communication option with magic code 0x55bb55bb for new firmware
- ✓ Improve touch responsiveness
- ✓ Protocol debugging option + python script to decode usb dumps
- ✓ Android Auto (have not tested much, but seems to work for me)

Next 
- Add microphone support (Calls, Siri)
- CAR menu with status, settings and options to run custom scripts


## Acknowledgement
The project is inspired and based on great work done by other developers:
- ![pycarplay by electric-monk](https://github.com/electric-monk/pycarplay)
- ![carplay-receiver by harrylepotter](https://github.com/harrylepotter/carplay-receiver)
- ![react-carplay by rhysmorgan134](https://github.com/rhysmorgan134/react-carplay)
- ![carplay-client by rayphee](https://github.com/rayphee/carplay-client)

The project is licenced under GPL-3 licence. See LICENCE for details.
The project is using Open Sans font (https://fonts.google.com/specimen/Open+Sans). See FONT_LICENCE for details.

## Finally
If you have any questions, suggestions or you find problems running this feel free to open issue.
