# DX12

This project is my master's thesis. Developed in collaboration with ITMO (https://itmo.ru/ru/) and Sperasoft (https://sperasoft.ru/)

This project was taken as the basis for the graphics engine developed in the team. https://github.com/Pepengineers

Several interaction algorithms have been implemented:

## Shared Shadow Map
![SSM](https://media.discordapp.net/attachments/190175905824374784/864151422445944842/unknown.png)
# Result
![SSMResult](https://media.discordapp.net/attachments/190175905824374784/864151896842567680/unknown.png)

## Shared User Interface Blending
![SUIB](https://media.discordapp.net/attachments/190175905824374784/864152276481343578/unknown.png)
# Result
![SUIBResult](https://media.discordapp.net/attachments/190175905824374784/864152448770637864/unknown.png)

## Shared Particle System
![SPS](https://media.discordapp.net/attachments/190175905824374784/864153137799888946/unknown.png)


## Shared Hybrid Compute
![SHC](https://media.discordapp.net/attachments/190175905824374784/864153312567754772/unknown.png)
# Result with full shared
![SHCResult](https://media.discordapp.net/attachments/190175905824374784/864153400563466290/unknown.png?width=1036&height=557)
# Result with scaled resource
![SHCResult1](https://media.discordapp.net/attachments/190175905824374784/864153503207260170/unknown.png?width=1088&height=538)


For the project you need:
 1. Windows SDK 19041 version
 2. Internet for restore Nuget packages
 3. More then one GPU (and/or iGPU/dGPU)
 
Steps for build:
  1. Restore Submodule
  2. Restore Nuget packages
  3. Build any Sample and Copy 'Data' and 'Shaders' folder to build directory
