CCN-VLC-AccessModule
====================

The CCN 1.0 version of the VLC access module that enables streaming of movies over CCN

This code was tested against the VLC-2.1.6 [1] using gcc-4.8 and g++-4.8.
Using significantly later versions (e.g. 2.2.4) will cause compile errors, as there
were VLC API changes (and it also requires gcc-5).

You should generally follow the instructions in `src/README.install`.  The differences
are:

- Use VLC-2.1.6 [1].
- You may use the Athena forwarder, rather than metis.  Either should work.
- Use `CCNx_Distillery/usr/bin/ccnxSimpleFileTransfer_Server` instead of 
  `CCN-Tutorial-Demo` and `tutorial_Server`.
- Use "ccnx:" everywhere instead of "lci:" in URI.
- The instruction on how to use `ln -s` was incorrect and causes a bad link to be placed
  in the VLC plugin/access directory.  Please remove the bad link and use the updated command
  `ln -s /home/USERNAME/CCN-VLC-AccessModule/src/libaccess_ccn_plugin.so /home/USERNAME/VLC-Built/lib/vlc/plugins/access/libaccess_ccn_plugin.so`

To use `ccnxSimpleFileTransfer_Server`:
- Compile CCNx_Distillery following its instructions.
- Create a directory with a movie in it, e.g. BigBuckBunny [2].
- Start the server (after metis or athena).  Note that the VLC plugin hard-codes the
  namespace `ccnx:/ccnx/tutorial` so we need to use that.

```
USERNAME@sautte:~$ CCNx_Distillery/usr/bin/ccnxSimpleFileTransfer_Server -l ccnx:/ccnx/tutorial -s 1200 ~/movies
Server Configuration: 
  namePrefix:    [ccnx:/ccnx/tutorial]
  doPreChunk:    [false]
  directoryPath: [/home/USERNAME/movies]
  chunkSize:     [1200]
  beVerbose:     [false]
ccnxSimpleFileTransfer_Server: now serving files from /home/USERNAME/movies
```

- In a new terminal, verify the server is operating:
```
USERNAME@sautte:~$ ./CCNx_Distillery/usr/bin/ccnxSimpleFileTransfer_Client -l ccnx:/ccnx/tutorial list
Client Configuration: 
  namePrefix:    [ccnx:/ccnx/tutorial]
  doSaveToDisk:  [true]
  beVerbose:     [false]

  Command: [list] []

Directory Listing follows:
  BigBuckBunny_320x180.mp4  (64657027 bytes)
45 bytes transferred in 8 ms (0.005 MB/sec)
```

- You can then try the VLC plugin:
```
VLC-Built/bin/vlc ccnx://BigBuckBunny_320x180.mp4
```

NOTE: You must use two "//" forward slashes here, otherwise VLC will try to use a local file path.

- If you have problems, try running metis with debugging: `CCNx_Distillery/usr/bin/metis_daemon --log processor=debug` and it will show you the URI names in the Interest messages.

[1] http://download.videolan.org/pub/videolan/vlc/2.1.6/vlc-2.1.6.tar.xz
[2] http://download.blender.org/peach/bigbuckbunny_movies/BigBuckBunny_320x180.mp4


