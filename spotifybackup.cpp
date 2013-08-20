#include <tchar.h>
#include <stdio.h>
#include <stdint.h>
#include <windows.h>
#include <api.h>

#include "appkey.h"

/** Number of playlists still loading. */
LONG g_playlistsLoading = (LONG)-1;
/** Whether any particular playlist has been processed (array by index). */
bool *g_playlistsLoaded;

sp_session *g_session;
HANDLE g_notifyEvent = NULL;
int g_loggedOut = 0;

sp_playlist_callbacks g_playListCallbacks;
sp_playlistcontainer_callbacks g_pcCallbacks;

void SP_CALLCONV notify_main_thread(sp_session *) {
    SetEvent(g_notifyEvent);
} 

void SP_CALLCONV logged_out(sp_session *) {
    g_loggedOut = 1; // Terminate main thread.
} 

/** Not thread safe, assumes one callback at a time. Trivial to fix if needed. */
void process_playlist(sp_playlist *pl, int index) {
    // TODO: this it tedious. Everyone has to do smth like that, so why can't libspotify?
    if (!sp_playlist_is_loaded(pl)) {
        return;
    }
    int trackCount = sp_playlist_num_tracks(pl);
    for (int i = 0; i < trackCount; ++i) {
        sp_track *track = sp_playlist_track(pl, i);
        if (!sp_track_is_loaded(track)) {
            return;
        }
        for (int j = 0; j < sp_track_num_artists(track); ++j) {
            if (!sp_artist_is_loaded(sp_track_artist(track, j))) {
                return;
            }
        }
    }
    // Finally, the whole thing is loaded. Check if the callback is spurious.
    if (g_playlistsLoaded[index]) {
        return;
    }
    g_playlistsLoaded[index] = true;
    sp_playlist_remove_callbacks(pl, &g_playListCallbacks, (void*)(intptr_t)index);

    int linkBufferSize = 128;
    char *linkBuffer = new char[linkBufferSize];

    printf("*** Playlist [%s]\n", sp_playlist_name(pl));
    for (int i = 0; i < trackCount; ++i) {
        sp_track *track = sp_playlist_track(pl, i);
        printf("[%c] [", sp_track_is_starred(g_session, track) ? '*' : ' ');
        char *comma = "";
        for (int j = 0; j < sp_track_num_artists(track); ++j) {
            printf("%s%s", comma, sp_artist_name(sp_track_artist(track, j)));
            comma = ", ";
        }
        int duration = sp_track_duration(track);
        printf("] - [%s] [%d:%02d] [", sp_track_name(track), duration / 60000, (duration / 1000) % 60);
        // Print a link.
        sp_link *link = sp_link_create_from_track(track, 0);
        if (NULL != link) {
            int newSize = sp_link_as_string(link, linkBuffer, linkBufferSize);
            if (linkBufferSize <= newSize) {
                linkBufferSize = newSize + 1;
                linkBuffer = new char[linkBufferSize];
                newSize = sp_link_as_string(link, linkBuffer, linkBufferSize);
                if (linkBufferSize <= newSize) {
                    fprintf(stderr, "Unexpected result from sp_link_as_string after buffer was expanded to %d: %d\n", linkBufferSize, newSize);
                    exit(1);
                }
            }
            printf("%s", linkBuffer);
            sp_link_release(link);
        } else {
            printf("no link");
        }
        
        printf("]\n");
    }
    fflush(stdout);
    delete linkBuffer;
    // TODO: Why are we doing interlockeddecrement if we are not threadsafe anyway?
    LONG remaining = InterlockedDecrement((volatile LONG*)&g_playlistsLoading);
    fprintf(stderr, "Playlist ended, %d remaining\n", remaining);
    if (0 == remaining) {
        delete g_playlistsLoaded;
        sp_session_logout(g_session); // This should cause the callback and terminate the main thread.
    }
}

static void SP_CALLCONV playlist_callback(sp_playlist *pl, void *userdata) {
    // userdata contains the index inside g_playlistsLoaded
    process_playlist(pl, (int)(intptr_t)userdata);
}

/** Not thread safe, assumes one callback at a time. We expect a single call and could just enforce that. */
void process_container(sp_playlistcontainer *pc) { 
    if (!sp_playlistcontainer_is_loaded(pc)) {
        return;
    }
    sp_playlistcontainer_remove_callbacks(pc, &g_pcCallbacks, NULL);

    int playlistCount = g_playlistsLoading = sp_playlistcontainer_num_playlists(pc);
    if (0 == playlistCount) {
        fprintf(stderr, "Cannot find any playlists despite successful login\n");
        exit(1);
    }
    fprintf(stderr, "Processing %d playlists\n", playlistCount);

    // Playlists might not be loaded.
    memset(&g_playListCallbacks, 0, sizeof(g_playListCallbacks));
    g_playListCallbacks.playlist_state_changed = g_playListCallbacks.playlist_metadata_updated = &playlist_callback;
    g_playlistsLoaded = new bool[playlistCount];
    memset(g_playlistsLoaded, 0, sizeof(bool) * playlistCount);
    for (int i = 0; i < playlistCount; ++i) {
        sp_playlist *pl = sp_playlistcontainer_playlist(pc, i);
        sp_playlist_add_callbacks(pl, &g_playListCallbacks, (void*)(intptr_t)i);
        process_playlist(pl, i);
    }
}

void SP_CALLCONV container_loaded(sp_playlistcontainer *pc, void *) {
    process_container(pc);
}    

void SP_CALLCONV logged_in(sp_session *session, sp_error error) {
    if (SP_ERROR_OK != error) {
        fprintf(stderr, "Login failed: %s\n", sp_error_message(error));
        exit(1);
    }
    sp_playlistcontainer *pc = sp_session_playlistcontainer(session);
    if (NULL == pc) {
        fprintf(stderr, "Failed to get playlist container\n");
        exit(1);
    }
    fprintf(stderr, "Logged in successfully\n");
    // Container might not be loaded.
    memset(&g_pcCallbacks, 0, sizeof(g_pcCallbacks));
    g_pcCallbacks.container_loaded = &container_loaded;
    sp_playlistcontainer_add_callbacks(pc, &g_pcCallbacks, NULL);
    process_container(pc);
}

int main(int argc, char* argv[]) {
    g_notifyEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (NULL == g_notifyEvent) {
        fprintf(stderr, "Unable to create event: %d\n", GetLastError());
        return 1;
    }

    sp_session_callbacks session_callbacks;
    memset(&session_callbacks, 0, sizeof(session_callbacks));
    session_callbacks.logged_in = &logged_in;
    session_callbacks.logged_out = &logged_out;
    session_callbacks.notify_main_thread = &notify_main_thread;

    sp_session_config config;
    memset(&config, 0, sizeof(config));
    config.api_version = SPOTIFY_API_VERSION,
    config.cache_location = config.settings_location = "tmp",
    config.application_key = g_appkey;
    config.application_key_size = g_appkey_size;
    config.user_agent = "spotify-backup";
    config.callbacks = &session_callbacks;

    sp_error err = sp_session_create(&config, &g_session);
    if (SP_ERROR_OK != err) {
        fprintf(stderr, "Error: %s\n", sp_error_message(err));
        return 1;
    }

    // Read username and password if not present... PITA.
    char *usernameBuf = NULL, *passwordBuf = NULL;
    char *username = (argc > 1) ? argv[1] : NULL;
    if (NULL == username) {
        fprintf(stderr, "Username: ");
        username = fgets((usernameBuf = new char[128]), 128, stdin);
        if (NULL == username) {
            fprintf(stderr, "Unable to read username\n");
            return 1;
        }
        int offset = strlen(username);
        while ((--offset >= 0) && (username[offset] == 10 || username[offset] == 13));
        username[offset + 1] = 0;
    }
    char *password = (argc > 2) ? argv[2] : NULL;
    if (NULL == password) {
        HANDLE consoleHandle = GetStdHandle(STD_INPUT_HANDLE); 
        if (INVALID_HANDLE_VALUE == consoleHandle) {
            fprintf(stderr, "Cannot get console handle\n");
            return 1;
        }
        DWORD mode;
        if (!GetConsoleMode(consoleHandle, &mode) ||
                !SetConsoleMode(consoleHandle, mode & ~ENABLE_ECHO_INPUT)) {
            fprintf(stderr, "Unable to get or set console mode: %d\n", GetLastError());
            return 1;
        }
        fprintf(stderr, "Password: ");
        password = fgets((passwordBuf = new char[128]), 128, stdin);
        if (NULL == password) {
            fprintf(stderr, "Unable to read password\n");
            return 1;
        }
        int offset = strlen(password);
        while ((--offset >= 0) && (password[offset] == 10 || password[offset] == 13));
        password[offset + 1] = 0;
        if (!SetConsoleMode(consoleHandle, mode)) {
            fprintf(stderr, "Unable to reset console mode: %d\n", GetLastError());
            return 1;
        }
    }

    err = sp_session_login(g_session, username, password, 0, NULL);
    if (SP_ERROR_OK != err) {
        fprintf(stderr, "\nError logging in: %s\n", sp_error_message(err));
        return 1;
    }

    // Callbacks will do the job from here; wait for session to end (see process_playlist).
    // While we wait we have to drive libspotify to actually do stuff.
    int nextTimeout = INFINITE;
    while (!g_loggedOut) {
        WaitForSingleObject(g_notifyEvent, nextTimeout);
        do {
            sp_session_process_events(g_session, &nextTimeout);
        } while (0 == nextTimeout);
    }

    // Cleanup... not that it matters in a script, we don't clean up for abnormal exit.
    sp_session_release(g_session);
    if (usernameBuf) {
        delete usernameBuf;
    }
    if (passwordBuf) {
        delete passwordBuf;
    }
    CloseHandle(g_notifyEvent);
    fprintf(stderr, "Success!\n");
    return 0;
}

