# Purpose

This is a port of this project to platformio to run on LilyGo T-2Can. 
The idea is to avoid buying simpBMS or at least to have another tool the debug during building home energy storage, built from dismantled Outlader PHEV battery. 

It compiles ok, connects to WiFi and is showing UI 
These setup instructions should work https://github.com/Xinyuan-LilyGO/T-2Can?tab=readme-ov-file#platformio

Just `cp config.h.template config.h` and set your wifi.

This is all done by an AI agent, I've got no idea about embedded. Hence more details in AGENTS.md

![web server](web_server.png)

## CanBUS is not tested yet

I haven't connected LilyGo T-2Can to the batteries yet. 
So don't get excited.