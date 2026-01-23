# OutlanderPHEVBMS Ported to platfrom.io to run on LilyGo T2-Can

## Port Status

Code is ported using coding agents, the core functionality is meant to be kept as is. 
Some tests were added with extra focus on safety features, some problems were discovered and the code was fixed. 
One of them is main loop counter reaching maximum value after roughly 2 months of continousus running. 
More info in this PR desc https://github.com/wodor/OutlanderPHEVBMS/pull/1

There are some changes in serial console output for convenience. 
A web server is added for easier display of module information. 
Balancing is sent every 200ms, not 400ms. 

![web server](t2can_port/web_server.png)

The code is tested to run with Yuasa LEV40-8S modules (the blue ones).
No resistors added, just the last CMU must be connected with extra 2 wires to CANH and CANL to terminate. 
Remember to put the large screws back on the +/- termminal of the modules, if it is left loose CMU will not connect properly. Temps will show ok but voltages will be 65533 or 0. 

## Original Project 

Control Over the Mitsubishi Outlander CMU modules

Reading out the modules over CAN and triggering them to balance.

This software is designed to run on the SimpBMS.

User Manual that covers some software functions https://github.com/tomdebree/SimpBMS 

## Documentation

- [T-2Can Port](t2can_port/README.md) - Modern ESP32-S3 implementation with web dashboard
- [Remote Logging Plan](docs/REMOTE_LOGGING_PLAN.md) - Comprehensive plan for remote logging with MQTT, buffering, and monitoring 
