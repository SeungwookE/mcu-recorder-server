# mcu-recorder-server

<b>Overview</b>
mcu recorder development based on janus-gateway mjr file
janus gateway is a SFU media relaying server. this is meaning there is no any proc to mixing video or audio while handling media packets.


<b>How the Janus-gateway works?</b>

1. broadcast rtp packets to other session participants.
2. recording (actually stacking rtp packets) single peer rtp stream in mjr file (with mjr format).
3. if recording option is on, this server stack rtp-packet on mjr, and after that it relay packet to other participants (This is meaning recording proc is on realtime as well!!)
5. No mixing video before it broadcast packets.
6. just broadcast every single rtp stream to other session participants on proper timeline. (Selective forwarding)


So I came up with some idea about module, mixing separated mjr files on realtime.



**what this module do?

1. keep looking at designated directory where mjr files will be saved.
2. if there is new mjr file is created
  1) create new Manager to grab multiple mjr files which is included in same session
  2) parsing mjr file name to detect if they are in same session. 
3. parsing MJR / rtp packets to get video frames.
4. decoding videoframes with ffmpeg internal libraries.
5. then we will get decoded visualizable videoframes.
6. edit them as the way we want with ffmpeg.

