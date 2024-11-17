# Screen Cast

Connect your Oculus Quest to your computer with a USB cable.

Run in the console:

```
adb reverse tcp:8090 tcp:8090
```

Then, start the screen-cast server:

```
./screen-cast
```

Open in the Oculus Quest browser: `http://localhost:8090`

Note: the code base uses ffmpeg. I am not sure if it is compatible with MIT; I am not a lawyer.

Note 2: more detailed instructions will come soon™.
