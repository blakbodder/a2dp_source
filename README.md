#a2dp_source

code to send .wav file over bluetooth to A2DP speaker (without pulseaudio
or alsa). the raspberry pi can output audio to an A2DP sink without this
code but the code can be used as a start point for other projects
(eg. playing samples in a loop.  playing tones in response to user input.)

tested on raspberry pi zero + bullseye + bluez 5.5  with ruark radio in
bluetooth mode.  it should work with other devices that support A2DP.

a2dp.py uses a hard-coded bd_addr (colon-separated hex digits that
identify the speaker-device).  to find this do\
`bluetoothctl scan on`\
patch the bd_addr into a2dp.py where it says `DEVICE_ADDR="AA:BB:CC:DD:EE:FF"`

a2dp.py uses a cpython module to encode and pump out the data to the
bluetooth sink.  build the _Pump module with\
`python setup_pump.py build_ext --inplace`\
ignore compiler warnings.

if the speaker device has not been paired with the pi before do:\
bluetoothctl\
 #power on\
 #pair XX:XX:XX:XX:XX:XX  (substitute your device addr)\
 #trust XX:XX:XX:XX:XX:XX\
 #quit

the pulseaudio daemon on the pi manages a source endpoint 
(path=/MediaEndpoint/A2DPSource/sbc) that needs to be disabled
before a2dp.py can run. so the play sequence is:\
bluetooth on\
make discoverable\
`pulseaudio --kill`\
turn on speaker (ruark says "Connecting")\
`python a2dp.py <wavfile to play>` 

on the pi-zero the audio stutters a little - to fix, turn off wifi 
beforehand.\
v1mono.wav is a short tune in mono format.\
sin.wav is a stereo tone ~1kHz 1 second long.\
the sbc library comes from sbc 1.3  and rtp.h is from bluez 4.101 

if your device requires the pi to connect to it, try\
 `bluetoothctl connect XX:XX:XX:XX:XX:XX` 
(substitute device bd_addr) in another console window while a2dp.py is 
waiting for connection.  i have tried ConnectProfile(SINK_UUID) within 
a2dp.py but that fails.
