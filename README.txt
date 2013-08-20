=== DISCLAIMER
I absolve all responsibility for whatever this program might do, the program is provided as is, no guarantees, use at your own risk, blah blah blah.
The code is mostly based on examples anyway so you can reuse it, modify it, sell it for money, fix bugs and mock the author for providing excuses for bad code in comments. The source is written like a script so you might want to clean it up in case of reuse.

=== To use the program
1) Go to bin/ and run it. 
Username and password can optionally be provided as first two arguments; if one or both are not provided prompt will appear.
The playlist contents will be dumped into stdout, whereas all prompts and diagnostic messages will go into stderr.
So, an example usage would be:
spotifybackup myusername 1> playlists.txt
If all goes well the txt file will contain your playlists in human readable form (machine-readable, not so much).

=== To use the source
1) Get libspotify for Win32 (https://developer.spotify.com/technologies/libspotify/#download).
2) Put libspotify.dll and libspotify.lib in lib/ directory.
3) Put api.h in include/libspotify directory.
4) Provide your appkey in appkey.h.
5) Build the project.

The source is written like a script so you might want to clean it up before reuse.
Also I started out assuming I'd need to make the thing thread-safe, but libspotify is essentially single-threaded with regard to client callbacks at the time of writing. If that changes, this is going to break horribly (easy to fix, though).