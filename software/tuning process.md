# Tuning process for gain and threshold parameters

If you have no oscilloscope to measure the telegram signaling, you are some kind of blind and it could get a game of testing to find out the right combination of gain and threshold for your specific environment. In this case I would go this way:

1. Be sure to have an easy way on your mobile phone to tune the parameters gain and threshold via MQTT and to see the recognition of the bell telegrams. I used Node Red, but you could also use an MQTT analyzer app on your phone.

2. Check if you see a heartbeat on MQTT topic SimpleBus/Heartbeat to secure your MQTT chain is online before making tests.

3. Start your tests at the floor door (apartment). Stand at the bell switch and make tests with your mobile phone in the hand to tune the parameters after each failure. For the start tune to 5/200 (gain/threshold) and go up with the threshold from 200mV up to max. 1000mV in steps of 50 for each test. If you find no recognition on MQTT topic SimpleBus/FloorDoor, start again at threshold 200 with gain rised in steps of 5 and so on until you get ON on MQTT.

4. Now make some tests at the entry door (MQTT topic SimpleBus/EntryDoor). The cable length is much longer, so the damping is higher for the telegram. The gain has to be also a bit higher for reliable recognition probably. So if you encounter a working bell signal on the floor door, it could be you have to add some gain for the entry door.

5. As a rule of thumb, the higher the gain is set, the higher you have to set the threshold, as the noise on the line is always amplified too (signal/noise ratio).

With this setup you should be able to find the right setting within short time.
