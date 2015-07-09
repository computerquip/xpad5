# The last attempted driver.

This driver is a bit different from the previous ones. It separates the USB driver and the input driver.
The goal with this is to provide a generic input layer that I can create an abstraction layer over. 
And the goal for that is to provide an XInput-like API for WINE. 

# Known Driver Implementation Issues:
  * Synchronization between the workqueue and interrupt handlers is... well... wrong.  
  * Limitation of 4 controllers. This will certainly need design changes. 
  * I'm not sure if a single threaded workqueue per controller is appropriate. 
  * I do not know how to tell different wireless controllers apart. See below. 
  * Outgoing requests should not be synchronous... not sure why I did it that way anymore. 
  
# Packet Protocol Issues

## Wiress Controller Information
There's things we still don't know about the wireless adapter. In particular, the wireless controller
hands us a few packets that seem to be handshake packets for the Xbox 360 console. However, one of these
packets for sure has information that alloss us to tell it apart from the other wireless controllers. I
need to find that packet and figure out how to interpret it. 

## Misunderstanding About Packets
A lot of the packets we may be misusing heavily. A lot of the packets we send are just copy and pasted
from the stream of data we view from the Windows driver. It's hard, if not impossible, to tell if what
we're doing is the correct way of doing things. The only thing I can say is to test, test, and test some more. 
