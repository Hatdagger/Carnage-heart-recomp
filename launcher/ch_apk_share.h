/* Carnage Heart: send the Android app to a tablet over the home Wi-Fi.
 *
 * After BUILD ANDROID APP the launcher shows a QR code. The tablet's camera
 * opens the link, which is a small web page served by the launcher itself:
 * a download button for CarnageHeart.apk and, below it, the player's disc
 * files (the Android app needs them too). Nothing leaves the home network.
 *
 * The server runs only while the QR screen is open (start() when it opens,
 * stop() when it closes). Every opening makes a new random code that is part
 * of the link, so an old link stops working. Only the files listed on the
 * page can be fetched; anything else is "not found".
 */
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace ch_apk_share {

/* CarnageHeart.apk in the kit folder: its path, size and change time
 * (seconds since 1970). False when there is none. */
bool apk_info(std::string* path, uint64_t* size, int64_t* mtime);

struct Status {
    bool        running = false;
    std::string url;          /* what the QR code holds */
    std::string error;        /* why it could not start, one line */
    int         downloads = 0;     /* files sent in full */
    std::string last_file;    /* the file being sent, or the last one sent */
    std::string last_client;  /* the tablet's address */
    uint64_t    sending_done = 0;  /* bytes of the file being sent now */
    uint64_t    sending_total = 0; /* 0 when nothing is being sent */
};

/* Opens the server. cue: the disc image's .cue (may be empty: then only the
 * app is offered). False (and Status::error) when it can't. */
bool start(const std::string& cue);
void stop();
Status status();

/* QR code for text: side length in modules (0 on failure) and one byte per
 * module, row by row, 1 = dark. */
int qr(const std::string& text, std::vector<uint8_t>* modules);

} /* namespace ch_apk_share */
