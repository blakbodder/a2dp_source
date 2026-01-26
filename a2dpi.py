# this works similarly to a2dp.py but initiates connection
from gi.repository import Gio,GLib
import dbus
import dbus.exceptions
import dbus.service
from dbus.mainloop.glib import DBusGMainLoop
from socket import *
import sys
from _Pump import datapump

ADAPTER_PATH = "/org/bluez/hci0"
#edit next line so it is the bd_addr of your speaker device
DEVICE_ADDR = "AA:BB:CC:DD:EE:FF"
DEVICE_PATH = ADAPTER_PATH + "/dev_" + DEVICE_ADDR.replace(':','_')
SOURCE_UUID = "0000110a-0000-1000-8000-00805f9b34fb"
SINK_UUID = "000110b-0000-1000-8000-00805f9b34fb"

bluez_name = "org.bluez"
INTROSPECT_IFACE = "org.freedesktop.DBus.Introspectable"
OM_IFACE = "org.freedesktop.DBus.ObjectManager"
PROPS_IFACE = "org.freedesktop.DBus.Properties"
ADAPTER_IFACE="org.bluez.Adapter1"
ENDPOINT_IFACE = "org.bluez.MediaEndpoint1"
TRANSPORT_IFACE = "org.bluez.MediaTransport1"

DBusGMainLoop(set_as_default=True)
mainloop = GLib.MainLoop()
system_bus = dbus.SystemBus()
gtransport=None

def error_handler(error):
    print("error", error)

# register endpoint causes SOURCE_UUID to be added to adapter's UUIDs list
# this change triggers sigh which connects to the SINK of the device
def sigh(signal, changed, invalidated):
    print("sigh")
    system_bus.remove_signal_receiver(sigh, signal_name="PropertiesChanged",
              dbus_interface=PROPS_IFACE, path=ADAPTER_PATH)
    system_bus.call_async(bluez_name, DEVICE_PATH, "org.bluez.Device1",
    "ConnectProfile", 's', [SINK_UUID], None, error_handler )

adpt = system_bus.get_object(bluez_name, ADAPTER_PATH)
adapter = dbus.Interface(adpt, PROPS_IFACE)
uuids = adapter.Get(ADAPTER_IFACE, 'UUIDs')
for uuid in uuids:
    if uuid == SOURCE_UUID:
       print("A2DP source found.  First disable pulseaudio with\npulseaudio --kill")
       quit()

if len(sys.argv)==2:
    filename = sys.argv[1]
    wavf=open(filename,"rb")
    h = wavf.read(40)
    numchans = h[22]+(h[23]<<8)
    freq = h[24] + (h[25]<<8) + (h[26]<<16) + (h[27]<<24)
    print("numchans=", numchans, "samplefreq=",freq)
    wavf.close()
else:
    print("usage:  python a2dpi.py <wavfilename>")
    quit()

class Endpoint(dbus.service.Object):

    def __init__(self, bus, path, properties, configuration):
        self.bus=bus
        self.path=path
        self.properties=properties
        self.configuration=configuration
        dbus.service.Object.__init__(self, bus, self.path)

    def get_path(self):
        return  dbus.ObjectPath(self.path)

    def get_properties(self):
        return self.properties

    @dbus.service.method(ENDPOINT_IFACE, in_signature='oay', out_signature='')
    def SetConfiguration(self, transport, properties):
        global gtransport
        print("SET CONFIG")
        gtransport = transport
        print("transport=", transport)
       # for k,v in properties.items():  print(k,v)
       # cannot aquire transport immediately.  wait for ServicesResolved.
        system_bus.add_signal_receiver(sig_handler,
            signal_name="PropertiesChanged",
            dbus_interface=PROPS_IFACE, path=DEVICE_PATH)

    @dbus.service.method(ENDPOINT_IFACE, in_signature='ay', out_signature='ay')
    def SelectConfiguration(self, capabilities):
        print("SELECT CONFIG", capabilities)
        return self.configuration

    @dbus.service.method(ENDPOINT_IFACE, in_signature='o', out_signature='')
    def ClearConfiguration(self, transport):
        print("CLEAR")

    @dbus.service.method(PROPS_IFACE, in_signature='s', out_signature='a{sv}')
    def GetAll(self, iface):
        if iface != EDNPOINT_IFACE:  print("GETALL WRONG IFACE")
        return self.properties

adapter = system_bus.get_object(bluez_name, ADAPTER_PATH)
media = dbus.Interface(adapter, "org.bluez.Media1")
eppath = "/Endpoint/A2DPSource/sbc"

if numchans==1: b0=0x28	  # 44100 mono
else: b0=0x22             # 44100 stereo
SBC_CAPS = dbus.Array([dbus.Byte(b0), dbus.Byte(0xff), dbus.Byte(10), dbus.Byte(53)])
SBC_CONFIG = dbus.Array([dbus.Byte(0x28), dbus.Byte(0x15), dbus.Byte(2), dbus.Byte(32)])
SBC_CODEC = dbus.Byte(0)

properties = dbus.Dictionary({ "UUID": SOURCE_UUID,
                               "Codec": SBC_CODEC,
                               "DelayReporting": False,
                               "Capabilities": SBC_CAPS })

system_bus.add_signal_receiver(sigh, signal_name="PropertiesChanged",
            dbus_interface=PROPS_IFACE, path=ADAPTER_PATH)

ep = Endpoint(system_bus, eppath, properties, SBC_CONFIG)
media.RegisterEndpoint(eppath, properties)
print(eppath, "registered")

def sig_handler(iface, changedprops, invalidated):
    global gtransport, filename, numchans
    print("sig received")
    for k,v in changedprops.items():
#        print(k, v)
        if k=='ServicesResolved' and v==1:
            transport_proxy = system_bus.get_object(bluez_name, gtransport)
            ufd, rmtu, wmtu = transport_proxy.Acquire(dbus_interface=TRANSPORT_IFACE)
            print("rmtu=", rmtu, "wmtu=", wmtu)
            fd = ufd.take()
#            print("fd=",fd)
#            print(sock.getpeername())
            sock = fromfd(fd, AF_BLUETOOTH, SOCK_SEQPACKET)
            mainloop.quit()
            datapump(sock.fileno(), filename, numchans)

mainloop.run()
